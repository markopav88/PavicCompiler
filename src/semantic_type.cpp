#include "semantic_type.hpp"

#include "ast.hpp"
#include "source.hpp"
#include "token.hpp"

#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pavic {

namespace {

enum class ValueType { Int, String, Bool, Invalid };

const char* valueTypeLabel(ValueType t) {
    switch (t) {
    case ValueType::Int:
        return "int";
    case ValueType::String:
        return "string";
    case ValueType::Bool:
        return "boolean";
    case ValueType::Invalid:
        return "<invalid>";
    }
    return "?";
}

ValueType valueTypeFromDeclName(const std::string& decl, bool& ok) {
    ok = true;
    if (decl == "int") {
        return ValueType::Int;
    }
    if (decl == "string") {
        return ValueType::String;
    }
    if (decl == "boolean") {
        return ValueType::Bool;
    }
    ok = false;
    return ValueType::Invalid;
}

SourceLocation locationAtSpan(const SourceMap& map, const std::vector<Token>& tokens, AstSpan span) {
    std::size_t byteOffset = 0;
    if (!tokens.empty() && span.begin < tokens.size()) {
        byteOffset = tokens[span.begin].offset;
    }
    return map.locationAt(byteOffset);
}

void typeTrace(bool verbose, const SourceMap& map, const std::vector<Token>& tokens, AstSpan span, const std::string& message) {
    if (!verbose) {
        return;
    }
    const SourceLocation loc = locationAtSpan(map, tokens, span);
    std::cout << "[semantic] types: " << loc.line << ":" << loc.column << ": " << message << "\n";
}

class TypeEnvStack {
public:
    void push() { scopes_.emplace_back(); }

    void pop() {
        if (!scopes_.empty()) {
            scopes_.pop_back();
        }
    }

    void declare(char name, ValueType t) {
        if (scopes_.empty()) {
            push();
        }
        scopes_.back()[name] = t;
    }

    ValueType lookup(char name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return ValueType::Invalid;
    }

private:
    std::vector<std::map<char, ValueType>> scopes_;
};

class TypeChecker {
public:
    TypeChecker(const SourceMap& map, const std::vector<Token>& tokens, DiagnosticBag& diagnostics, bool verbose)
        : map_(map), tokens_(tokens), diagnostics_(diagnostics), verbose_(verbose) {}

    void checkProgram(const AstProgram& program) {
        typeTrace(verbose_, map_, tokens_, program.span(), "type check: Program (enter)");
        visitBlock(*program.block());
        typeTrace(verbose_, map_, tokens_, program.span(), "type check: Program (leave)");
    }

private:
    void visitBlock(const AstBlock& block) {
        types_.push();
        typeTrace(verbose_, map_, tokens_, block.span(), "type check: enter Block");
        if (block.statements()) {
            for (const auto& st : block.statements()->statements()) {
                if (st) {
                    visitStatement(*st);
                }
            }
        }
        types_.pop();
        typeTrace(verbose_, map_, tokens_, block.span(), "type check: leave Block");
    }

    void visitStatement(const AstStatement& st) {
        switch (st.nodeKind()) {
        case AstNodeKind::PrintStatement: {
            const auto& p = static_cast<const AstPrintStatement&>(st);
            (void)inferExprType(*p.expr());
            return;
        }
        case AstNodeKind::AssignStatement: {
            const auto& a = static_cast<const AstAssignStatement&>(st);
            const ValueType lhs = types_.lookup(a.name());
            const ValueType rhs = inferExprType(*a.expr());
            if (lhs == ValueType::Invalid || rhs == ValueType::Invalid) {
                return;
            }
            if (lhs != rhs) {
                std::ostringstream msg;
                msg << "type mismatch in assignment to `" << a.name() << "`: variable is `" << valueTypeLabel(lhs)
                    << "` but the expression is `" << valueTypeLabel(rhs) << "`";
                std::ostringstream hint;
                hint << "Change the right-hand side to a `" << valueTypeLabel(lhs) << "` expression, or use a variable of the correct type.";
                diagnostics_.addError(msg.str(), locationAtSpan(map_, tokens_, st.span()), hint.str());
            }
            return;
        }
        case AstNodeKind::VarDeclStatement: {
            const auto& v = static_cast<const AstVarDeclStatement&>(st);
            bool declOk = false;
            const ValueType vt = valueTypeFromDeclName(v.typeName(), declOk);
            if (!declOk) {
                std::ostringstream msg;
                msg << "internal error: unknown declared type `" << v.typeName() << "`";
                diagnostics_.addError(msg.str(), locationAtSpan(map_, tokens_, st.span()), "The parser should only allow `int`, `string`, or `boolean`.");
                return;
            }
            types_.declare(v.name(), vt);
            typeTrace(verbose_, map_, tokens_, st.span(), std::string("record type for `") + v.name() + "` as " + valueTypeLabel(vt));
            return;
        }
        case AstNodeKind::WhileStatement: {
            const auto& w = static_cast<const AstWhileStatement&>(st);
            (void)inferBooleanExprType(*w.condition());
            visitBlock(*w.body());
            return;
        }
        case AstNodeKind::IfStatement: {
            const auto& i = static_cast<const AstIfStatement&>(st);
            (void)inferBooleanExprType(*i.condition());
            visitBlock(*i.body());
            return;
        }
        case AstNodeKind::BlockStatement:
            visitBlock(*static_cast<const AstBlockStatement&>(st).block());
            return;
        default:
            break;
        }
        throw std::logic_error("visitStatement: unexpected AST statement kind (type checker)");
    }

