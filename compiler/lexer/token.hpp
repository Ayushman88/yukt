#pragma once

#include "common/source.hpp"

#include <string>
#include <string_view>

namespace yukt {

enum class TokenKind {
    EndOfFile,
    Newline,
    Indent,
    Dedent,

    Identifier,
    Integer,
    Float,
    String,

    KwCreate,
    KwSay,
    KwIf,
    KwOtherwise,
    KwRepeat,
    KwIn,
    KwGive,
    KwTrue,
    KwFalse,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    EqualEqual,
    BangEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Equal,

    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Colon,
    DotDot,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string lexeme;
    SourceLocation location;
};

inline constexpr std::string_view token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::EndOfFile:
            return "EOF";
        case TokenKind::Newline:
            return "NEWLINE";
        case TokenKind::Indent:
            return "INDENT";
        case TokenKind::Dedent:
            return "DEDENT";
        case TokenKind::Identifier:
            return "IDENTIFIER";
        case TokenKind::Integer:
            return "INTEGER";
        case TokenKind::Float:
            return "FLOAT";
        case TokenKind::String:
            return "STRING";
        case TokenKind::KwCreate:
            return "CREATE";
        case TokenKind::KwSay:
            return "SAY";
        case TokenKind::KwIf:
            return "IF";
        case TokenKind::KwOtherwise:
            return "OTHERWISE";
        case TokenKind::KwRepeat:
            return "REPEAT";
        case TokenKind::KwIn:
            return "IN";
        case TokenKind::KwGive:
            return "GIVE";
        case TokenKind::KwTrue:
            return "TRUE";
        case TokenKind::KwFalse:
            return "FALSE";
        case TokenKind::Plus:
            return "PLUS";
        case TokenKind::Minus:
            return "MINUS";
        case TokenKind::Star:
            return "STAR";
        case TokenKind::Slash:
            return "SLASH";
        case TokenKind::Percent:
            return "PERCENT";
        case TokenKind::EqualEqual:
            return "EQUAL_EQUAL";
        case TokenKind::BangEqual:
            return "BANG_EQUAL";
        case TokenKind::Less:
            return "LESS";
        case TokenKind::Greater:
            return "GREATER";
        case TokenKind::LessEqual:
            return "LESS_EQUAL";
        case TokenKind::GreaterEqual:
            return "GREATER_EQUAL";
        case TokenKind::Equal:
            return "EQUAL";
        case TokenKind::LParen:
            return "LPAREN";
        case TokenKind::RParen:
            return "RPAREN";
        case TokenKind::LBracket:
            return "LBRACKET";
        case TokenKind::RBracket:
            return "RBRACKET";
        case TokenKind::Comma:
            return "COMMA";
        case TokenKind::Colon:
            return "COLON";
        case TokenKind::DotDot:
            return "DOT_DOT";
    }
    return "UNKNOWN";
}

inline constexpr std::string_view token_kind_spelling(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus:
            return "+";
        case TokenKind::Minus:
            return "-";
        case TokenKind::Star:
            return "*";
        case TokenKind::Slash:
            return "/";
        case TokenKind::Percent:
            return "%";
        case TokenKind::EqualEqual:
            return "==";
        case TokenKind::BangEqual:
            return "!=";
        case TokenKind::Less:
            return "<";
        case TokenKind::Greater:
            return ">";
        case TokenKind::LessEqual:
            return "<=";
        case TokenKind::GreaterEqual:
            return ">=";
        case TokenKind::Equal:
            return "=";
        case TokenKind::LParen:
            return "(";
        case TokenKind::RParen:
            return ")";
        case TokenKind::LBracket:
            return "[";
        case TokenKind::RBracket:
            return "]";
        case TokenKind::Comma:
            return ",";
        case TokenKind::Colon:
            return ":";
        case TokenKind::DotDot:
            return "..";
        default:
            return token_kind_name(kind);
    }
}

inline constexpr bool is_keyword(TokenKind kind) {
    return kind == TokenKind::KwCreate || kind == TokenKind::KwSay || kind == TokenKind::KwIf ||
           kind == TokenKind::KwOtherwise || kind == TokenKind::KwRepeat || kind == TokenKind::KwIn ||
           kind == TokenKind::KwGive || kind == TokenKind::KwTrue || kind == TokenKind::KwFalse;
}

}  // namespace yukt
