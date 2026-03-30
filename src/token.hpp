#pragma once

#include <cstddef>
#include <string_view>

namespace pavic {

enum class TokenKind {
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Plus,
    Equal,
    EqualEqual,
    BangEqual,
    KwPrint,
    KwWhile,
    KwIf,
    KwInt,
    KwString,
    KwBoolean,
    KwTrue,
    KwFalse,
    Identifier,
    IntegerLiteral,
    StringLiteral,
    DollarEop,
    EndOfFile,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string_view lexeme{};
    std::size_t offset = 0;
};

std::string_view tokenKindName(TokenKind kind);

} // namespace pavic
