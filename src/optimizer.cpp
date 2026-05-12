#include "optimizer.hpp"

#include "ast.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pavic {
namespace {

struct ConstValue {
    enum class Kind { Unknown, Int, String, Bool };
    Kind kind = Kind::Unknown;
    int intValue = 0;
    bool boolValue = false;
    std::string stringValue;

    static ConstValue unknown() { return {}; }
    static ConstValue intConst(int v) {
        ConstValue c;
        c.kind = Kind::Int;
        c.intValue = v;
        return c;
    }
    static ConstValue boolConst(bool v) {
        ConstValue c;
        c.kind = Kind::Bool;
        c.boolValue = v;
        return c;
    }
    static ConstValue stringConst(std::string v) {
        ConstValue c;
        c.kind = Kind::String;
        c.stringValue = std::move(v);
        return c;
    }
    bool isKnown() const { return kind != Kind::Unknown; }
};

class Env {
public:
    void pushScope() { scopes_.emplace_back(); }
    void popScope() {
        if (!scopes_.empty()) {
            scopes_.pop_back();
        }
    }

    void declareVar(char name) {
        if (scopes_.empty()) {
            pushScope();
        }
        scopes_.back()[name] = ConstValue::unknown();
    }

    void assignVar(char name, ConstValue value) {
        for (std::size_t i = scopes_.size(); i > 0; --i) {
            auto& scope = scopes_[i - 1];
            auto it = scope.find(name);
            if (it != scope.end()) {
                it->second = std::move(value);
                return;
            }
        }
        if (scopes_.empty()) {
            pushScope();
        }
        scopes_.back()[name] = std::move(value);
    }

    void invalidateVar(char name) { assignVar(name, ConstValue::unknown()); }

