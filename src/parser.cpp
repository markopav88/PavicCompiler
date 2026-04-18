#include "parser.hpp"

#include <iostream>
#include <sstream>
#include <utility>

namespace pavic {

Parser::Parser(const SourceMap& map, DiagnosticBag& diagnostics, const std::vector<Token>& tokens, bool verbose)
    : map_(map), diagnostics_(diagnostics), tokens_(tokens), verbose_(verbose) {
    if (tokens_.empty()) {
        diagnostics_.addError(
            "internal parser error: token stream is empty",
            {1, 1},
            "The lexer should always emit at least an `EndOfFile` token."
        );
    }
}

const Token& Parser::peek() const {
    return peek(0);
}

const Token& Parser::peek(std::size_t distance) const {
    if (index_ + distance < tokens_.size()) {
        return tokens_[index_ + distance];
    }
    return tokens_.back();
}

void Parser::advance() {
    if (tokens_.empty()) {
        return;
    }
    if (peek().kind == TokenKind::EndOfFile) {
        return;
    }
    if (index_ + 1 < tokens_.size()) {
        ++index_;
    }
}

bool Parser::isAtEnd() const {
    return peek().kind == TokenKind::EndOfFile;
}

bool Parser::match(TokenKind kind) {
    if (peek().kind == kind) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenKind kind, const std::string& context) {
    const Token& current = peek();
    if (current.kind == kind) {
        advance();
        return true;
    }

    std::ostringstream message;
    message << "expected " << tokenKindName(kind) << " but found " << tokenKindName(current.kind);
    if (!context.empty()) {
        message << " (" << context << ")";
    }

    std::ostringstream hint;
    hint << "Insert or fix the token so a " << tokenKindName(kind) << " appears here.";
    if (current.kind == TokenKind::EndOfFile) {
        hint << " The parser reached end-of-input earlier than expected — check for a missing token "
                "near the end of this construct.";
    } else if (!current.lexeme.empty()) {
        hint << " Current lexeme is `" << current.lexeme << "`.";
    }

    diagnostics_.addError(message.str(), map_.locationAt(current.offset), hint.str());
    return false;
}

void Parser::trace(const std::string& message) const {
    if (!verbose_) {
        return;
    }
    const Token& current = peek();
    const SourceLocation location = map_.locationAt(current.offset);
    std::cout << "[parse] " << location.line << ":" << location.column << ": " << message << "\n";
}

bool Parser::isTypeKeyword(TokenKind kind) {
    return kind == TokenKind::KwInt || kind == TokenKind::KwString || kind == TokenKind::KwBoolean;
}

std::string Parser::typeKeywordLexeme(TokenKind kind) {
    switch (kind) {
    case TokenKind::KwInt:
        return "int";
    case TokenKind::KwString:
        return "string";
    case TokenKind::KwBoolean:
        return "boolean";
    default:
        return "?";
    }
}

bool Parser::statementStartsHere() const {
    const TokenKind k = peek().kind;
    if (k == TokenKind::EndOfFile || k == TokenKind::RightBrace) {
        return false;
    }
    return k == TokenKind::KwPrint || k == TokenKind::KwWhile || k == TokenKind::KwIf || k == TokenKind::LeftBrace ||
           k == TokenKind::Identifier || isTypeKeyword(k);
}

std::unique_ptr<CstExpr> Parser::parseExpr() {
    const std::size_t begin = index_;
    const Token& t = peek();

    if (t.kind == TokenKind::StringLiteral) {
        const std::string lex(t.lexeme);
        advance();
        auto node = std::make_unique<CstLiteralString>(lex, CstSpan{begin, index_});
        return node;
    }

    if (t.kind == TokenKind::KwTrue) {
        advance();
        return std::make_unique<CstLiteralBool>(true, CstSpan{begin, index_});
    }
    if (t.kind == TokenKind::KwFalse) {
        advance();
        return std::make_unique<CstLiteralBool>(false, CstSpan{begin, index_});
    }

    if (t.kind == TokenKind::Identifier) {
        if (t.lexeme.empty()) {
            diagnostics_.addError(
                "internal parser error: empty identifier lexeme",
                map_.locationAt(t.offset),
                "The lexer should always attach a one-letter lexeme for identifiers."
            );
            return nullptr;
        }
        const char name = t.lexeme[0];
        advance();
        return std::make_unique<CstIdentifierExpr>(name, CstSpan{begin, index_});
    }

    if (t.kind == TokenKind::IntegerLiteral) {
        const std::string lex(t.lexeme);
        advance();
        if (match(TokenKind::Plus)) {
            std::unique_ptr<CstExpr> left = std::make_unique<CstLiteralInt>(lex, CstSpan{begin, begin + 1});
            std::unique_ptr<CstExpr> right = parseExpr();
            if (!right) {
                return nullptr;
            }
            return std::make_unique<CstAddExpr>(std::move(left), std::move(right), CstSpan{begin, index_});
        }
        return std::make_unique<CstLiteralInt>(lex, CstSpan{begin, index_});
    }

    if (t.kind == TokenKind::LeftParen) {
        trace("parse Expr as parenthesized BooleanExpr");
        std::unique_ptr<CstBinaryBoolExpr> bin = parseBooleanParenExpr();
        if (!bin) {
            return nullptr;
        }
        std::unique_ptr<CstBooleanExpr> base(bin.release());
        return std::make_unique<CstBooleanExprWrapper>(std::move(base), CstSpan{begin, index_});
    }

    diagnostics_.addError(
        "expected an expression (integer, string, boolean, identifier, or parenthesized comparison)",
        map_.locationAt(t.offset),
        "Start the expression with a digit, `\"`, `true`/`false`, a single-letter name `a`–`z`, or `(` for a comparison."
    );
    return nullptr;
}

std::unique_ptr<CstBinaryBoolExpr> Parser::parseBooleanParenExpr() {
    const std::size_t begin = index_;
    if (!expect(TokenKind::LeftParen, "start of BooleanExpr")) {
        return nullptr;
    }

    std::unique_ptr<CstExpr> left = parseExpr();
    if (!left) {
        return nullptr;
    }

    CstBinaryBoolExpr::Op op = CstBinaryBoolExpr::Op::Equal;
    if (peek().kind == TokenKind::EqualEqual) {
        op = CstBinaryBoolExpr::Op::Equal;
        advance();
    } else if (peek().kind == TokenKind::BangEqual) {
        op = CstBinaryBoolExpr::Op::NotEqual;
        advance();
    } else {
        diagnostics_.addError(
            "expected `==` or `!=` inside BooleanExpr",
            map_.locationAt(peek().offset),
            "A parenthesized boolean comparison must use `==` or `!=` between the two sub-expressions."
        );
        return nullptr;
    }

    std::unique_ptr<CstExpr> right = parseExpr();
    if (!right) {
        return nullptr;
    }

    if (!expect(TokenKind::RightParen, "end of BooleanExpr")) {
        return nullptr;
    }

    return std::make_unique<CstBinaryBoolExpr>(op, std::move(left), std::move(right), CstSpan{begin, index_});
}

std::unique_ptr<CstBooleanExpr> Parser::parseBooleanExpr() {
    const std::size_t begin = index_;
    const Token& t = peek();

    if (t.kind == TokenKind::KwTrue) {
        advance();
        return std::make_unique<CstBooleanLiteralExpr>(true, CstSpan{begin, index_});
    }
    if (t.kind == TokenKind::KwFalse) {
        advance();
        return std::make_unique<CstBooleanLiteralExpr>(false, CstSpan{begin, index_});
    }
    if (t.kind == TokenKind::LeftParen) {
        std::unique_ptr<CstBinaryBoolExpr> bin = parseBooleanParenExpr();
        if (!bin) {
            return nullptr;
        }
        return std::unique_ptr<CstBooleanExpr>(std::move(bin));
    }

    diagnostics_.addError(
        "expected a boolean expression (`true`, `false`, or `( ... == ... )` / `( ... != ... )`)",
        map_.locationAt(t.offset),
        "After `while` or `if`, provide either a boolean literal or a parenthesized comparison."
    );
    return nullptr;
}

std::unique_ptr<CstPrintStatement> Parser::parsePrintStatement() {
    const std::size_t begin = index_;
    trace("parse PrintStatement");
    if (!expect(TokenKind::KwPrint, "PrintStatement")) {
        return nullptr;
    }
    if (!expect(TokenKind::LeftParen, "after `print`")) {
        return nullptr;
    }
    std::unique_ptr<CstExpr> expr = parseExpr();
    if (!expr) {
        return nullptr;
    }
    if (!expect(TokenKind::RightParen, "after print expression")) {
        return nullptr;
    }
    return std::make_unique<CstPrintStatement>(std::move(expr), CstSpan{begin, index_});
}

std::unique_ptr<CstAssignStatement> Parser::parseAssignStatement() {
    const std::size_t begin = index_;
    trace("parse AssignStatement");
    const Token& idTok = peek();
    if (idTok.kind != TokenKind::Identifier || idTok.lexeme.empty()) {
        diagnostics_.addError(
            "expected identifier at start of assignment",
            map_.locationAt(idTok.offset),
            "Assignments must begin with a single-letter identifier `a`–`z`."
        );
        return nullptr;
    }
    const char name = idTok.lexeme[0];
    advance();
    if (!expect(TokenKind::Equal, "assignment")) {
        return nullptr;
    }
    std::unique_ptr<CstExpr> expr = parseExpr();
    if (!expr) {
        return nullptr;
    }
    return std::make_unique<CstAssignStatement>(name, std::move(expr), CstSpan{begin, index_});
}

std::unique_ptr<CstVarDeclStatement> Parser::parseVarDeclStatement() {
    const std::size_t begin = index_;
    trace("parse VarDeclStatement");
    const TokenKind typeKind = peek().kind;
    if (!isTypeKeyword(typeKind)) {
        diagnostics_.addError(
            "expected type keyword `int`, `string`, or `boolean`",
            map_.locationAt(peek().offset),
            "Variable declarations must start with one of the three type keywords."
        );
        return nullptr;
    }
    const std::string typeName = typeKeywordLexeme(typeKind);
    advance();

    const Token& idTok = peek();
    if (idTok.kind != TokenKind::Identifier || idTok.lexeme.empty()) {
        diagnostics_.addError(
            "expected identifier after type in variable declaration",
            map_.locationAt(idTok.offset),
            "Declare variables as: `int x`, `string y`, or `boolean z` using a single-letter name."
        );
        return nullptr;
    }
    const char name = idTok.lexeme[0];
    advance();

    return std::make_unique<CstVarDeclStatement>(typeName, name, CstSpan{begin, index_});
}

std::unique_ptr<CstWhileStatement> Parser::parseWhileStatement() {
    const std::size_t begin = index_;
    trace("parse WhileStatement");
    if (!expect(TokenKind::KwWhile, "WhileStatement")) {
        return nullptr;
    }
    std::unique_ptr<CstBooleanExpr> cond = parseBooleanExpr();
    if (!cond) {
        return nullptr;
    }
    std::unique_ptr<CstBlock> body = parseBlock();
    if (!body) {
        return nullptr;
    }
    return std::make_unique<CstWhileStatement>(std::move(cond), std::move(body), CstSpan{begin, index_});
}

std::unique_ptr<CstIfStatement> Parser::parseIfStatement() {
    const std::size_t begin = index_;
    trace("parse IfStatement");
    if (!expect(TokenKind::KwIf, "IfStatement")) {
        return nullptr;
    }
    std::unique_ptr<CstBooleanExpr> cond = parseBooleanExpr();
    if (!cond) {
        return nullptr;
    }
    std::unique_ptr<CstBlock> body = parseBlock();
    if (!body) {
        return nullptr;
    }
    return std::make_unique<CstIfStatement>(std::move(cond), std::move(body), CstSpan{begin, index_});
}

std::unique_ptr<CstStatement> Parser::parseStatement() {
    const TokenKind k = peek().kind;

    if (k == TokenKind::KwPrint) {
        return parsePrintStatement();
    }
    if (k == TokenKind::KwWhile) {
        return parseWhileStatement();
    }
    if (k == TokenKind::KwIf) {
        return parseIfStatement();
    }
    if (isTypeKeyword(k)) {
        return parseVarDeclStatement();
    }
    if (k == TokenKind::Identifier) {
        if (peek(1).kind == TokenKind::Equal) {
            return parseAssignStatement();
        }
        diagnostics_.addError(
            "expected `=` after identifier in assignment, or this is not a valid statement start",
            map_.locationAt(peek().offset),
            "Use `x = expr` for assignment, or start other statements with `print`, `while`, `if`, `{`, or a type keyword."
        );
        return nullptr;
    }
    if (k == TokenKind::LeftBrace) {
        const std::size_t begin = index_;
        trace("parse BlockStatement");
        std::unique_ptr<CstBlock> inner = parseBlock();
        if (!inner) {
            return nullptr;
        }
        return std::make_unique<CstBlockStatement>(std::move(inner), CstSpan{begin, index_});
    }

    diagnostics_.addError(
        "unexpected token at start of statement",
        map_.locationAt(peek().offset),
        "Statements may be `print`, assignments, declarations, `while`, `if`, or a nested `{ ... }` block."
    );
    return nullptr;
}

std::unique_ptr<CstStatementList> Parser::parseStatementList() {
    const std::size_t begin = index_;
    trace("parse StatementList");
    auto list = std::make_unique<CstStatementList>();
    while (statementStartsHere()) {
        std::unique_ptr<CstStatement> st = parseStatement();
        if (!st) {
            return nullptr;
        }
        list->addStatement(std::move(st));
    }
    list->setSpan(CstSpan{begin, index_});
    return list;
}

std::unique_ptr<CstBlock> Parser::parseBlock() {
    const std::size_t begin = index_;
    trace("parse Block");
    const Token& openBraceToken = peek();
    if (!expect(TokenKind::LeftBrace, "start of Block")) {
        return nullptr;
    }
    std::unique_ptr<CstStatementList> stmts = parseStatementList();
    if (!stmts) {
        return nullptr;
    }
    if (stmts->statements().empty()) {
        diagnostics_.addHint(
            "this `{ ... }` block contains no statements",
            map_.locationAt(openBraceToken.offset),
            "Empty blocks are valid in the grammar; add statements when you implement this region."
        );
    }
    if (!expect(TokenKind::RightBrace, "end of Block")) {
        return nullptr;
    }
    return std::make_unique<CstBlock>(std::move(stmts), CstSpan{begin, index_});
}

std::unique_ptr<CstProgram> Parser::parseProgram() {
    const std::size_t begin = index_;
    trace("parse Program");
    std::unique_ptr<CstBlock> block = parseBlock();
    if (!block) {
        return nullptr;
    }
    if (!expect(TokenKind::DollarEop, "after Program Block (each program must end with `$`)")) {
        return nullptr;
    }
    return std::make_unique<CstProgram>(std::move(block), CstSpan{begin, index_});
}

std::vector<std::unique_ptr<CstProgram>> Parser::parseTranslationUnit() {
    trace("parse TranslationUnit (zero or more programs)");
    std::vector<std::unique_ptr<CstProgram>> programs;
    while (!isAtEnd()) {
        std::unique_ptr<CstProgram> program = parseProgram();
        if (!program) {
            break;
        }
        programs.push_back(std::move(program));
    }

    if (!programs.empty() && !isAtEnd()) {
        diagnostics_.addError(
            "unexpected tokens after a complete program (expected end of file or another `Block $`)",
            map_.locationAt(peek().offset),
            "Separate programs with `$` and ensure each program is `Block $`. Remove stray tokens after the last program."
        );
    }

    return programs;
}

} // namespace pavic
