#include "cst.hpp"

namespace pavic {

namespace {

const char* opName(CstBinaryBoolExpr::Op op) {
    switch (op) {
    case CstBinaryBoolExpr::Op::Equal:
        return "==";
    case CstBinaryBoolExpr::Op::NotEqual:
        return "!=";
    }
    return "?";
}

} // namespace

// --- CstNode -----------------------------------------------------------------

CstNode::CstNode(CstNodeKind kind, CstSpan span) : kind_(kind), span_(span) {}

void CstNode::writeIndent(std::ostream& os, int depth) {
    for (int i = 0; i < depth; ++i) {
        os << "  ";
    }
}

void CstNode::writeHeader(std::ostream& os, int depth, const char* name, CstSpan span) {
    writeIndent(os, depth);
    os << name << " [tokens " << span.begin << ".." << span.end << ")\n";
}

// --- CstStatement / CstExpr / CstBooleanExpr base ----------------------------

CstStatement::CstStatement(CstNodeKind kind, CstSpan span) : CstNode(kind, span) {}

CstExpr::CstExpr(CstNodeKind kind, CstSpan span) : CstNode(kind, span) {}

CstBooleanExpr::CstBooleanExpr(CstNodeKind kind, CstSpan span) : CstNode(kind, span) {}

// --- CstProgram --------------------------------------------------------------

CstProgram::CstProgram(std::unique_ptr<CstBlock> block, CstSpan span)
    : CstNode(CstNodeKind::Program, span), block_(std::move(block)) {}

void CstProgram::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "Program", span_);
    if (block_) {
        block_->print(os, depth + 1);
    } else {
        writeIndent(os, depth + 1);
        os << "<null block>\n";
    }
}

// --- CstBlock ----------------------------------------------------------------

CstBlock::CstBlock(std::unique_ptr<CstStatementList> statements, CstSpan span)
    : CstNode(CstNodeKind::Block, span), statements_(std::move(statements)) {}

void CstBlock::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "Block", span_);
    if (statements_) {
        statements_->print(os, depth + 1);
    } else {
        writeIndent(os, depth + 1);
        os << "<null statement list>\n";
    }
}

// --- CstStatementList --------------------------------------------------------

CstStatementList::CstStatementList() : CstNode(CstNodeKind::StatementList, {0, 0}) {}

void CstStatementList::addStatement(std::unique_ptr<CstStatement> statement) {
    statements_.push_back(std::move(statement));
}

void CstStatementList::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "StatementList", span_);
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

// --- Statements --------------------------------------------------------------

CstPrintStatement::CstPrintStatement(std::unique_ptr<CstExpr> expr, CstSpan span)
    : CstStatement(CstNodeKind::PrintStatement, span), expr_(std::move(expr)) {}

void CstPrintStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "PrintStatement", span_);
    if (expr_) {
        expr_->print(os, depth + 1);
    }
}

CstAssignStatement::CstAssignStatement(char name, std::unique_ptr<CstExpr> expr, CstSpan span)
    : CstStatement(CstNodeKind::AssignStatement, span), name_(name), expr_(std::move(expr)) {}

void CstAssignStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "AssignStatement", span_);
    writeIndent(os, depth + 1);
    os << "name: `" << name_ << "`\n";
    if (expr_) {
        expr_->print(os, depth + 1);
    }
}

CstVarDeclStatement::CstVarDeclStatement(std::string typeName, char name, CstSpan span)
    : CstStatement(CstNodeKind::VarDeclStatement, span), typeName_(std::move(typeName)), name_(name) {}

void CstVarDeclStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "VarDeclStatement", span_);
    writeIndent(os, depth + 1);
    os << "type: `" << typeName_ << "` name: `" << name_ << "`\n";
}

