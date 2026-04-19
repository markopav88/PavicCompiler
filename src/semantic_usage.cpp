#include "semantic_usage.hpp"

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

struct VarSlot {
    SourceLocation declLoc{};
    bool read = false;
    bool assigned = false;
};

SourceLocation locationAtSpan(const SourceMap& map, const std::vector<Token>& tokens, AstSpan span) {
    std::size_t byteOffset = 0;
    if (!tokens.empty() && span.begin < tokens.size()) {
        byteOffset = tokens[span.begin].offset;
    }
    return map.locationAt(byteOffset);
}

void usageTrace(bool verbose, const SourceMap& map, const std::vector<Token>& tokens, AstSpan span, const std::string& message) {
    if (!verbose) {
        return;
    }
    const SourceLocation loc = locationAtSpan(map, tokens, span);
    std::cout << "[semantic] usage: " << loc.line << ":" << loc.column << ": " << message << "\n";
}

class UsageChecker {
public:
    UsageChecker(const SourceMap& map, const std::vector<Token>& tokens, DiagnosticBag& diagnostics, bool verbose)
        : map_(map), tokens_(tokens), diagnostics_(diagnostics), verbose_(verbose) {}

    void checkProgram(const AstProgram& program) {
        usageTrace(verbose_, map_, tokens_, program.span(), "usage hints: Program (enter)");
        visitBlock(*program.block());
        usageTrace(verbose_, map_, tokens_, program.span(), "usage hints: Program (leave)");
    }

private:
    void pushScope() { scopes_.emplace_back(); }

    void popScope(AstSpan blockSpan) {
        if (scopes_.empty()) {
            return;
        }
        for (const auto& entry : scopes_.back()) {
            const char name = entry.first;
            const VarSlot& slot = entry.second;
            if (!slot.read && !slot.assigned) {
                std::ostringstream msg;
                msg << "variable `" << name << "` is declared but never used";
                diagnostics_.addHint(
                    msg.str(),
                    slot.declLoc,
                    "Remove the declaration, or use the variable in an expression, assignment, or `print`."
                );
            } else if (!slot.read && slot.assigned) {
                std::ostringstream msg;
                msg << "variable `" << name << "` is assigned but its value is never read";
                diagnostics_.addWarning(
                    msg.str(),
                    slot.declLoc,
                    "The assignment has side effect only on this name; consider using the value in an expression or removing dead stores."
                );
            }
        }
        scopes_.pop_back();
        usageTrace(verbose_, map_, tokens_, blockSpan, "usage hints: left Block (diagnostics for locals emitted)");
    }

    void declareVar(char name, SourceLocation declLoc) {
        if (scopes_.empty()) {
            pushScope();
        }
        VarSlot slot;
        slot.declLoc = declLoc;
        scopes_.back()[name] = slot;
    }

    VarSlot* lookupMutable(char name) {
        for (std::size_t i = scopes_.size(); i > 0; --i) {
            std::map<char, VarSlot>& m = scopes_[i - 1];
            const auto found = m.find(name);
            if (found != m.end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    void markRead(char name, AstSpan useSpan) {
        VarSlot* slot = lookupMutable(name);
        if (!slot) {
            return;
        }
        if (!slot->assigned) {
            std::ostringstream msg;
            msg << "use of uninitialized variable `" << name << "`";
            diagnostics_.addWarning(
                msg.str(),
                locationAtSpan(map_, tokens_, useSpan),
                "Assign a value before reading the variable, or initialize it at declaration if your language allows that."
            );
        }
        slot->read = true;
        if (verbose_) {
            std::ostringstream oss;
            oss << "read of `" << name << "`";
            usageTrace(verbose_, map_, tokens_, useSpan, oss.str());
        }
    }

    void markAssigned(char name, AstSpan assignSpan) {
        VarSlot* slot = lookupMutable(name);
        if (slot) {
            slot->assigned = true;
            if (verbose_) {
                std::ostringstream oss;
                oss << "assignment to `" << name << "` (marks initialized)";
                usageTrace(verbose_, map_, tokens_, assignSpan, oss.str());
            }
        }
    }

    void visitBlock(const AstBlock& block) {
        pushScope();
        usageTrace(verbose_, map_, tokens_, block.span(), "enter Block (usage tracking)");
        if (block.statements()) {
            for (const auto& st : block.statements()->statements()) {
                if (st) {
                    visitStatement(*st);
                }
            }
        }
        popScope(block.span());
    }

    void visitStatement(const AstStatement& st) {
        switch (st.nodeKind()) {
        case AstNodeKind::PrintStatement:
            visitExpr(*static_cast<const AstPrintStatement&>(st).expr());
            return;
        case AstNodeKind::AssignStatement: {
            const auto& a = static_cast<const AstAssignStatement&>(st);
            visitExpr(*a.expr());
            markAssigned(a.name(), a.span());
            return;
        }
        case AstNodeKind::VarDeclStatement: {
            const auto& v = static_cast<const AstVarDeclStatement&>(st);
            declareVar(v.name(), locationAtSpan(map_, tokens_, v.span()));
            if (verbose_) {
                std::ostringstream oss;
                oss << "record declaration of `" << v.name() << "` for usage tracking";
                usageTrace(verbose_, map_, tokens_, v.span(), oss.str());
            }
            return;
        }
        case AstNodeKind::WhileStatement: {
            const auto& w = static_cast<const AstWhileStatement&>(st);
            visitBooleanExpr(*w.condition());
            visitBlock(*w.body());
            return;
        }
        case AstNodeKind::IfStatement: {
            const auto& i = static_cast<const AstIfStatement&>(st);
            visitBooleanExpr(*i.condition());
            visitBlock(*i.body());
            return;
        }
        case AstNodeKind::BlockStatement:
            visitBlock(*static_cast<const AstBlockStatement&>(st).block());
            return;
        default:
            break;
        }
        throw std::logic_error("visitStatement: unexpected AST statement kind (usage checker)");
    }

    void visitExpr(const AstExpr& e) {
        switch (e.nodeKind()) {
        case AstNodeKind::LiteralInt:
        case AstNodeKind::LiteralString:
        case AstNodeKind::LiteralBool:
            return;
        case AstNodeKind::IdentifierExpr: {
            const auto& id = static_cast<const AstIdentifierExpr&>(e);
            markRead(id.name(), e.span());
            return;
        }
        case AstNodeKind::AddExpr: {
            const auto& a = static_cast<const AstAddExpr&>(e);
            visitExpr(*a.left());
            visitExpr(*a.right());
            return;
        }
        case AstNodeKind::BooleanExprWrapper:
            visitBooleanExpr(*static_cast<const AstBooleanExprWrapper&>(e).inner());
            return;
        default:
            break;
        }
        throw std::logic_error("visitExpr: unexpected AST expression kind (usage checker)");
    }

    void visitBooleanExpr(const AstBooleanExpr& b) {
        switch (b.nodeKind()) {
        case AstNodeKind::BinaryBoolExpr: {
            const auto& c = static_cast<const AstBinaryBoolExpr&>(b);
            visitExpr(*c.left());
            visitExpr(*c.right());
            return;
        }
        case AstNodeKind::BooleanLiteralExpr:
            return;
        default:
            break;
        }
        throw std::logic_error("visitBooleanExpr: unexpected AST boolean kind (usage checker)");
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    bool verbose_;
    std::vector<std::map<char, VarSlot>> scopes_;
};

} // namespace

void runUsageAndInitHints(
    const AstProgram& ast,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    bool verbose
) {
    UsageChecker checker(map, tokens, diagnostics, verbose);
    checker.checkProgram(ast);
}

} // namespace pavic
