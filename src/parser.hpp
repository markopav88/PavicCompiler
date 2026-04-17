#pragma once

#include "cst.hpp"
#include "diagnostic.hpp"
#include "source.hpp"
#include "token.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace pavic {

/// Token-stream cursor + recursive descent parser + CST construction.
class Parser {
public:
    Parser(const SourceMap& map, DiagnosticBag& diagnostics, const std::vector<Token>& tokens, bool verbose);

    const SourceMap& sourceMap() const { return map_; }
    const std::vector<Token>& tokens() const { return tokens_; }
    bool verbose() const { return verbose_; }

    std::size_t index() const { return index_; }

    /// Current token (always valid while `tokens_` is non-empty; lexer appends `EndOfFile`).
    const Token& peek() const;

    /// Peek `distance` tokens ahead (`0` == `peek()`). Must not pass end of stream.
    const Token& peek(std::size_t distance) const;

    /// Advance one token if not already past the final `EndOfFile`.
    void advance();

    bool isAtEnd() const;

    /// If the current token matches `kind`, consume it and return true. No diagnostic on failure.
    bool match(TokenKind kind);

    /// Require `kind` at the current position; on failure emit a detailed error and return false.
    bool expect(TokenKind kind, const std::string& context);

    void trace(const std::string& message) const;

    /// One `Program ::= Block '$'` then stops (caller may repeat for multi-program files).
    std::unique_ptr<CstProgram> parseProgram();

    /// All programs in the token stream: `( Block '$' )*`, stopping at `EndOfFile`.
    std::vector<std::unique_ptr<CstProgram>> parseTranslationUnit();

private:
    const SourceMap& map_;
    DiagnosticBag& diagnostics_;
    const std::vector<Token>& tokens_;
    bool verbose_;
    std::size_t index_ = 0;

    bool statementStartsHere() const;
    static bool isTypeKeyword(TokenKind kind);
    static std::string typeKeywordLexeme(TokenKind kind);

    std::unique_ptr<CstBlock> parseBlock();
    std::unique_ptr<CstStatementList> parseStatementList();
    std::unique_ptr<CstStatement> parseStatement();

    std::unique_ptr<CstPrintStatement> parsePrintStatement();
    std::unique_ptr<CstAssignStatement> parseAssignStatement();
    std::unique_ptr<CstVarDeclStatement> parseVarDeclStatement();
    std::unique_ptr<CstWhileStatement> parseWhileStatement();
    std::unique_ptr<CstIfStatement> parseIfStatement();

    std::unique_ptr<CstExpr> parseExpr();
    std::unique_ptr<CstBooleanExpr> parseBooleanExpr();
    std::unique_ptr<CstBinaryBoolExpr> parseBooleanParenExpr();
};

} // namespace pavic
