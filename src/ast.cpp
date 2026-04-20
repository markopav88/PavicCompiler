#include "ast.hpp"

namespace pavic {

namespace {

const char* opName(AstBinaryBoolExpr::Op op) {
    switch (op) {
    case AstBinaryBoolExpr::Op::Equal:
        return "==";
    case AstBinaryBoolExpr::Op::NotEqual:
        return "!=";
    }
    return "?";
}

} // namespace

AstNode::AstNode(AstNodeKind kind, AstSpan span) : kind_(kind), span_(span) {}

void AstNode::writeIndent(std::ostream& os, int depth) {
    for (int i = 0; i < depth; ++i) {
        os << "  ";
    }
}

void AstNode::writeHeader(std::ostream& os, int depth, const char* name, AstSpan span) {
    writeIndent(os, depth);
    os << name << " [tokens " << span.begin << ".." << span.end << ")\n";
}

AstStatement::AstStatement(AstNodeKind kind, AstSpan span) : AstNode(kind, span) {}

AstExpr::AstExpr(AstNodeKind kind, AstSpan span) : AstNode(kind, span) {}

AstBooleanExpr::AstBooleanExpr(AstNodeKind kind, AstSpan span) : AstNode(kind, span) {}

AstProgram::AstProgram(std::unique_ptr<AstBlock> block, AstSpan span)
    : AstNode(AstNodeKind::Program, span), block_(std::move(block)) {}

void AstProgram::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "Program (AST)", span_);
    if (block_) {
        block_->print(os, depth + 1);
    } else {
        writeIndent(os, depth + 1);
        os << "<null block>\n";
    }
}

AstBlock::AstBlock(std::unique_ptr<AstStatementList> statements, AstSpan span)
    : AstNode(AstNodeKind::Block, span), statements_(std::move(statements)) {}

void AstBlock::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "Block (AST)", span_);
    if (statements_) {
        statements_->print(os, depth + 1);
    } else {
        writeIndent(os, depth + 1);
        os << "<null statement list>\n";
    }
}

AstStatementList::AstStatementList() : AstNode(AstNodeKind::StatementList, {0, 0}) {}

void AstStatementList::addStatement(std::unique_ptr<AstStatement> statement) {
    statements_.push_back(std::move(statement));
}

void AstStatementList::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "StatementList (AST)", span_);
    if (statements_.empty()) {
        writeIndent(os, depth + 1);
        os << "<empty>\n";
        return;
    }
    for (const auto& statement : statements_) {
        if (statement) {
            statement->print(os, depth + 1);
        }
    }
}

AstPrintStatement::AstPrintStatement(std::unique_ptr<AstExpr> expr, AstSpan span)
    : AstStatement(AstNodeKind::PrintStatement, span), expr_(std::move(expr)) {}

void AstPrintStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "PrintStatement (AST)", span_);
    if (expr_) {
        expr_->print(os, depth + 1);
    }
}

AstAssignStatement::AstAssignStatement(char name, std::unique_ptr<AstExpr> expr, AstSpan span)
    : AstStatement(AstNodeKind::AssignStatement, span), name_(name), expr_(std::move(expr)) {}

void AstAssignStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "AssignStatement (AST)", span_);
    writeIndent(os, depth + 1);
    os << "name: `" << name_ << "`";
    if (lhsResolvedDeclBinding_) {
        os << "  [resolved decl scope id: " << lhsResolvedDeclScopeId_ << "]";
    }
    os << "\n";
    if (expr_) {
        expr_->print(os, depth + 1);
    }
}

AstVarDeclStatement::AstVarDeclStatement(std::string typeName, char name, AstSpan span)
    : AstStatement(AstNodeKind::VarDeclStatement, span), typeName_(std::move(typeName)), name_(name) {}

void AstVarDeclStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "VarDeclStatement (AST)", span_);
    writeIndent(os, depth + 1);
    os << "type: `" << typeName_ << "` name: `" << name_ << "`\n";
}

AstWhileStatement::AstWhileStatement(std::unique_ptr<AstBooleanExpr> condition, std::unique_ptr<AstBlock> body, AstSpan span)
    : AstStatement(AstNodeKind::WhileStatement, span), condition_(std::move(condition)), body_(std::move(body)) {}

void AstWhileStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "WhileStatement (AST)", span_);
    writeIndent(os, depth + 1);
    os << "condition:\n";
    if (condition_) {
        condition_->print(os, depth + 2);
    }
    writeIndent(os, depth + 1);
    os << "body:\n";
    if (body_) {
        body_->print(os, depth + 2);
    }
}

