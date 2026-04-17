#include "parser.hpp"

#include <iostream>
#include <sstream>

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

} // namespace pavic
