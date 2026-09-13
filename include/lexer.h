#pragma once
#include <string>
#include "token.h"

class Lexer {
private:
    std::string source;
    size_t current = 0;
    int line = 1;
    int column = 1;
    bool unclosedComment = false;

    char peek();
    char peekNext();
    char advance();
    bool match(char expected);
    void skipWhitespace();
    Token errorToken(const std::string& message, int tokenLine, int tokenCol);

public:
    Lexer(std::string src);
    Token nextToken();
    std::string getLineString(int targetLine) const;
};
