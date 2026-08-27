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

bool Lexer::match(char expected) {
    if (peek() != expected) return false;
    current++;
    return true;
}

void Lexer::skipWhitespace() {
    while (current < source.length()) {
        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else if (c == '/' && current + 1 < source.length() && source[current + 1] == '/') {
            // Single line comment: skip until newline or EOF
            while (current < source.length() && peek() != '\n') {
                advance();
            }
        } else if (c == '/' && current + 1 < source.length() && source[current + 1] == '*') {
            // Multi-line comment: skip until */ or EOF
            advance(); // consume '/'
            advance(); // consume '*'
            bool closed = false;
            while (current < source.length()) {
                if (peek() == '*' && current + 1 < source.length() && source[current + 1] == '/') {
                    advance(); // consume '*'
                    advance(); // consume '/'
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                unclosedComment = true;
                break;
            }
        } else {
            break;
        }
    }
}

Token Lexer::nextToken() {
    if (unclosedComment) {
        unclosedComment = false;
        return {TokenType::ERROR, "Unclosed block comment", 0.0, ""};
    }
    skipWhitespace();
    if (unclosedComment) {
        unclosedComment = false;
        return {TokenType::ERROR, "Unclosed block comment", 0.0, ""};
    }
    if (current >= source.length()) return {TokenType::END_OF_FILE, "", 0.0, ""};

    char c = advance();
    switch (c) {
        case '+': return {TokenType::PLUS, "+", 0.0, ""};
        case '-': return {TokenType::MINUS, "-", 0.0, ""};
        case '*': return {TokenType::STAR, "*", 0.0, ""};
        case '/': return {TokenType::SLASH, "/", 0.0, ""};
        case '%': return {TokenType::PERCENT, "%", 0.0, ""};
        case '^': return {TokenType::CARET, "^", 0.0, ""};
        case '(': return {TokenType::LPAREN, "(", 0.0, ""};
        case ')': return {TokenType::RPAREN, ")", 0.0, ""};
        case '{': return {TokenType::LBRACE, "{", 0.0, ""};
        case '}': return {TokenType::RBRACE, "}", 0.0, ""};
        case '[': return {TokenType::LBRACKET, "[", 0.0, ""};
        case ']': return {TokenType::RBRACKET, "]", 0.0, ""};
        case ',': return {TokenType::COMMA, ",", 0.0, ""};
        case ':': return {TokenType::COLON, ":", 0.0, ""};
        case '~': return {TokenType::TILDE, "~", 0.0, ""};
        case '=':
            if (match('=')) return {TokenType::EQUAL_EQUAL, "==", 0.0, ""};
            return {TokenType::EQUAL, "=", 0.0, ""};
        case '!':
            if (match('=')) return {TokenType::BANG_EQUAL, "!=", 0.0, ""};
            return {TokenType::BANG, "!", 0.0, ""};
        case '<':
            if (match('=')) return {TokenType::LESS_EQUAL, "<=", 0.0, ""};
            return {TokenType::LESS, "<", 0.0, ""};
        case '>':
            if (match('=')) return {TokenType::GREATER_EQUAL, ">=", 0.0, ""};
            return {TokenType::GREATER, ">", 0.0, ""};
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
        if (ident == "echo") return {TokenType::ECHO, ident, 0.0, ""};
        if (ident == "let") return {TokenType::LET, ident, 0.0, ""};
        if (ident == "true") return {TokenType::TRUE, ident, 0.0, ""};
        if (ident == "false") return {TokenType::FALSE, ident, 0.0, ""};
        if (ident == "nil") return {TokenType::NIL, ident, 0.0, ""};
        if (ident == "and") return {TokenType::AND, ident, 0.0, ""};
        if (ident == "or") return {TokenType::OR, ident, 0.0, ""};
        if (ident == "not") return {TokenType::NOT, ident, 0.0, ""};
        if (ident == "if") return {TokenType::IF, ident, 0.0, ""};
        if (ident == "else") return {TokenType::ELSE, ident, 0.0, ""};
        if (ident == "while") return {TokenType::WHILE, ident, 0.0, ""};
        if (ident == "task") return {TokenType::TASK, ident, 0.0, ""};
        if (ident == "give") return {TokenType::GIVE, ident, 0.0, ""};
        if (ident == "halt") return {TokenType::HALT, ident, 0.0, ""};
        if (ident == "skip") return {TokenType::SKIP, ident, 0.0, ""};
        if (ident == "grab") return {TokenType::GRAB, ident, 0.0, ""};
        return {TokenType::IDENTIFIER, ident, 0.0, ""};
    }

    return {TokenType::ERROR, std::string(1, c), 0.0, ""};
}
