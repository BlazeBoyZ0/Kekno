#pragma once
#include <string>

enum class TokenType {
    NUMBER, STRING, IDENTIFIER,
    PLUS, MINUS, STAR, SLASH, PERCENT, CARET, EQUAL,
    EQUAL_EQUAL, BANG_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
    BANG,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, COMMA, ECHO, LET, TILDE,
    TRUE, FALSE, NIL, AND, OR, NOT, IF, ELSE, WHILE,
    TASK, GIVE,
    HALT, SKIP, GRAB, COLON,
    END_OF_FILE, ERROR
};

struct Token {
    TokenType type;
    std::string text;
    double numValue = 0.0;
    std::string strValue = "";
};
