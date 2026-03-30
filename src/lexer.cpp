#include "lexer.hpp"

#include <iostream>
#include <string>

namespace {

bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

} // namespace

namespace pavic {

Lexer::Lexer(const SourceMap& map, DiagnosticBag& diagnostics, bool verbose)
    : map_(map), diagnostics_(diagnostics), verbose_(verbose), text_(map.text()) {}

void Lexer::traceStage(int stage, const std::string& message) const {
    if (!verbose_) {
        return;
    }

    std::cout << "[lex] stage " << stage << "/" << kNumLexTraceStages << ": " << message << "\n";
}

void Lexer::traceLexSummary(const std::vector<Token>& tokens) const {
    if (!verbose_) {
        return;
    }

    std::size_t errorCount = 0;
    std::size_t warningCount = 0;
    for (const auto& diagnostic : diagnostics_.all()) {
        if (diagnostic.kind == DiagnosticKind::Error) {
            ++errorCount;
        } else {
            ++warningCount;
        }
    }

    std::cout << "[lex] stage " << kNumLexTraceStages << "/" << kNumLexTraceStages
              << ": lex session complete — tokens=" << tokens.size()
              << " (including EOF), diagnostics: " << errorCount << " error(s), " << warningCount
              << " warning(s)\n";
}

void Lexer::traceToken(const Token& token) const {
    if (!verbose_) {
        return;
    }

    const SourceLocation location = map_.locationAt(token.offset);
    std::cout << "[lex] token: " << location.line << ":" << location.column << ": " << tokenKindName(token.kind);
    if (!token.lexeme.empty()) {
        std::cout << " `" << token.lexeme << "`";
    }
    std::cout << "\n";
}

void Lexer::emitToken(std::vector<Token>& tokens, TokenKind kind, std::size_t start, std::size_t end) {
    Token token{kind, text_.substr(start, end - start), start};
    tokens.push_back(token);
    traceToken(token);
}

void Lexer::skipWhitespace() {
    while (pos_ < text_.size() && isSpace(text_[pos_])) {
        ++pos_;
    }
}

bool Lexer::trySkipBlockComment() {
    if (pos_ + 1 >= text_.size() || text_[pos_] != '/' || text_[pos_ + 1] != '*') {
        return false;
    }

    const std::size_t start = pos_;
    pos_ += 2;

    while (pos_ < text_.size()) {
        if (text_[pos_] == '*' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
            pos_ += 2;
            return true;
        }
        ++pos_;
    }

    diagnostics_.addError(
        "unterminated block comment starting with `/*`",
        map_.locationAt(start),
        "Close the comment with `*/` before the end of the file."
    );
    return true;
}

void Lexer::lexString(std::vector<Token>& tokens) {
    const std::size_t start = pos_;
    ++pos_; // opening "

    while (pos_ < text_.size()) {
        const char ch = text_[pos_];
        if (ch == '"') {
            ++pos_;
            emitToken(tokens, TokenKind::StringLiteral, start, pos_);
            return;
        }
        if (ch == '\n' || ch == '\r') {
            diagnostics_.addError(
                "newline character is not allowed inside a string literal",
                map_.locationAt(pos_),
                "Close the string with `\"` before starting a new line, or remove the line break."
            );
            emitToken(tokens, TokenKind::StringLiteral, start, pos_);
            return;
        }
        ++pos_;
    }

    diagnostics_.addError(
        "unterminated string literal starting with `\"`",
        map_.locationAt(start),
        "Add a closing `\"` before the end of the file."
    );
    emitToken(tokens, TokenKind::StringLiteral, start, pos_);
}

void Lexer::lexNumber(std::vector<Token>& tokens) {
    const std::size_t start = pos_;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
    }
    emitToken(tokens, TokenKind::IntegerLiteral, start, pos_);
}

void Lexer::lexWord(std::vector<Token>& tokens) {
    const std::size_t start = pos_;
    while (pos_ < text_.size() && text_[pos_] >= 'a' && text_[pos_] <= 'z') {
        ++pos_;
    }

    const std::string_view word = text_.substr(start, pos_ - start);

    if (word == "print") {
        emitToken(tokens, TokenKind::KwPrint, start, pos_);
        return;
    }
    if (word == "while") {
        emitToken(tokens, TokenKind::KwWhile, start, pos_);
        return;
    }
    if (word == "if") {
        emitToken(tokens, TokenKind::KwIf, start, pos_);
        return;
    }
    if (word == "int") {
        emitToken(tokens, TokenKind::KwInt, start, pos_);
        return;
    }
    if (word == "string") {
        emitToken(tokens, TokenKind::KwString, start, pos_);
        return;
    }
    if (word == "boolean") {
        emitToken(tokens, TokenKind::KwBoolean, start, pos_);
        return;
    }
    if (word == "true") {
        emitToken(tokens, TokenKind::KwTrue, start, pos_);
        return;
    }
    if (word == "false") {
        emitToken(tokens, TokenKind::KwFalse, start, pos_);
        return;
    }

    if (word.size() == 1) {
        emitToken(tokens, TokenKind::Identifier, start, pos_);
        return;
    }

    diagnostics_.addError(
        "invalid identifier or unknown keyword",
        map_.locationAt(start),
        "Identifiers must be a single lowercase letter `a` through `z`. Reserved words like `print` and `int` must match exactly."
    );
    emitToken(tokens, TokenKind::Identifier, start, pos_);
}