CstWhileStatement::CstWhileStatement(std::unique_ptr<CstBooleanExpr> condition, std::unique_ptr<CstBlock> body, CstSpan span)
    : CstStatement(CstNodeKind::WhileStatement, span), condition_(std::move(condition)), body_(std::move(body)) {}

void CstWhileStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "WhileStatement", span_);
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

CstIfStatement::CstIfStatement(std::unique_ptr<CstBooleanExpr> condition, std::unique_ptr<CstBlock> body, CstSpan span)
    : CstStatement(CstNodeKind::IfStatement, span), condition_(std::move(condition)), body_(std::move(body)) {}

void CstIfStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "IfStatement", span_);
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

CstBlockStatement::CstBlockStatement(std::unique_ptr<CstBlock> block, CstSpan span)
    : CstStatement(CstNodeKind::BlockStatement, span), block_(std::move(block)) {}

void CstBlockStatement::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "BlockStatement", span_);
    if (block_) {
        block_->print(os, depth + 1);
    }
}

// --- Expressions -------------------------------------------------------------

CstLiteralInt::CstLiteralInt(std::string lexeme, CstSpan span)
    : CstExpr(CstNodeKind::LiteralInt, span), lexeme_(std::move(lexeme)) {}

void CstLiteralInt::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "LiteralInt", span_);
    writeIndent(os, depth + 1);
    os << "value: `" << lexeme_ << "`\n";
}

CstLiteralString::CstLiteralString(std::string lexeme, CstSpan span)
    : CstExpr(CstNodeKind::LiteralString, span), lexeme_(std::move(lexeme)) {}

void CstLiteralString::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "LiteralString", span_);
    writeIndent(os, depth + 1);
    os << "value: `" << lexeme_ << "`\n";
}

CstLiteralBool::CstLiteralBool(bool value, CstSpan span) : CstExpr(CstNodeKind::LiteralBool, span), value_(value) {}

void CstLiteralBool::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "LiteralBool", span_);
    writeIndent(os, depth + 1);
    os << "value: " << (value_ ? "true" : "false") << "\n";
}

CstIdentifierExpr::CstIdentifierExpr(char name, CstSpan span) : CstExpr(CstNodeKind::IdentifierExpr, span), name_(name) {}

void CstIdentifierExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "IdentifierExpr", span_);
    writeIndent(os, depth + 1);
    os << "name: `" << name_ << "`\n";
}

CstAddExpr::CstAddExpr(std::unique_ptr<CstExpr> left, std::unique_ptr<CstExpr> right, CstSpan span)
    : CstExpr(CstNodeKind::AddExpr, span), left_(std::move(left)), right_(std::move(right)) {}

void CstAddExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "AddExpr (+)", span_);
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

CstBinaryBoolExpr::CstBinaryBoolExpr(Op op, std::unique_ptr<CstExpr> left, std::unique_ptr<CstExpr> right, CstSpan span)
    : CstBooleanExpr(CstNodeKind::BinaryBoolExpr, span), op_(op), left_(std::move(left)), right_(std::move(right)) {}

void CstBinaryBoolExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "BinaryBoolExpr", span_);
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

CstBooleanLiteralExpr::CstBooleanLiteralExpr(bool value, CstSpan span)
    : CstBooleanExpr(CstNodeKind::BooleanLiteral, span), value_(value) {}

void CstBooleanLiteralExpr::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "BooleanLiteralExpr", span_);
    writeIndent(os, depth + 1);
    os << "value: " << (value_ ? "true" : "false") << "\n";
}

CstBooleanExprWrapper::CstBooleanExprWrapper(std::unique_ptr<CstBooleanExpr> inner, CstSpan span)
    : CstExpr(CstNodeKind::Expr, span), inner_(std::move(inner)) {}

void CstBooleanExprWrapper::print(std::ostream& os, int depth) const {
    writeHeader(os, depth, "Expr (BooleanExpr)", span_);
    if (inner_) {
        inner_->print(os, depth + 1);
    }
}

} // namespace pavic
