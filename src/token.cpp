#include "token.hpp"

namespace pavic {

std::string_view tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::LeftParen:
        return "LeftParen";
    case TokenKind::RightParen:
        return "RightParen";
    case TokenKind::LeftBrace:
        return "LeftBrace";
    case TokenKind::RightBrace:
        return "RightBrace";
    case TokenKind::Plus:
        return "Plus";
    case TokenKind::Equal:
        return "Equal";
    case TokenKind::EqualEqual:
        return "EqualEqual";
    case TokenKind::BangEqual:
        return "BangEqual";
    case TokenKind::KwPrint:
        return "KwPrint";
    case TokenKind::KwWhile:
        return "KwWhile";
    case TokenKind::KwIf:
        return "KwIf";
    case TokenKind::KwInt:
        return "KwInt";
    case TokenKind::KwString:
        return "KwString";
    case TokenKind::KwBoolean:
        return "KwBoolean";
    case TokenKind::KwTrue:
        return "KwTrue";
    case TokenKind::KwFalse:
        return "KwFalse";
    case TokenKind::Identifier:
        return "Identifier";
    case TokenKind::IntegerLiteral:
        return "IntegerLiteral";
    case TokenKind::StringLiteral:
        return "StringLiteral";
    case TokenKind::DollarEop:
        return "DollarEop";
    case TokenKind::EndOfFile:
        return "EndOfFile";
    }
    return "Unknown";
}

} // namespace pavic
