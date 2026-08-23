#pragma once
#include <string>

enum class TokenType {
    NUMBER, STRING, IDENTIFIER,
    PLUS, MINUS, STAR, SLASH, EQUAL,
    LPAREN, RPAREN, PRINT, LET, TILDE,
    END_OF_FILE, ERROR
};

struct Token {
    TokenType type;
    std::string text;
    double numValue = 0.0;
    std::string strValue = "";
};
