#include "semantic_scope.hpp"

#include "ast.hpp"
#include "source.hpp"
#include "token.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace pavic {

namespace {

SourceLocation locationAtSpan(const SourceMap& map, const std::vector<Token>& tokens, AstSpan span) {
    std::size_t byteOffset = 0;
    if (!tokens.empty() && span.begin < tokens.size()) {
        byteOffset = tokens[span.begin].offset;
    }
    return map.locationAt(byteOffset);
}

void semanticTrace(bool verbose, const SourceMap& map, const std::vector<Token>& tokens, AstSpan span, const std::string& message) {
    if (!verbose) {
        return;
    }
    const SourceLocation loc = locationAtSpan(map, tokens, span);
    std::cout << "[semantic] scope: " << loc.line << ":" << loc.column << ": " << message << "\n";
}

class ScopeChecker {
public:
    ScopeChecker(
        const SourceMap& map,
        const std::vector<Token>& tokens,
        DiagnosticBag& diagnostics,
        bool verbose
    )
        : map_(map), tokens_(tokens), diagnostics_(diagnostics), verbose_(verbose) {}

    void checkProgram(const AstProgram& program) {
        semanticTrace(verbose_, map_, tokens_, program.span(), "enter Program (root Block is one scope)");
        visitBlock(*program.block());
        semanticTrace(verbose_, map_, tokens_, program.span(), "leave Program");
    }

    void takeSymbols(std::vector<SymbolRecord>& out) { out = scopes_.orderedDeclarations(); }

private:
    void visitBlock(const AstBlock& block) {
        semanticTrace(verbose_, map_, tokens_, block.span(), "enter Block (push scope)");
        scopes_.pushScope();
        if (block.statements()) {
            for (const auto& st : block.statements()->statements()) {
                if (st) {
                    visitStatement(*st);
                }
            }
        }
        scopes_.popScope();
        semanticTrace(verbose_, map_, tokens_, block.span(), "leave Block (pop scope)");
    }

    void visitStatement(const AstStatement& st) {
        switch (st.nodeKind()) {
        case AstNodeKind::PrintStatement:
            visitExpr(*static_cast<const AstPrintStatement&>(st).expr());
            return;
        case AstNodeKind::AssignStatement:
            visitAssign(static_cast<const AstAssignStatement&>(st));
            return;
        case AstNodeKind::VarDeclStatement:
            visitVarDecl(static_cast<const AstVarDeclStatement&>(st));
            return;
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
        throw std::logic_error("visitStatement: unexpected AST statement kind");
    }

    void visitAssign(const AstAssignStatement& st) {
        const char lhs = st.name();
        const SourceLocation lhsLoc = locationAtSpan(map_, tokens_, st.span());
        const SymbolRecord* sym = scopes_.lookup(lhs);
        if (verbose_) {
            std::ostringstream oss;
            oss << "lookup `" << lhs << "` for assignment: " << (sym ? "found" : "not found");
            semanticTrace(verbose_, map_, tokens_, st.span(), oss.str());
        }
        if (!sym) {
            std::ostringstream msg;
            msg << "assignment to undeclared identifier `" << lhs << "`";
            std::ostringstream hint;
            hint << "Declare the variable with a type before assigning, e.g. `int " << lhs << "` or `string " << lhs
                 << "` in this or an enclosing block.";
            diagnostics_.addError(msg.str(), lhsLoc, hint.str());
        }
        visitExpr(*st.expr());
    }

    void visitVarDecl(const AstVarDeclStatement& st) {
        const char name = st.name();
        const SourceLocation loc = locationAtSpan(map_, tokens_, st.span());
        if (verbose_) {
            std::ostringstream oss;
            oss << "declare `" << name << "` as " << st.typeName();
            semanticTrace(verbose_, map_, tokens_, st.span(), oss.str());
        }
        if (!scopes_.tryDeclare(name, st.typeName(), loc)) {
            std::ostringstream msg;
            msg << "redeclaration of `" << name << "` in the same scope";
            std::ostringstream hint;
            hint << "Choose a different single-letter name, or remove the duplicate `"
                 << st.typeName() << " " << name << "` in this block.";
            diagnostics_.addError(msg.str(), loc, hint.str());
        }
    }

    void visitExpr(const AstExpr& e) {
        const AstSpan sp = e.span();
        switch (e.nodeKind()) {
        case AstNodeKind::LiteralInt:
        case AstNodeKind::LiteralString:
        case AstNodeKind::LiteralBool:
            return;
        case AstNodeKind::IdentifierExpr: {
            const char name = static_cast<const AstIdentifierExpr&>(e).name();
            const SymbolRecord* sym = scopes_.lookup(name);
            if (verbose_) {
                std::ostringstream oss;
                oss << "lookup `" << name << "` in expression: " << (sym ? "found" : "not found");
                semanticTrace(verbose_, map_, tokens_, sp, oss.str());
            }
            if (!sym) {
                std::ostringstream msg;
                msg << "use of undeclared identifier `" << name << "`";
                std::ostringstream hint;
                hint << "Declare `" << name << "` with a type keyword before use, or fix the spelling.";
                diagnostics_.addError(msg.str(), locationAtSpan(map_, tokens_, sp), hint.str());
            }
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
        throw std::logic_error("visitExpr: unexpected AST expression kind");
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
        throw std::logic_error("visitBooleanExpr: unexpected AST boolean kind");
    }

    const SourceMap& map_;
    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    bool verbose_;
    ScopeStack scopes_;
};

} // namespace

bool runScopeCheck(
    const AstProgram& ast,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    bool verbose,
    std::vector<SymbolRecord>& outSymbols
) {
    outSymbols.clear();
    const std::size_t errorsBefore = diagnostics.errorCount();
    ScopeChecker checker(map, tokens, diagnostics, verbose);
    checker.checkProgram(ast);
    checker.takeSymbols(outSymbols);
    return diagnostics.errorCount() == errorsBefore;
}

} // namespace pavic
