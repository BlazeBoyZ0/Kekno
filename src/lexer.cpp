#include "lexer.h"
#include <cctype>

Lexer::Lexer(std::string src) : source(std::move(src)) {}

char Lexer::peek() {
    if (current >= source.length()) return '\0';
    return source[current];
}

char Lexer::advance() {
    return source[current++];
}

void Lexer::skipWhitespace() {
    while (current < source.length() && std::isspace(peek())) {
        advance();
    }
}

Token Lexer::nextToken() {
    skipWhitespace();
    if (current >= source.length()) return {TokenType::END_OF_FILE, "", 0.0, ""};

    char c = advance();
    switch (c) {
        case '+': return {TokenType::PLUS, "+", 0.0, ""};
        case '-': return {TokenType::MINUS, "-", 0.0, ""};
        case '*': return {TokenType::STAR, "*", 0.0, ""};
        case '/': return {TokenType::SLASH, "/", 0.0, ""};
        case '=': return {TokenType::EQUAL, "=", 0.0, ""};
        case '(': return {TokenType::LPAREN, "(", 0.0, ""};
        case ')': return {TokenType::RPAREN, ")", 0.0, ""};
        case '~': return {TokenType::TILDE, "~", 0.0, ""};
    }

    if (c == '"' || c == '\'') {
        char quote = c;
        size_t start = current;
        while (peek() != quote && peek() != '\0') advance();
        if (peek() == '\0') return {TokenType::ERROR, "Unterminated string", 0.0, ""};
        std::string str = source.substr(start, current - start);
        advance();
        return {TokenType::STRING, str, 0.0, str};
    }

    if (std::isdigit(c) || c == '.') {
        size_t start = current - 1;
        while (std::isdigit(peek()) || peek() == '.') advance();
        std::string numStr = source.substr(start, current - start);
        return {TokenType::NUMBER, numStr, std::stod(numStr), ""};
    }

    if (std::isalpha(c) || c == '_') {
        size_t start = current - 1;
        while (std::isalnum(peek()) || peek() == '_') advance();
        std::string ident = source.substr(start, current - start);
        if (ident == "print") return {TokenType::PRINT, ident, 0.0, ""};
        if (ident == "let") return {TokenType::LET, ident, 0.0, ""};
        return {TokenType::IDENTIFIER, ident, 0.0, ""};
    }

    return {TokenType::ERROR, std::string(1, c), 0.0, ""};
}
