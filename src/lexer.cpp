#include "lexer.h"
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <limits>

Lexer::Lexer(std::string src) : source(std::move(src)) {}

char Lexer::peek() {
    if (current >= source.length()) return '\0';
    return source[current];
}

char Lexer::peekNext() {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

char Lexer::advance() {
    char c = source[current++];
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() != expected) return false;
    advance();
    return true;
}

Token Lexer::errorToken(const std::string& message, int tokenLine, int tokenCol) {
    Token t;
    t.type = TokenType::ERROR;
    t.text = message;
    t.line = tokenLine;
    t.column = tokenCol;
    return t;
}

void Lexer::skipWhitespace() {
    while (current < source.length()) {
        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else if (c == '/' && current + 1 < source.length() && source[current + 1] == '/') {
            // Single line comment
            while (current < source.length() && peek() != '\n') {
                advance();
            }
        } else if (c == '/' && current + 1 < source.length() && source[current + 1] == '*') {
            // Multi-line comment
            advance(); // '/'
            advance(); // '*'
            bool closed = false;
            while (current < source.length()) {
                if (peek() == '*' && current + 1 < source.length() && source[current + 1] == '/') {
                    advance(); // '*'
                    advance(); // '/'
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

std::string Lexer::getLineString(int targetLine) const {
    std::stringstream ss(source);
    std::string lineStr;
    int currentLine = 1;
    while (std::getline(ss, lineStr)) {
        if (currentLine == targetLine) {
            return lineStr;
        }
        currentLine++;
    }
    return "";
}

Token Lexer::nextToken() {
    int startLine = line;
    int startCol = column;

    if (unclosedComment) {
        unclosedComment = false;
        return errorToken("Unclosed block comment", startLine, startCol);
    }
    skipWhitespace();
    startLine = line;
    startCol = column;

    if (unclosedComment) {
        unclosedComment = false;
        return errorToken("Unclosed block comment", startLine, startCol);
    }
    if (current >= source.length()) {
        Token t;
        t.type = TokenType::END_OF_FILE;
        t.text = "";
        t.line = line;
        t.column = column;
        return t;
    }

    char c = peek();

    // Single quotes -> char literal
    if (c == '\'') {
        size_t tokenStart = current;
        advance(); // consume opening '\''
        if (peek() == '\'') {
            advance(); // consume closing '\''
            return errorToken("Character literal cannot be empty", startLine, startCol);
        }
        if (peek() == '\0' || peek() == '\n') {
            return errorToken("Unterminated character literal", startLine, startCol);
        }
        char32_t charCode = 0;
        if (peek() == '\\') {
            advance(); // consume '\\'
            if (peek() == '\0' || peek() == '\n') {
                return errorToken("Unterminated character literal", startLine, startCol);
            }
            char esc = advance();
            switch (esc) {
                case 'n': charCode = '\n'; break;
                case 'r': charCode = '\r'; break;
                case 't': charCode = '\t'; break;
                case '\\': charCode = '\\'; break;
                case '\'': charCode = '\''; break;
                case '"': charCode = '"'; break;
                case '0': charCode = '\0'; break;
                default:
                    while (peek() != '\'' && peek() != '\n' && peek() != '\0') advance();
                    if (peek() == '\'') advance();
                    return errorToken("Invalid escape sequence '\\" + std::string(1, esc) + "' in character literal", startLine, startCol);
            }
        } else {
            // UTF-8 sequence read
            unsigned char ch = static_cast<unsigned char>(advance());
            if (ch < 0x80) {
                charCode = ch;
            } else if ((ch & 0xE0) == 0xC0) {
                if (current >= source.length()) {
                    return errorToken("Unterminated character literal", startLine, startCol);
                }
                char ch2 = advance();
                charCode = ((ch & 0x1F) << 6) | (ch2 & 0x3F);
            } else if ((ch & 0xF0) == 0xE0) {
                if (current + 1 >= source.length()) {
                    return errorToken("Unterminated character literal", startLine, startCol);
                }
                char ch2 = advance();
                char ch3 = advance();
                charCode = ((ch & 0x0F) << 12) | ((ch2 & 0x3F) << 6) | (ch3 & 0x3F);
            } else if ((ch & 0xF8) == 0xF0) {
                if (current + 2 >= source.length()) {
                    return errorToken("Unterminated character literal", startLine, startCol);
                }
                char ch2 = advance();
                char ch3 = advance();
                char ch4 = advance();
                charCode = ((ch & 0x07) << 18) | ((ch2 & 0x3F) << 12) | ((ch3 & 0x3F) << 6) | (ch4 & 0x3F);
            } else {
                while (peek() != '\'' && peek() != '\n' && peek() != '\0') advance();
                if (peek() == '\'') advance();
                return errorToken("Invalid UTF-8 sequence in character literal", startLine, startCol);
            }
        }

        if (peek() != '\'') {
            while (peek() != '\'' && peek() != '\n' && peek() != '\0') advance();
            if (peek() == '\'') {
                advance(); // consume closing quote
                return errorToken("Character literal must contain exactly one character", startLine, startCol);
            }
            return errorToken("Unterminated character literal", startLine, startCol);
        }
        advance(); // consume ending '\''

        Token t;
        t.type = TokenType::CHAR_LITERAL;
        t.text = source.substr(tokenStart, current - tokenStart);
        t.charValue = charCode;
        t.line = startLine;
        t.column = startCol;
        return t;
    }

    // Double quotes -> string literal
    if (c == '"') {
        advance(); // consume '"'
        std::string strContent;
        while (peek() != '"' && peek() != '\0') {
            if (peek() == '\\') {
                advance();
                char esc = advance();
                switch (esc) {
                    case 'n': strContent += '\n'; break;
                    case 'r': strContent += '\r'; break;
                    case 't': strContent += '\t'; break;
                    case '\\': strContent += '\\'; break;
                    case '"': strContent += '"'; break;
                    case '\'': strContent += '\''; break;
                    case '0': strContent += '\0'; break;
                    default: strContent += esc; break;
                }
            } else {
                strContent += advance();
            }
        }
        if (peek() == '\0') {
            return errorToken("Unterminated string", startLine, startCol);
        }
        advance(); // consume closing '"'
        Token t;
        t.type = TokenType::STRING_LITERAL;
        t.text = "\"" + strContent + "\"";
        t.strValue = strContent;
        t.line = startLine;
        t.column = startCol;
        return t;
    }

    // Numeric literals & standalone dot handling
    if (std::isdigit(c) || (c == '.' && std::isdigit(peekNext()))) {
        size_t start = current;
        int dotCount = 0;
        bool hasDigits = false;

        while (std::isdigit(peek()) || peek() == '.') {
            if (peek() == '.') {
                dotCount++;
            } else {
                hasDigits = true;
            }
            advance();
        }

        std::string numStr = source.substr(start, current - start);

        if (dotCount > 1 || !hasDigits) {
            return errorToken("Malformed numeric literal '" + numStr + "'", startLine, startCol);
        }

        Token t;
        t.line = startLine;
        t.column = startCol;
        t.text = numStr;

        if (dotCount == 1) {
            try {
                size_t pos = 0;
                t.floatValue = std::stod(numStr, &pos);
                if (pos != numStr.length()) {
                    return errorToken("Malformed floating point literal '" + numStr + "'", startLine, startCol);
                }
                t.type = TokenType::FLOAT_LITERAL;
            } catch (...) {
                return errorToken("Floating point literal out of range '" + numStr + "'", startLine, startCol);
            }
        } else {
            try {
                size_t pos = 0;
                t.intValue = std::stoll(numStr, &pos, 10);
                if (pos != numStr.length()) {
                    return errorToken("Malformed integer literal '" + numStr + "'", startLine, startCol);
                }
                t.type = TokenType::INT_LITERAL;
            } catch (...) {
                return errorToken("Integer literal out of 64-bit range '" + numStr + "'", startLine, startCol);
            }
        }
        return t;
    }

    // Identifiers & Keywords
    if (std::isalpha(c) || c == '_') {
        size_t start = current;
        while (std::isalnum(peek()) || peek() == '_') advance();
        std::string ident = source.substr(start, current - start);

        Token t;
        t.line = startLine;
        t.column = startCol;
        t.text = ident;

        if (ident == "echo") t.type = TokenType::ECHO;
        else if (ident == "let") t.type = TokenType::LET;
        else if (ident == "const") t.type = TokenType::CONST;
        else if (ident == "true") t.type = TokenType::TRUE;
        else if (ident == "false") t.type = TokenType::FALSE;
        else if (ident == "nil") t.type = TokenType::NIL;
        else if (ident == "and") t.type = TokenType::AND;
        else if (ident == "or") t.type = TokenType::OR;
        else if (ident == "not") t.type = TokenType::NOT;
        else if (ident == "if") t.type = TokenType::IF;
        else if (ident == "else") t.type = TokenType::ELSE;
        else if (ident == "while") t.type = TokenType::WHILE;
        else if (ident == "for") t.type = TokenType::FOR;
        else if (ident == "task") t.type = TokenType::TASK;
        else if (ident == "give") t.type = TokenType::GIVE;
        else if (ident == "halt") t.type = TokenType::HALT;
        else if (ident == "skip") t.type = TokenType::SKIP;
        else if (ident == "grab") t.type = TokenType::GRAB;
        else if (ident == "as") t.type = TokenType::AS;
        else if (ident == "pub") t.type = TokenType::PUB;
        else if (ident == "priv") t.type = TokenType::PRIV;
        else if (ident == "int") t.type = TokenType::TYPE_INT;
        else if (ident == "float") t.type = TokenType::TYPE_FLOAT;
        else if (ident == "string") t.type = TokenType::TYPE_STRING;
        else if (ident == "bool") t.type = TokenType::TYPE_BOOL;
        else if (ident == "char") t.type = TokenType::TYPE_CHAR;
        else if (ident == "array") t.type = TokenType::TYPE_ARRAY;
        else if (ident == "map") t.type = TokenType::TYPE_MAP;
        else if (ident == "func") t.type = TokenType::TYPE_FUNC;
        else t.type = TokenType::IDENTIFIER;

        return t;
    }

    advance(); // consume character for operators/punctuation
    Token t;
    t.line = startLine;
    t.column = startCol;
    t.text = std::string(1, c);

    switch (c) {
        case '+':
            if (match('+')) { t.type = TokenType::PLUS_PLUS; t.text = "++"; }
            else if (match('=')) { t.type = TokenType::PLUS_EQUAL; t.text = "+="; }
            else { t.type = TokenType::PLUS; }
            break;
        case '-':
            if (match('-')) { t.type = TokenType::MINUS_MINUS; t.text = "--"; }
            else if (match('=')) { t.type = TokenType::MINUS_EQUAL; t.text = "-="; }
            else { t.type = TokenType::MINUS; }
            break;
        case '*':
            if (match('=')) { t.type = TokenType::STAR_EQUAL; t.text = "*="; }
            else { t.type = TokenType::STAR; }
            break;
        case '/':
            if (match('=')) { t.type = TokenType::SLASH_EQUAL; t.text = "/="; }
            else { t.type = TokenType::SLASH; }
            break;
        case '%':
            if (match('=')) { t.type = TokenType::PERCENT_EQUAL; t.text = "%="; }
            else { t.type = TokenType::PERCENT; }
            break;
        case '^': t.type = TokenType::CARET; break;
        case '(': t.type = TokenType::LPAREN; break;
        case ')': t.type = TokenType::RPAREN; break;
        case '{': t.type = TokenType::LBRACE; break;
        case '}': t.type = TokenType::RBRACE; break;
        case '[': t.type = TokenType::LBRACKET; break;
        case ']': t.type = TokenType::RBRACKET; break;
        case ',': t.type = TokenType::COMMA; break;
        case ':': t.type = TokenType::COLON; break;
        case '.': t.type = TokenType::DOT; break;
        case '~': t.type = TokenType::TILDE; break;
        case '=':
            if (match('=')) { t.type = TokenType::EQUAL_EQUAL; t.text = "=="; }
            else { t.type = TokenType::EQUAL; }
            break;
        case '!':
            if (match('=')) { t.type = TokenType::BANG_EQUAL; t.text = "!="; }
            else { t.type = TokenType::BANG; }
            break;
        case '<':
            if (match('=')) { t.type = TokenType::LESS_EQUAL; t.text = "<="; }
            else { t.type = TokenType::LESS; }
            break;
        case '>':
            if (match('=')) { t.type = TokenType::GREATER_EQUAL; t.text = ">="; }
            else { t.type = TokenType::GREATER; }
            break;
        default:
            return errorToken("Unexpected character '" + std::string(1, c) + "'", startLine, startCol);
    }

    return t;
}