    ValueType inferExprType(const AstExpr& e) {
        const AstSpan sp = e.span();
        switch (e.nodeKind()) {
        case AstNodeKind::LiteralInt:
            typeTrace(verbose_, map_, tokens_, sp, "expression type: int literal");
            return ValueType::Int;
        case AstNodeKind::LiteralString:
            typeTrace(verbose_, map_, tokens_, sp, "expression type: string literal");
            return ValueType::String;
        case AstNodeKind::LiteralBool:
            typeTrace(verbose_, map_, tokens_, sp, "expression type: boolean literal");
            return ValueType::Bool;
        case AstNodeKind::IdentifierExpr: {
            const char name = static_cast<const AstIdentifierExpr&>(e).name();
            const ValueType t = types_.lookup(name);
            if (verbose_) {
                std::ostringstream oss;
                oss << "expression type: identifier `" << name << "` → " << valueTypeLabel(t);
                typeTrace(verbose_, map_, tokens_, sp, oss.str());
            }
            if (t == ValueType::Invalid) {
                std::ostringstream msg;
                msg << "internal error: no type for `" << name << "` (scope check should have caught this)";
                diagnostics_.addError(msg.str(), locationAtSpan(map_, tokens_, sp), "");
            }
            return t;
        }
        case AstNodeKind::AddExpr: {
            const auto& a = static_cast<const AstAddExpr&>(e);
            const ValueType L = inferExprType(*a.left());
            const ValueType R = inferExprType(*a.right());
            if (L == ValueType::Invalid || R == ValueType::Invalid) {
                return ValueType::Invalid;
            }
            if (L != ValueType::Int || R != ValueType::Int) {
                std::ostringstream msg;
                msg << "operator `+` requires `int` on both sides; got `" << valueTypeLabel(L) << "` and `" << valueTypeLabel(R) << "`";
                diagnostics_.addError(
                    msg.str(),
                    locationAtSpan(map_, tokens_, sp),
                    "Use integer literals and `int` variables, or remove the `+` chain."
                );
                return ValueType::Invalid;
            }
            typeTrace(verbose_, map_, tokens_, sp, "expression type: int + int → int");
            return ValueType::Int;
        }
        case AstNodeKind::BooleanExprWrapper: {
            const ValueType t = inferBooleanExprType(*static_cast<const AstBooleanExprWrapper&>(e).inner());
            typeTrace(verbose_, map_, tokens_, sp, "expression type: boolean sub-expression");
            return t;
        }
        default:
            break;
        }
        throw std::logic_error("inferExprType: unexpected AST expression kind");
    }

    ValueType inferBooleanExprType(const AstBooleanExpr& b) {
        const AstSpan sp = b.span();
        switch (b.nodeKind()) {
        case AstNodeKind::BooleanLiteralExpr:
            return ValueType::Bool;
        case AstNodeKind::BinaryBoolExpr: {
            const auto& c = static_cast<const AstBinaryBoolExpr&>(b);
            const ValueType L = inferExprType(*c.left());
            const ValueType R = inferExprType(*c.right());
            if (L == ValueType::Invalid || R == ValueType::Invalid) {
                return ValueType::Invalid;
            }
            if (L != R) {
                const char* opStr = (c.op() == AstBinaryBoolExpr::Op::Equal) ? "==" : "!=";
                std::ostringstream msg;
                msg << "operands of `" << opStr << "` must have the same type; got `" << valueTypeLabel(L) << "` and `" << valueTypeLabel(R) << "`";
                diagnostics_.addError(
                    msg.str(),
                    locationAtSpan(map_, tokens_, sp),
                    "Rewrite the comparison so both sides are `int`, both `string`, or both `boolean`."
                );
                return ValueType::Invalid;
            }
            typeTrace(verbose_, map_, tokens_, sp, "boolean compare: same-type operands → boolean");
            return ValueType::Bool;
        }
        default:
            break;
        }
        throw std::logic_error("inferBooleanExprType: unexpected AST boolean kind");
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    bool verbose_;
    TypeEnvStack types_;
};

} // namespace

bool runTypeCheck(
    const AstProgram& ast,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    bool verbose
) {
    const std::size_t errorsBefore = diagnostics.errorCount();
    TypeChecker checker(map, tokens, diagnostics, verbose);
    checker.checkProgram(ast);
    return diagnostics.errorCount() == errorsBefore;
}

} // namespace pavic
