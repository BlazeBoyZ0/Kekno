#pragma once
#include <string>
#include "lexer.h"
#include "chunk.h"

class Compiler {
private:
    Lexer lexer;
    Token current;
    Chunk& chunk;
    bool hasError = false;

    void advance();
    void consume(TokenType type, const std::string& errMsg);
    void emitConstant(Value value);
    void factor();
    void term();
    void expression();
    void varDeclaration();
    void statement();

public:
    Compiler(const std::string& src, Chunk& targetChunk);
    bool compile();
};
