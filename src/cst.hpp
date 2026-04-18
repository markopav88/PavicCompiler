#pragma once

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace pavic {

class CstBlock;
class CstStatementList;
class CstStatement;
class CstExpr;
class CstBooleanExpr;

/// Half-open range of token indices `[begin, end)` owned by this CST subtree (filled by parser).
struct CstSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

enum class CstNodeKind {
    Program,
    Block,
    StatementList,
    PrintStatement,
    AssignStatement,
    VarDeclStatement,
    WhileStatement,
    IfStatement,
    BlockStatement,
    Expr,
    AddExpr,
    BinaryBoolExpr,
    LiteralInt,
    LiteralString,
    LiteralBool,
    BooleanLiteral,
    IdentifierExpr,
};

class CstNode {
public:
    virtual ~CstNode() = default;

    CstNode(CstNodeKind kind, CstSpan span);

    CstNodeKind nodeKind() const { return kind_; }
    CstSpan span() const { return span_; }
    void setSpan(CstSpan span) { span_ = span; }

    virtual void print(std::ostream& os, int depth) const = 0;

protected:
    static void writeIndent(std::ostream& os, int depth);
    static void writeHeader(std::ostream& os, int depth, const char* name, CstSpan span);

    CstNodeKind kind_;
    CstSpan span_;
};

// ---------------------------------------------------------------------------
// Program ::= Block '$'  (terminal `$` is not stored as a child; span covers it)
// ---------------------------------------------------------------------------
class CstProgram final : public CstNode {
public:
    explicit CstProgram(std::unique_ptr<CstBlock> block, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstBlock* block() const { return block_.get(); }

private:
    std::unique_ptr<CstBlock> block_;
};

// ---------------------------------------------------------------------------
// Block ::= '{' StatementList '}'
// ---------------------------------------------------------------------------
class CstBlock final : public CstNode {
public:
    explicit CstBlock(std::unique_ptr<class CstStatementList> statements, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstStatementList* statements() const { return statements_.get(); }

private:
    std::unique_ptr<CstStatementList> statements_;
};

// ---------------------------------------------------------------------------
// StatementList ::= Statement StatementList | ε
// ---------------------------------------------------------------------------
class CstStatementList final : public CstNode {
public:
    CstStatementList();

    void addStatement(std::unique_ptr<class CstStatement> statement);

    void print(std::ostream& os, int depth) const override;

    const std::vector<std::unique_ptr<CstStatement>>& statements() const { return statements_; }

private:
    std::vector<std::unique_ptr<CstStatement>> statements_;
};

// ---------------------------------------------------------------------------
// Statement (abstract)
// ---------------------------------------------------------------------------
class CstStatement : public CstNode {
protected:
    CstStatement(CstNodeKind kind, CstSpan span);
};

class CstPrintStatement final : public CstStatement {
public:
    explicit CstPrintStatement(std::unique_ptr<class CstExpr> expr, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstExpr* expr() const { return expr_.get(); }

private:
    std::unique_ptr<CstExpr> expr_;
};

class CstAssignStatement final : public CstStatement {
public:
    CstAssignStatement(char name, std::unique_ptr<class CstExpr> expr, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    char name() const { return name_; }
    const CstExpr* expr() const { return expr_.get(); }

private:
    char name_;
    std::unique_ptr<CstExpr> expr_;
};

class CstVarDeclStatement final : public CstStatement {
public:
    CstVarDeclStatement(std::string typeName, char name, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const std::string& typeName() const { return typeName_; }
    char name() const { return name_; }

private:
    std::string typeName_;
    char name_;
};

class CstWhileStatement final : public CstStatement {
public:
    CstWhileStatement(std::unique_ptr<class CstBooleanExpr> condition, std::unique_ptr<CstBlock> body, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstBooleanExpr* condition() const { return condition_.get(); }
    const CstBlock* body() const { return body_.get(); }

private:
    std::unique_ptr<CstBooleanExpr> condition_;
    std::unique_ptr<CstBlock> body_;
};

class CstIfStatement final : public CstStatement {
public:
    CstIfStatement(std::unique_ptr<class CstBooleanExpr> condition, std::unique_ptr<CstBlock> body, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstBooleanExpr* condition() const { return condition_.get(); }
    const CstBlock* body() const { return body_.get(); }

private:
    std::unique_ptr<CstBooleanExpr> condition_;
    std::unique_ptr<CstBlock> body_;
};

/// Statement ::= Block (nested block as a statement)
class CstBlockStatement final : public CstStatement {
public:
    explicit CstBlockStatement(std::unique_ptr<CstBlock> block, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstBlock* block() const { return block_.get(); }

private:
    std::unique_ptr<CstBlock> block_;
};

// ---------------------------------------------------------------------------
// Expr (abstract) — IntExpr | StringExpr | BooleanExpr | Id
// ---------------------------------------------------------------------------
class CstExpr : public CstNode {
protected:
    CstExpr(CstNodeKind kind, CstSpan span);
};

class CstLiteralInt final : public CstExpr {
public:
    explicit CstLiteralInt(std::string lexeme, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const std::string& lexeme() const { return lexeme_; }

private:
    std::string lexeme_;
};

class CstLiteralString final : public CstExpr {
public:
    explicit CstLiteralString(std::string lexeme, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const std::string& lexeme() const { return lexeme_; }

private:
    std::string lexeme_;
};

class CstLiteralBool final : public CstExpr {
public:
    explicit CstLiteralBool(bool value, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    bool value() const { return value_; }

private:
    bool value_;
};

class CstIdentifierExpr final : public CstExpr {
public:
    explicit CstIdentifierExpr(char name, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    char name() const { return name_; }

private:
    char name_;
};

/// IntExpr ::= digit intop Expr  (only `+` as intop in your grammar)
class CstAddExpr final : public CstExpr {
public:
    CstAddExpr(std::unique_ptr<CstExpr> left, std::unique_ptr<CstExpr> right, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstExpr* left() const { return left_.get(); }
    const CstExpr* right() const { return right_.get(); }

private:
    std::unique_ptr<CstExpr> left_;
    std::unique_ptr<CstExpr> right_;
};

// ---------------------------------------------------------------------------
// BooleanExpr ::= '(' Expr boolop Expr ')' | boolval
// ---------------------------------------------------------------------------
class CstBooleanExpr : public CstNode {
protected:
    CstBooleanExpr(CstNodeKind kind, CstSpan span);
};

class CstBinaryBoolExpr final : public CstBooleanExpr {
public:
    enum class Op { Equal, NotEqual };

    CstBinaryBoolExpr(Op op, std::unique_ptr<CstExpr> left, std::unique_ptr<CstExpr> right, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    Op op() const { return op_; }
    const CstExpr* left() const { return left_.get(); }
    const CstExpr* right() const { return right_.get(); }

private:
    Op op_;
    std::unique_ptr<CstExpr> left_;
    std::unique_ptr<CstExpr> right_;
};

class CstBooleanLiteralExpr final : public CstBooleanExpr {
public:
    explicit CstBooleanLiteralExpr(bool value, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    bool value() const { return value_; }

private:
    bool value_;
};

/// Expr that wraps a BooleanExpr subtree (when Expr ::= BooleanExpr)
class CstBooleanExprWrapper final : public CstExpr {
public:
    explicit CstBooleanExprWrapper(std::unique_ptr<CstBooleanExpr> inner, CstSpan span);

    void print(std::ostream& os, int depth) const override;

    const CstBooleanExpr* inner() const { return inner_.get(); }

private:
    std::unique_ptr<CstBooleanExpr> inner_;
};

} // namespace pavic
