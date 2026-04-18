#pragma once

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace pavic {

/// Token half-range copied from the CST for diagnostics (same convention as `CstSpan`).
struct AstSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

enum class AstNodeKind {
    Program,
    Block,
    StatementList,
    PrintStatement,
    AssignStatement,
    VarDeclStatement,
    WhileStatement,
    IfStatement,
    BlockStatement,
    LiteralInt,
    LiteralString,
    LiteralBool,
    IdentifierExpr,
    AddExpr,
    BinaryBoolExpr,
    BooleanLiteralExpr,
    BooleanExprWrapper,
};

class AstNode {
public:
    virtual ~AstNode() = default;

    AstNode(AstNodeKind kind, AstSpan span);

    AstNodeKind nodeKind() const { return kind_; }
    AstSpan span() const { return span_; }
    void setSpan(AstSpan span) { span_ = span; }

    virtual void print(std::ostream& os, int depth) const = 0;

protected:
    static void writeIndent(std::ostream& os, int depth);
    static void writeHeader(std::ostream& os, int depth, const char* name, AstSpan span);

    AstNodeKind kind_;
    AstSpan span_;
};

class AstBlock;
class AstStatementList;
class AstStatement;
class AstExpr;
class AstBooleanExpr;

class AstProgram final : public AstNode {
public:
    explicit AstProgram(std::unique_ptr<AstBlock> block, AstSpan span);

    void print(std::ostream& os, int depth) const override;

    const AstBlock* block() const { return block_.get(); }

private:
    std::unique_ptr<AstBlock> block_;
};

class AstBlock final : public AstNode {
public:
    explicit AstBlock(std::unique_ptr<AstStatementList> statements, AstSpan span);

    void print(std::ostream& os, int depth) const override;

    const AstStatementList* statements() const { return statements_.get(); }

private:
    std::unique_ptr<AstStatementList> statements_;
};

class AstStatementList final : public AstNode {
public:
    AstStatementList();

    void addStatement(std::unique_ptr<AstStatement> statement);

    void print(std::ostream& os, int depth) const override;

    const std::vector<std::unique_ptr<AstStatement>>& statements() const { return statements_; }

private:
    std::vector<std::unique_ptr<AstStatement>> statements_;
};

class AstStatement : public AstNode {
protected:
    AstStatement(AstNodeKind kind, AstSpan span);
};

class AstPrintStatement final : public AstStatement {
public:
    explicit AstPrintStatement(std::unique_ptr<AstExpr> expr, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::unique_ptr<AstExpr> expr_;
};

class AstAssignStatement final : public AstStatement {
public:
    AstAssignStatement(char name, std::unique_ptr<AstExpr> expr, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    char name_;
    std::unique_ptr<AstExpr> expr_;
};

class AstVarDeclStatement final : public AstStatement {
public:
    AstVarDeclStatement(std::string typeName, char name, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::string typeName_;
    char name_;
};

class AstWhileStatement final : public AstStatement {
public:
    AstWhileStatement(std::unique_ptr<AstBooleanExpr> condition, std::unique_ptr<AstBlock> body, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::unique_ptr<AstBooleanExpr> condition_;
    std::unique_ptr<AstBlock> body_;
};

class AstIfStatement final : public AstStatement {
public:
    AstIfStatement(std::unique_ptr<AstBooleanExpr> condition, std::unique_ptr<AstBlock> body, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::unique_ptr<AstBooleanExpr> condition_;
    std::unique_ptr<AstBlock> body_;
};

class AstBlockStatement final : public AstStatement {
public:
    explicit AstBlockStatement(std::unique_ptr<AstBlock> block, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::unique_ptr<AstBlock> block_;
};

class AstExpr : public AstNode {
protected:
    AstExpr(AstNodeKind kind, AstSpan span);
};

class AstLiteralInt final : public AstExpr {
public:
    explicit AstLiteralInt(std::string lexeme, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::string lexeme_;
};

class AstLiteralString final : public AstExpr {
public:
    explicit AstLiteralString(std::string lexeme, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::string lexeme_;
};

class AstLiteralBool final : public AstExpr {
public:
    explicit AstLiteralBool(bool value, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    bool value_;
};

class AstIdentifierExpr final : public AstExpr {
public:
    explicit AstIdentifierExpr(char name, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    char name_;
};

class AstAddExpr final : public AstExpr {
public:
    AstAddExpr(std::unique_ptr<AstExpr> left, std::unique_ptr<AstExpr> right, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::unique_ptr<AstExpr> left_;
    std::unique_ptr<AstExpr> right_;
};

class AstBooleanExpr : public AstNode {
protected:
    AstBooleanExpr(AstNodeKind kind, AstSpan span);
};

class AstBinaryBoolExpr final : public AstBooleanExpr {
public:
    enum class Op { Equal, NotEqual };

    AstBinaryBoolExpr(Op op, std::unique_ptr<AstExpr> left, std::unique_ptr<AstExpr> right, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    Op op_;
    std::unique_ptr<AstExpr> left_;
    std::unique_ptr<AstExpr> right_;
};

class AstBooleanLiteralExpr final : public AstBooleanExpr {
public:
    explicit AstBooleanLiteralExpr(bool value, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    bool value_;
};

/// Expr position wrapping a boolean sub-tree (from CST `CstBooleanExprWrapper`).
class AstBooleanExprWrapper final : public AstExpr {
public:
    explicit AstBooleanExprWrapper(std::unique_ptr<AstBooleanExpr> inner, AstSpan span);

    void print(std::ostream& os, int depth) const override;

private:
    std::unique_ptr<AstBooleanExpr> inner_;
};

} // namespace pavic