    std::optional<ConstValue> lookup(char name) const {
        for (std::size_t i = scopes_.size(); i > 0; --i) {
            const auto& scope = scopes_[i - 1];
            auto it = scope.find(name);
            if (it != scope.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

private:
    std::vector<std::unordered_map<char, ConstValue>> scopes_;
};

std::optional<int> parseIntLiteral(const AstLiteralInt& lit) {
    try {
        return std::stoi(lit.lexeme());
    } catch (...) {
        return std::nullopt;
    }
}

ConstValue evalConstExpr(const AstExpr& e, const Env& env) {
    switch (e.nodeKind()) {
    case AstNodeKind::LiteralInt: {
        const auto* lit = static_cast<const AstLiteralInt*>(&e);
        const auto v = parseIntLiteral(*lit);
        return v.has_value() ? ConstValue::intConst(*v) : ConstValue::unknown();
    }
    case AstNodeKind::LiteralString:
        return ConstValue::stringConst(static_cast<const AstLiteralString*>(&e)->lexeme());
    case AstNodeKind::LiteralBool:
        return ConstValue::boolConst(static_cast<const AstLiteralBool*>(&e)->value());
    case AstNodeKind::IdentifierExpr: {
        const auto* id = static_cast<const AstIdentifierExpr*>(&e);
        const auto known = env.lookup(id->name());
        return known.has_value() ? *known : ConstValue::unknown();
    }
    case AstNodeKind::AddExpr: {
        const auto* add = static_cast<const AstAddExpr*>(&e);
        const ConstValue a = evalConstExpr(*add->left(), env);
        const ConstValue b = evalConstExpr(*add->right(), env);
        if (a.kind == ConstValue::Kind::Int && b.kind == ConstValue::Kind::Int) {
            return ConstValue::intConst(a.intValue + b.intValue);
        }
        return ConstValue::unknown();
    }
    case AstNodeKind::BooleanExprWrapper: {
        const auto* w = static_cast<const AstBooleanExprWrapper*>(&e);
        // Wrapper represents boolean as value.
        const auto* inner = w->inner();
        if (inner->nodeKind() == AstNodeKind::BooleanLiteralExpr) {
            return ConstValue::boolConst(static_cast<const AstBooleanLiteralExpr*>(inner)->value());
        }
        if (inner->nodeKind() == AstNodeKind::BinaryBoolExpr) {
            const auto* b = static_cast<const AstBinaryBoolExpr*>(inner);
            const ConstValue l = evalConstExpr(*b->left(), env);
            const ConstValue r = evalConstExpr(*b->right(), env);
            if (l.kind != ConstValue::Kind::Unknown && l.kind == r.kind) {
                bool eq = false;
                if (l.kind == ConstValue::Kind::Int) {
                    eq = l.intValue == r.intValue;
                } else if (l.kind == ConstValue::Kind::Bool) {
                    eq = l.boolValue == r.boolValue;
                } else if (l.kind == ConstValue::Kind::String) {
                    eq = l.stringValue == r.stringValue;
                }
                return ConstValue::boolConst(b->op() == AstBinaryBoolExpr::Op::Equal ? eq : !eq);
            }
        }
        return ConstValue::unknown();
    }
    default:
        return ConstValue::unknown();
    }
}

std::unique_ptr<AstExpr> cloneExpr(const AstExpr& e);
std::unique_ptr<AstBooleanExpr> cloneBoolExpr(const AstBooleanExpr& b);
std::unique_ptr<AstStatement> cloneStatement(const AstStatement& s);
std::unique_ptr<AstBlock> cloneBlock(const AstBlock& b);

std::unique_ptr<AstExpr> constToExpr(const ConstValue& c, AstSpan span) {
    if (c.kind == ConstValue::Kind::Int) {
        return std::make_unique<AstLiteralInt>(std::to_string(c.intValue), span);
    }
    if (c.kind == ConstValue::Kind::String) {
        return std::make_unique<AstLiteralString>(c.stringValue, span);
    }
    if (c.kind == ConstValue::Kind::Bool) {
        return std::make_unique<AstLiteralBool>(c.boolValue, span);
    }
    return nullptr;
}

std::unique_ptr<AstBooleanExpr> constToBoolExpr(const ConstValue& c, AstSpan span) {
    if (c.kind == ConstValue::Kind::Bool) {
        return std::make_unique<AstBooleanLiteralExpr>(c.boolValue, span);
    }
    return nullptr;
}

std::unique_ptr<AstExpr> optimizeExpr(const AstExpr& e, Env& env);
std::unique_ptr<AstBooleanExpr> optimizeBoolExpr(const AstBooleanExpr& b, Env& env);

std::unique_ptr<AstExpr> cloneExpr(const AstExpr& e) {
    switch (e.nodeKind()) {
    case AstNodeKind::LiteralInt:
        return std::make_unique<AstLiteralInt>(static_cast<const AstLiteralInt&>(e).lexeme(), e.span());
    case AstNodeKind::LiteralString:
        return std::make_unique<AstLiteralString>(static_cast<const AstLiteralString&>(e).lexeme(), e.span());
    case AstNodeKind::LiteralBool:
        return std::make_unique<AstLiteralBool>(static_cast<const AstLiteralBool&>(e).value(), e.span());
    case AstNodeKind::IdentifierExpr: {
        const auto& id = static_cast<const AstIdentifierExpr&>(e);
        auto out = std::make_unique<AstIdentifierExpr>(id.name(), e.span());
        if (id.hasResolvedDeclScope()) {
            out->setResolvedDeclScope(id.resolvedDeclScopeId());
        }
        return out;
    }
    case AstNodeKind::AddExpr: {
        const auto& a = static_cast<const AstAddExpr&>(e);
        return std::make_unique<AstAddExpr>(cloneExpr(*a.left()), cloneExpr(*a.right()), e.span());
    }
    case AstNodeKind::BooleanExprWrapper: {
        const auto& w = static_cast<const AstBooleanExprWrapper&>(e);
        return std::make_unique<AstBooleanExprWrapper>(cloneBoolExpr(*w.inner()), e.span());
    }
    default:
        return nullptr;
    }
}

std::unique_ptr<AstBooleanExpr> cloneBoolExpr(const AstBooleanExpr& b) {
    switch (b.nodeKind()) {
    case AstNodeKind::BooleanLiteralExpr:
        return std::make_unique<AstBooleanLiteralExpr>(static_cast<const AstBooleanLiteralExpr&>(b).value(), b.span());
    case AstNodeKind::BinaryBoolExpr: {
        const auto& cmp = static_cast<const AstBinaryBoolExpr&>(b);
        return std::make_unique<AstBinaryBoolExpr>(cmp.op(), cloneExpr(*cmp.left()), cloneExpr(*cmp.right()), b.span());
    }
    default:
        return nullptr;
    }
}

std::unique_ptr<AstBlock> cloneBlock(const AstBlock& b) {
    auto list = std::make_unique<AstStatementList>();
    if (b.statements()) {
        for (const auto& st : b.statements()->statements()) {
            list->addStatement(cloneStatement(*st));
        }
        list->setSpan(b.statements()->span());
    }
    return std::make_unique<AstBlock>(std::move(list), b.span());
}

std::unique_ptr<AstStatement> cloneStatement(const AstStatement& s) {
    switch (s.nodeKind()) {
    case AstNodeKind::PrintStatement:
        return std::make_unique<AstPrintStatement>(
            cloneExpr(*static_cast<const AstPrintStatement&>(s).expr()),
            s.span()
        );
    case AstNodeKind::AssignStatement: {
        const auto& a = static_cast<const AstAssignStatement&>(s);
        auto out = std::make_unique<AstAssignStatement>(a.name(), cloneExpr(*a.expr()), s.span());
        if (a.hasLhsResolvedDeclScope()) {
            out->setLhsResolvedDeclScope(a.lhsResolvedDeclScopeId());
        }
        return out;
    }
    case AstNodeKind::VarDeclStatement: {
        const auto& d = static_cast<const AstVarDeclStatement&>(s);
        return std::make_unique<AstVarDeclStatement>(d.typeName(), d.name(), s.span());
    }
    case AstNodeKind::WhileStatement: {
        const auto& w = static_cast<const AstWhileStatement&>(s);
        return std::make_unique<AstWhileStatement>(cloneBoolExpr(*w.condition()), cloneBlock(*w.body()), s.span());
    }
    case AstNodeKind::IfStatement: {
        const auto& i = static_cast<const AstIfStatement&>(s);
        return std::make_unique<AstIfStatement>(cloneBoolExpr(*i.condition()), cloneBlock(*i.body()), s.span());
    }
    case AstNodeKind::BlockStatement:
        return std::make_unique<AstBlockStatement>(cloneBlock(*static_cast<const AstBlockStatement&>(s).block()), s.span());
    default:
        return nullptr;
    }
}

std::unique_ptr<AstExpr> optimizeExpr(const AstExpr& e, Env& env) {
    if (const ConstValue c = evalConstExpr(e, env); c.isKnown()) {
        auto lit = constToExpr(c, e.span());
        if (lit) {
            return lit;
        }
    }

    switch (e.nodeKind()) {
    case AstNodeKind::AddExpr: {
        const auto& a = static_cast<const AstAddExpr&>(e);
        auto l = optimizeExpr(*a.left(), env);
        auto r = optimizeExpr(*a.right(), env);
        // local constant folding after recursion
        ConstValue lc = evalConstExpr(*l, env);
        ConstValue rc = evalConstExpr(*r, env);
        if (lc.kind == ConstValue::Kind::Int && rc.kind == ConstValue::Kind::Int) {
            return std::make_unique<AstLiteralInt>(std::to_string(lc.intValue + rc.intValue), e.span());
        }
        return std::make_unique<AstAddExpr>(std::move(l), std::move(r), e.span());
    }
    case AstNodeKind::BooleanExprWrapper: {
        const auto& w = static_cast<const AstBooleanExprWrapper&>(e);
        auto inner = optimizeBoolExpr(*w.inner(), env);
        if (inner->nodeKind() == AstNodeKind::BooleanLiteralExpr) {
            return std::make_unique<AstLiteralBool>(static_cast<AstBooleanLiteralExpr*>(inner.get())->value(), e.span());
        }
        return std::make_unique<AstBooleanExprWrapper>(std::move(inner), e.span());
    }
    case AstNodeKind::IdentifierExpr: {
        const auto& id = static_cast<const AstIdentifierExpr&>(e);
        auto out = std::make_unique<AstIdentifierExpr>(id.name(), e.span());
        if (id.hasResolvedDeclScope()) {
            out->setResolvedDeclScope(id.resolvedDeclScopeId());
        }
        return out;
    }
    default:
        return cloneExpr(e);
    }
}

std::unique_ptr<AstBooleanExpr> optimizeBoolExpr(const AstBooleanExpr& b, Env& env) {
    switch (b.nodeKind()) {
    case AstNodeKind::BooleanLiteralExpr:
        return cloneBoolExpr(b);
    case AstNodeKind::BinaryBoolExpr: {
        const auto& cmp = static_cast<const AstBinaryBoolExpr&>(b);
        auto l = optimizeExpr(*cmp.left(), env);
        auto r = optimizeExpr(*cmp.right(), env);
        const ConstValue lc = evalConstExpr(*l, env);
        const ConstValue rc = evalConstExpr(*r, env);
        if (lc.kind != ConstValue::Kind::Unknown && lc.kind == rc.kind) {
            bool eq = false;
            if (lc.kind == ConstValue::Kind::Int) {
                eq = lc.intValue == rc.intValue;
            } else if (lc.kind == ConstValue::Kind::Bool) {
                eq = lc.boolValue == rc.boolValue;
            } else if (lc.kind == ConstValue::Kind::String) {
                eq = lc.stringValue == rc.stringValue;
            }
            const bool value = (cmp.op() == AstBinaryBoolExpr::Op::Equal) ? eq : !eq;
            return std::make_unique<AstBooleanLiteralExpr>(value, b.span());
        }
        return std::make_unique<AstBinaryBoolExpr>(cmp.op(), std::move(l), std::move(r), b.span());
    }
    default:
        return cloneBoolExpr(b);
    }
}

void collectAssignedVars(const AstStatement& st, std::unordered_set<char>& out);

void collectAssignedVarsInBlock(const AstBlock& b, std::unordered_set<char>& out) {
    if (!b.statements()) {
        return;
    }
    for (const auto& st : b.statements()->statements()) {
        if (st) {
            collectAssignedVars(*st, out);
        }
    }
}

void collectAssignedVars(const AstStatement& st, std::unordered_set<char>& out) {
    switch (st.nodeKind()) {
    case AstNodeKind::AssignStatement:
        out.insert(static_cast<const AstAssignStatement&>(st).name());
        return;
    case AstNodeKind::WhileStatement:
        collectAssignedVarsInBlock(*static_cast<const AstWhileStatement&>(st).body(), out);
        return;
    case AstNodeKind::IfStatement:
        collectAssignedVarsInBlock(*static_cast<const AstIfStatement&>(st).body(), out);
        return;
    case AstNodeKind::BlockStatement:
        collectAssignedVarsInBlock(*static_cast<const AstBlockStatement&>(st).block(), out);
        return;
    default:
        return;
    }
}

std::unique_ptr<AstBlock> optimizeBlock(const AstBlock& b, Env& env, bool verbose);

std::optional<int> incrementStepIfCanonical(const AstAssignStatement& asg, char loopVar) {
    const AstExpr* ex = asg.expr();
    if (!ex || ex->nodeKind() != AstNodeKind::AddExpr) {
        return std::nullopt;
    }
    const auto& add = static_cast<const AstAddExpr&>(*ex);
    const AstExpr* l = add.left();
    const AstExpr* r = add.right();
    auto lit = [](const AstExpr* e) -> std::optional<int> {
        if (!e || e->nodeKind() != AstNodeKind::LiteralInt) {
            return std::nullopt;
        }
        return parseIntLiteral(static_cast<const AstLiteralInt&>(*e));
    };
    auto idIsVar = [loopVar](const AstExpr* e) {
        return e && e->nodeKind() == AstNodeKind::IdentifierExpr &&
               static_cast<const AstIdentifierExpr*>(e)->name() == loopVar;
    };
    if (idIsVar(l)) {
        return lit(r);
    }
    if (idIsVar(r)) {
        return lit(l);
    }
    return std::nullopt;
}

bool tryUnrollWhile(
    const AstWhileStatement& w,
    Env& env,
    bool verbose,
    std::vector<std::unique_ptr<AstStatement>>& outStmts
) {
    const auto* cond = dynamic_cast<const AstBinaryBoolExpr*>(w.condition());
    if (!cond || cond->op() != AstBinaryBoolExpr::Op::NotEqual) {
        return false;
    }

    const AstExpr* left = cond->left();
    const AstExpr* right = cond->right();
    if (!left || !right || left->nodeKind() != AstNodeKind::IdentifierExpr || right->nodeKind() != AstNodeKind::LiteralInt) {
        return false;
    }

    const char loopVar = static_cast<const AstIdentifierExpr*>(left)->name();
    const auto limitOpt = parseIntLiteral(static_cast<const AstLiteralInt&>(*right));
    const auto curValOpt = env.lookup(loopVar);
    if (!limitOpt.has_value() || !curValOpt.has_value() || curValOpt->kind != ConstValue::Kind::Int) {
        return false;
    }

    if (!w.body() || !w.body()->statements() || w.body()->statements()->statements().empty()) {
        return false;
    }
    const auto& bodyStmts = w.body()->statements()->statements();
    const AstStatement* last = bodyStmts.back().get();
    if (!last || last->nodeKind() != AstNodeKind::AssignStatement) {
        return false;
    }
    const auto& incAsg = static_cast<const AstAssignStatement&>(*last);
    if (incAsg.name() != loopVar) {
        return false;
    }
    const auto stepOpt = incrementStepIfCanonical(incAsg, loopVar);
    if (!stepOpt.has_value() || *stepOpt <= 0) {
        return false;
    }

    std::unordered_set<char> assigned;
    collectAssignedVarsInBlock(*w.body(), assigned);
    // only one assignment to loop var in loop body for safe deterministic unroll
    int loopVarAssignCount = 0;
    for (const auto& st : bodyStmts) {
        if (st && st->nodeKind() == AstNodeKind::AssignStatement &&
            static_cast<const AstAssignStatement&>(*st).name() == loopVar) {
            ++loopVarAssignCount;
        }
    }
    if (loopVarAssignCount != 1) {
        return false;
    }

    // Conservative safety: only unroll straight-line loop bodies (no nested control flow).
    for (const auto& st : bodyStmts) {
        if (!st) {
            continue;
        }
        if (st->nodeKind() == AstNodeKind::WhileStatement || st->nodeKind() == AstNodeKind::IfStatement ||
            st->nodeKind() == AstNodeKind::BlockStatement) {
            return false;
        }
    }

    const int cur = curValOpt->intValue;
    const int limit = *limitOpt;
    const int step = *stepOpt;
    if (cur > limit || (limit - cur) < 0 || ((limit - cur) % step) != 0) {
        return false;
    }
    const int iters = (limit - cur) / step;
    if (iters < 0 || iters > 16) {
        return false;
    }

    if (verbose) {
        std::cout << "[semantic] optimize: unrolled while-loop into " << iters << " iteration block(s)\n";
    }

    for (int i = 0; i < iters; ++i) {
        auto body = optimizeBlock(*w.body(), env, verbose);
        outStmts.push_back(std::make_unique<AstBlockStatement>(std::move(body), w.span()));
    }
    return true;
}

std::vector<std::unique_ptr<AstStatement>> optimizeStatement(const AstStatement& st, Env& env, bool verbose) {
    std::vector<std::unique_ptr<AstStatement>> out;
    switch (st.nodeKind()) {
    case AstNodeKind::VarDeclStatement: {
        const auto& d = static_cast<const AstVarDeclStatement&>(st);
        env.declareVar(d.name());
        out.push_back(std::make_unique<AstVarDeclStatement>(d.typeName(), d.name(), st.span()));
        return out;
    }
    case AstNodeKind::AssignStatement: {
        const auto& a = static_cast<const AstAssignStatement&>(st);
        auto expr = optimizeExpr(*a.expr(), env);
        const ConstValue v = evalConstExpr(*expr, env);
        env.assignVar(a.name(), v.isKnown() ? v : ConstValue::unknown());
        auto outAsg = std::make_unique<AstAssignStatement>(a.name(), std::move(expr), st.span());
        if (a.hasLhsResolvedDeclScope()) {
            outAsg->setLhsResolvedDeclScope(a.lhsResolvedDeclScopeId());
        }
        out.push_back(std::move(outAsg));
        return out;
    }
    case AstNodeKind::PrintStatement: {
        const auto& p = static_cast<const AstPrintStatement&>(st);
        out.push_back(std::make_unique<AstPrintStatement>(optimizeExpr(*p.expr(), env), st.span()));
        return out;
    }
    case AstNodeKind::BlockStatement: {
        const auto& bs = static_cast<const AstBlockStatement&>(st);
        out.push_back(std::make_unique<AstBlockStatement>(optimizeBlock(*bs.block(), env, verbose), st.span()));
        return out;
    }
    case AstNodeKind::IfStatement: {
        const auto& i = static_cast<const AstIfStatement&>(st);
        auto cond = optimizeBoolExpr(*i.condition(), env);
        if (cond->nodeKind() == AstNodeKind::BooleanLiteralExpr) {
            const bool v = static_cast<AstBooleanLiteralExpr*>(cond.get())->value();
            if (!v) {
                if (verbose) {
                    std::cout << "[semantic] optimize: removed dead `if (false)` block\n";
                }
                return out;
            }
            auto body = optimizeBlock(*i.body(), env, verbose);
            out.push_back(std::make_unique<AstBlockStatement>(std::move(body), st.span()));
            return out;
        }

        Env branchEnv = env;
        auto body = optimizeBlock(*i.body(), branchEnv, verbose);
        std::unordered_set<char> assigned;
        collectAssignedVarsInBlock(*i.body(), assigned);
        for (char c : assigned) {
            env.invalidateVar(c);
        }
        out.push_back(std::make_unique<AstIfStatement>(std::move(cond), std::move(body), st.span()));
        return out;
    }
    case AstNodeKind::WhileStatement: {
        const auto& w = static_cast<const AstWhileStatement&>(st);
        std::unordered_set<char> assigned;
        collectAssignedVarsInBlock(*w.body(), assigned);

        // Do not fold while predicates using variables mutated in the loop body.
        Env condEnv = env;
        for (char c : assigned) {
            condEnv.invalidateVar(c);
        }
        auto cond = optimizeBoolExpr(*w.condition(), condEnv);
        if (cond->nodeKind() == AstNodeKind::BooleanLiteralExpr &&
            !static_cast<AstBooleanLiteralExpr*>(cond.get())->value()) {
            if (verbose) {
                std::cout << "[semantic] optimize: removed dead `while (false)` loop\n";
            }
            return out;
        }

        if (tryUnrollWhile(w, env, verbose, out)) {
            return out;
        }

        // Inside loop body, values of loop-mutated vars are not stable constants across iterations.
        Env bodyEnv = env;
        for (char c : assigned) {
            bodyEnv.invalidateVar(c);
        }
        auto body = optimizeBlock(*w.body(), bodyEnv, verbose);
        for (char c : assigned) {
            env.invalidateVar(c);
        }
        out.push_back(std::make_unique<AstWhileStatement>(std::move(cond), std::move(body), st.span()));
        return out;
    }
    default:
        out.push_back(cloneStatement(st));
        return out;
    }
}

std::unique_ptr<AstBlock> optimizeBlock(const AstBlock& b, Env& env, bool verbose) {
    env.pushScope();
    auto outList = std::make_unique<AstStatementList>();
    if (b.statements()) {
        for (const auto& st : b.statements()->statements()) {
            if (!st) {
                continue;
            }
            auto rewritten = optimizeStatement(*st, env, verbose);
            for (auto& s : rewritten) {
                outList->addStatement(std::move(s));
            }
        }
        outList->setSpan(b.statements()->span());
    }
    env.popScope();
    return std::make_unique<AstBlock>(std::move(outList), b.span());
}

} // namespace

void optimizeAstProgram(AstProgram& ast, bool verbose) {
    if (!ast.block()) {
        return;
    }
    Env env;
    auto newBlock = optimizeBlock(*ast.block(), env, verbose);
    auto rewritten = std::make_unique<AstProgram>(std::move(newBlock), ast.span());
    ast = std::move(*rewritten);
}

} // namespace pavic
