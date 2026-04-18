#include "ast_lower.hpp"

#include "ast.hpp"
#include "cst.hpp"
#include "source.hpp"
#include "token.hpp"

#include <iostream>
#include <stdexcept>

namespace pavic {

namespace {

AstSpan astSpanFrom(CstSpan span) {
    return AstSpan{span.begin, span.end};
}

void semanticTrace(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    CstSpan span,
    const char* message
) {
    if (!verbose) {
        return;
    }
    std::size_t byteOffset = 0;
    if (!tokens.empty() && span.begin < tokens.size()) {
        byteOffset = tokens[span.begin].offset;
    }
    const SourceLocation loc = map.locationAt(byteOffset);
    std::cout << "[semantic] " << loc.line << ":" << loc.column << ": " << message << "\n";
}

std::unique_ptr<AstBooleanExpr> lowerBooleanExpr(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstBooleanExpr& node
);

std::unique_ptr<AstExpr> lowerExpr(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstExpr& node
) {
    const CstSpan sp = node.span();
    switch (node.nodeKind()) {
    case CstNodeKind::LiteralInt: {
        const auto& n = static_cast<const CstLiteralInt&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: LiteralInt");
        return std::make_unique<AstLiteralInt>(n.lexeme(), astSpanFrom(sp));
    }
    case CstNodeKind::LiteralString: {
        const auto& n = static_cast<const CstLiteralString&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: LiteralString");
        return std::make_unique<AstLiteralString>(n.lexeme(), astSpanFrom(sp));
    }
    case CstNodeKind::LiteralBool: {
        const auto& n = static_cast<const CstLiteralBool&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: LiteralBool");
        return std::make_unique<AstLiteralBool>(n.value(), astSpanFrom(sp));
    }
    case CstNodeKind::IdentifierExpr: {
        const auto& n = static_cast<const CstIdentifierExpr&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: IdentifierExpr");
        return std::make_unique<AstIdentifierExpr>(n.name(), astSpanFrom(sp));
    }
    case CstNodeKind::AddExpr: {
        const auto& n = static_cast<const CstAddExpr&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: AddExpr");
        std::unique_ptr<AstExpr> left = lowerExpr(verbose, map, tokens, *n.left());
        std::unique_ptr<AstExpr> right = lowerExpr(verbose, map, tokens, *n.right());
        return std::make_unique<AstAddExpr>(std::move(left), std::move(right), astSpanFrom(sp));
    }
    case CstNodeKind::Expr: {
        const auto& n = static_cast<const CstBooleanExprWrapper&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: BooleanExprWrapper (Expr position)");
        std::unique_ptr<AstBooleanExpr> inner = lowerBooleanExpr(verbose, map, tokens, *n.inner());
        return std::make_unique<AstBooleanExprWrapper>(std::move(inner), astSpanFrom(sp));
    }
    default:
        break;
    }
    throw std::logic_error("lowerExpr: unexpected CST expression node kind");
}

std::unique_ptr<AstBooleanExpr> lowerBooleanExpr(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstBooleanExpr& node
) {
    const CstSpan sp = node.span();
    switch (node.nodeKind()) {
    case CstNodeKind::BinaryBoolExpr: {
        const auto& n = static_cast<const CstBinaryBoolExpr&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: BinaryBoolExpr");
        AstBinaryBoolExpr::Op op = AstBinaryBoolExpr::Op::Equal;
        switch (n.op()) {
        case CstBinaryBoolExpr::Op::Equal:
            op = AstBinaryBoolExpr::Op::Equal;
            break;
        case CstBinaryBoolExpr::Op::NotEqual:
            op = AstBinaryBoolExpr::Op::NotEqual;
            break;
        }
        std::unique_ptr<AstExpr> left = lowerExpr(verbose, map, tokens, *n.left());
        std::unique_ptr<AstExpr> right = lowerExpr(verbose, map, tokens, *n.right());
        return std::make_unique<AstBinaryBoolExpr>(op, std::move(left), std::move(right), astSpanFrom(sp));
    }
    case CstNodeKind::BooleanLiteral: {
        const auto& n = static_cast<const CstBooleanLiteralExpr&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: BooleanLiteralExpr");
        return std::make_unique<AstBooleanLiteralExpr>(n.value(), astSpanFrom(sp));
    }
    default:
        break;
    }
    throw std::logic_error("lowerBooleanExpr: unexpected CST boolean node kind");
}

std::unique_ptr<AstStatement> lowerStatement(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstStatement& node
);

std::unique_ptr<AstStatementList> lowerStatementList(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstStatementList& list
) {
    const CstSpan sp = list.span();
    semanticTrace(verbose, map, tokens, sp, "AST: StatementList (lowering children)");
    auto out = std::make_unique<AstStatementList>();
    for (const auto& st : list.statements()) {
        if (st) {
            out->addStatement(lowerStatement(verbose, map, tokens, *st));
        }
    }
    out->setSpan(astSpanFrom(sp));
    return out;
}

std::unique_ptr<AstBlock> lowerBlock(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstBlock& block
) {
    const CstSpan sp = block.span();
    semanticTrace(verbose, map, tokens, sp, "AST: Block");
    std::unique_ptr<AstStatementList> stmts = lowerStatementList(verbose, map, tokens, *block.statements());
    return std::make_unique<AstBlock>(std::move(stmts), astSpanFrom(sp));
}

std::unique_ptr<AstStatement> lowerStatement(
    bool verbose,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstStatement& node
) {
    const CstSpan sp = node.span();
    switch (node.nodeKind()) {
    case CstNodeKind::PrintStatement: {
        const auto& n = static_cast<const CstPrintStatement&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: PrintStatement");
        std::unique_ptr<AstExpr> e = lowerExpr(verbose, map, tokens, *n.expr());
        return std::make_unique<AstPrintStatement>(std::move(e), astSpanFrom(sp));
    }
    case CstNodeKind::AssignStatement: {
        const auto& n = static_cast<const CstAssignStatement&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: AssignStatement");
        std::unique_ptr<AstExpr> e = lowerExpr(verbose, map, tokens, *n.expr());
        return std::make_unique<AstAssignStatement>(n.name(), std::move(e), astSpanFrom(sp));
    }
    case CstNodeKind::VarDeclStatement: {
        const auto& n = static_cast<const CstVarDeclStatement&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: VarDeclStatement");
        return std::make_unique<AstVarDeclStatement>(n.typeName(), n.name(), astSpanFrom(sp));
    }
    case CstNodeKind::WhileStatement: {
        const auto& n = static_cast<const CstWhileStatement&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: WhileStatement");
        std::unique_ptr<AstBooleanExpr> cond = lowerBooleanExpr(verbose, map, tokens, *n.condition());
        std::unique_ptr<AstBlock> body = lowerBlock(verbose, map, tokens, *n.body());
        return std::make_unique<AstWhileStatement>(std::move(cond), std::move(body), astSpanFrom(sp));
    }
    case CstNodeKind::IfStatement: {
        const auto& n = static_cast<const CstIfStatement&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: IfStatement");
        std::unique_ptr<AstBooleanExpr> cond = lowerBooleanExpr(verbose, map, tokens, *n.condition());
        std::unique_ptr<AstBlock> body = lowerBlock(verbose, map, tokens, *n.body());
        return std::make_unique<AstIfStatement>(std::move(cond), std::move(body), astSpanFrom(sp));
    }
    case CstNodeKind::BlockStatement: {
        const auto& n = static_cast<const CstBlockStatement&>(node);
        semanticTrace(verbose, map, tokens, sp, "AST: BlockStatement");
        std::unique_ptr<AstBlock> inner = lowerBlock(verbose, map, tokens, *n.block());
        return std::make_unique<AstBlockStatement>(std::move(inner), astSpanFrom(sp));
    }
    default:
        break;
    }
    throw std::logic_error("lowerStatement: unexpected CST statement node kind");
}

} // namespace

std::unique_ptr<AstProgram> lowerCstToAst(
    const SourceMap& map,
    const std::vector<Token>& tokens,
    const CstProgram& cst,
    bool verbose
) {
    const CstSpan sp = cst.span();
    semanticTrace(verbose, map, tokens, sp, "AST lowering: Program (enter)");
    std::unique_ptr<AstBlock> block = lowerBlock(verbose, map, tokens, *cst.block());
    semanticTrace(verbose, map, tokens, sp, "AST lowering: Program (leave)");
    return std::make_unique<AstProgram>(std::move(block), astSpanFrom(sp));
}

} // namespace pavic
