#pragma once
#include <string>

#include <cstdint>
#include <string>

enum class TokenType {
    INT_LITERAL, FLOAT_LITERAL, STRING_LITERAL, CHAR_LITERAL, IDENTIFIER,
    PLUS, MINUS, STAR, SLASH, PERCENT, CARET, EQUAL,
    PLUS_PLUS, MINUS_MINUS,
    PLUS_EQUAL, MINUS_EQUAL, STAR_EQUAL, SLASH_EQUAL, PERCENT_EQUAL,
    EQUAL_EQUAL, BANG_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
    BANG,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, COMMA, ECHO, LET, CONST, TILDE,
    TRUE, FALSE, NIL, AND, OR, NOT, IF, ELSE, WHILE, FOR,
    TASK, GIVE, BUILD, OPERATOR, SELF,
    HALT, SKIP, GRAB, COLON, AS, PUB, PRIV, DOT, DOT_DOT_DOT,
    TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_BOOL, TYPE_CHAR, TYPE_ARRAY, TYPE_MAP, TYPE_FUNC,
    END_OF_FILE, ERROR
};

struct Token {
    TokenType type;
    std::string text;
    int64_t intValue = 0;
    double floatValue = 0.0;
    std::string strValue = "";
    char32_t charValue = 0;
    int line = 1;
    int column = 1;
};