void Lexer::warnTrailingEop() {
    std::size_t end = text_.size();
    while (end > 0 && isSpace(text_[end - 1])) {
        --end;
    }

    if (end == 0) {
        return;
    }

    if (text_[end - 1] != '$') {
        diagnostics_.addWarning(
            "input does not end with `$` (end-of-program marker)",
            map_.locationAt(end),
            "The grammar requires each program to end with `$`. Append `$` after the final `}` block."
        );
    }
}

void Lexer::lexAll(std::vector<Token>& tokens) {
    tokens.clear();
    pos_ = 0;

    traceStage(
        1,
        "begin lexing session — source bytes=" + std::to_string(map_.byteLength()) + ", lines=" +
            std::to_string(map_.lineCount())
    );
    traceStage(2, "cursor initialized at byte offset 0");
    traceStage(3, "entering main scan loop (whitespace, block comments, then tokens)");
    traceStage(4, "recognizing tokens (each following line is one token)");

    while (pos_ < text_.size()) {
        skipWhitespace();
        if (pos_ >= text_.size()) {
            break;
        }

        if (trySkipBlockComment()) {
            continue;
        }

        const char ch = text_[pos_];

        if (ch == '"') {
            lexString(tokens);
            continue;
        }

        if (ch >= '0' && ch <= '9') {
            lexNumber(tokens);
            continue;
        }

        if (ch >= 'a' && ch <= 'z') {
            lexWord(tokens);
            continue;
        }

        if (ch == '(') {
            emitToken(tokens, TokenKind::LeftParen, pos_, pos_ + 1);
            ++pos_;
            continue;
        }
        if (ch == ')') {
            emitToken(tokens, TokenKind::RightParen, pos_, pos_ + 1);
            ++pos_;
            continue;
        }
        if (ch == '{') {
            emitToken(tokens, TokenKind::LeftBrace, pos_, pos_ + 1);
            ++pos_;
            continue;
        }
        if (ch == '}') {
            emitToken(tokens, TokenKind::RightBrace, pos_, pos_ + 1);
            ++pos_;
            continue;
        }
        if (ch == '+') {
            emitToken(tokens, TokenKind::Plus, pos_, pos_ + 1);
            ++pos_;
            continue;
        }
        if (ch == '$') {
            emitToken(tokens, TokenKind::DollarEop, pos_, pos_ + 1);
            ++pos_;
            continue;
        }

        if (ch == '=') {
            if (pos_ + 1 < text_.size() && text_[pos_ + 1] == '=') {
                emitToken(tokens, TokenKind::EqualEqual, pos_, pos_ + 2);
                pos_ += 2;
            } else {
                emitToken(tokens, TokenKind::Equal, pos_, pos_ + 1);
                ++pos_;
            }
            continue;
        }

        if (ch == '!') {
            if (pos_ + 1 < text_.size() && text_[pos_ + 1] == '=') {
                emitToken(tokens, TokenKind::BangEqual, pos_, pos_ + 2);
                pos_ += 2;
            } else {
                diagnostics_.addError(
                    "unexpected `!` — `!=` is the only operator that starts with `!`",
                    map_.locationAt(pos_),
                    "Use `!=` for inequality, or remove the stray `!`."
                );
                ++pos_;
            }
            continue;
        }

        if (ch == '/') {
            diagnostics_.addError(
                "stray `/` — comments must begin with `/*`",
                map_.locationAt(pos_),
                "Start a block comment with `/*` or remove the `/`."
            );
            ++pos_;
            continue;
        }

        diagnostics_.addError(
            "unexpected character in source text",
            map_.locationAt(pos_),
            "Remove or replace the character so it matches the language alphabet (letters, digits, symbols listed in the assignment)."
        );
        ++pos_;
    }

    traceStage(5, "emitting EOF sentinel token");
    Token eofToken{TokenKind::EndOfFile, std::string_view{}, pos_};
    tokens.push_back(eofToken);
    traceToken(eofToken);

    traceStage(6, "checking trailing end-of-program marker ($)");
    if (!diagnostics_.hasErrors()) {
        warnTrailingEop();
    }

    traceLexSummary(tokens);
}

} // namespace pavic