AstIfStatement::AstIfStatement(std::unique_ptr<AstBooleanExpr> condition, std::unique_ptr<AstBlock> body, AstSpan span)
    : AstStatement(AstNodeKind::IfStatement, span), condition_(std::move(condition)), body_(std::move(body)) {}

void AstIfStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "IfStatement (AST)", span_);
    writeIndent(os, depth + 1);
    os << "condition:\n";
    if (condition_) {
        condition_->print(os, depth + 2);
    }
    writeIndent(os, depth + 1);
    os << "body:\n";
    if (body_) {
        body_->print(os, depth + 2);
    }
}

AstBlockStatement::AstBlockStatement(std::unique_ptr<AstBlock> block, AstSpan span)
    : AstStatement(AstNodeKind::BlockStatement, span), block_(std::move(block)) {}

void AstBlockStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "BlockStatement (AST)", span_);
    if (block_) {
        block_->print(os, depth + 1);
    }
}

AstLiteralInt::AstLiteralInt(std::string lexeme, AstSpan span)
    : AstExpr(AstNodeKind::LiteralInt, span), lexeme_(std::move(lexeme)) {}

void AstLiteralInt::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "LiteralInt (AST)", span_);
    writeIndent(os, depth + 1);
    os << "value: `" << lexeme_ << "`\n";
}

AstLiteralString::AstLiteralString(std::string lexeme, AstSpan span)
    : AstExpr(AstNodeKind::LiteralString, span), lexeme_(std::move(lexeme)) {}

void AstLiteralString::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "LiteralString (AST)", span_);
    writeIndent(os, depth + 1);
    os << "value: `" << lexeme_ << "`\n";
}

AstLiteralBool::AstLiteralBool(bool value, AstSpan span) : AstExpr(AstNodeKind::LiteralBool, span), value_(value) {}

void AstLiteralBool::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "LiteralBool (AST)", span_);
    writeIndent(os, depth + 1);
    os << "value: " << (value_ ? "true" : "false") << "\n";
}

AstIdentifierExpr::AstIdentifierExpr(char name, AstSpan span) : AstExpr(AstNodeKind::IdentifierExpr, span), name_(name) {}

void AstIdentifierExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "IdentifierExpr (AST)", span_);
    writeIndent(os, depth + 1);
    os << "name: `" << name_ << "`";
    if (resolvedDeclBinding_) {
        os << "  [resolved decl scope id: " << resolvedDeclScopeId_ << "]";
    }
    os << "\n";
}

AstAddExpr::AstAddExpr(std::unique_ptr<AstExpr> left, std::unique_ptr<AstExpr> right, AstSpan span)
    : AstExpr(AstNodeKind::AddExpr, span), left_(std::move(left)), right_(std::move(right)) {}

void AstAddExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "AddExpr (AST) (+)", span_);
    writeIndent(os, depth + 1);
    os << "left:\n";
    if (left_) {
        left_->print(os, depth + 2);
    }
    writeIndent(os, depth + 1);
    os << "right:\n";
    if (right_) {
        right_->print(os, depth + 2);
    }
}

AstBinaryBoolExpr::AstBinaryBoolExpr(Op op, std::unique_ptr<AstExpr> left, std::unique_ptr<AstExpr> right, AstSpan span)
    : AstBooleanExpr(AstNodeKind::BinaryBoolExpr, span), op_(op), left_(std::move(left)), right_(std::move(right)) {}

void AstBinaryBoolExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "BinaryBoolExpr (AST)", span_);
    writeIndent(os, depth + 1);
    os << "op: `" << opName(op_) << "`\n";
    writeIndent(os, depth + 1);
    os << "left:\n";
    if (left_) {
        left_->print(os, depth + 2);
    }
    writeIndent(os, depth + 1);
    os << "right:\n";
    if (right_) {
        right_->print(os, depth + 2);
    }
}

AstBooleanLiteralExpr::AstBooleanLiteralExpr(bool value, AstSpan span)
    : AstBooleanExpr(AstNodeKind::BooleanLiteralExpr, span), value_(value) {}

void AstBooleanLiteralExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "BooleanLiteralExpr (AST)", span_);
    writeIndent(os, depth + 1);
    os << "value: " << (value_ ? "true" : "false") << "\n";
}

AstBooleanExprWrapper::AstBooleanExprWrapper(std::unique_ptr<AstBooleanExpr> inner, AstSpan span)
    : AstExpr(AstNodeKind::BooleanExprWrapper, span), inner_(std::move(inner)) {}

void AstBooleanExprWrapper::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "BooleanExprWrapper (AST)", span_);
    if (inner_) {
        inner_->print(os, depth + 1);
    }
}

} // namespace pavic
