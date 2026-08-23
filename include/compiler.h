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
    bool match(TokenType type);
    void consume(TokenType type, const std::string& errMsg);
    void emitConstant(Value value);

    int emitJump(OpCode op);
    void patchJump(int offset);
    void emitLoop(int loopStart);

    void primary();
    void power();
    void unary();
    void factor();
    void term();
    void relational();
    void equality();
    void andExpression();
    void orExpression();
    void expression();

    void varDeclaration();
    void blockStatement();
    void ifStatement();
    void whileStatement();
    void statement();

public:
    Compiler(const std::string& src, Chunk& targetChunk);
    bool compile();
};
