#pragma once
#include <string>
#include "token.h"

class Lexer {
private:
    std::string source;
    size_t current = 0;

    char peek();
    char advance();
    bool match(char expected);
    void skipWhitespace();

public:
    Lexer(std::string src);
    Token nextToken();
};
