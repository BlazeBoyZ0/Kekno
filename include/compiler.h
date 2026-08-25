#pragma once
#include <string>
#include <vector>
#include <memory>
#include "lexer.h"
#include "chunk.h"
#include "value.h"

struct Local {
    std::string name;
    int depth = 0;
};

struct Loop {
    int startIP;
    int scopeDepth;
    std::vector<int> breakJumps;
    Loop* enclosing = nullptr;
};

enum class FunctionType {
    TYPE_FUNCTION,
    TYPE_SCRIPT
};

struct CompilerContext {
    CompilerContext* enclosing = nullptr;
    FunctionPtr function = nullptr;
    FunctionType type = FunctionType::TYPE_SCRIPT;
    Local locals[256];
    int localCount = 0;
    int scopeDepth = 0;
    Chunk& chunk;

    CompilerContext(Chunk& c) : chunk(c) {}
};

class Compiler {
private:
    Lexer lexer;
    Token current;
    Token prev;
    Chunk& targetChunk;
    CompilerContext* currentContext = nullptr;
    Loop* currentLoop = nullptr;
    bool hasError = false;

    Chunk& chunk() { return currentContext->chunk; }

    void advance();
    bool match(TokenType type);
    void consume(TokenType type, const std::string& errMsg);
    void emitConstant(Value value);

    int emitJump(OpCode op);
    void patchJump(int offset);
    void emitLoop(int loopStart);

    void beginScope();
    void endScope();
    void addLocal(const std::string& name);
    int resolveLocal(CompilerContext* context, const std::string& name);

    uint8_t argumentList();

    void primary();
    void postfix();
    void power();
    void unary();
    void factor();
    void term();
    void relational();
    void equality();
    void andExpression();
    void orExpression();
    void assignment();
    void expression();

    void varDeclaration();
    void taskDeclaration();
    void giveStatement();
    void blockStatement();
    void ifStatement();
    void whileStatement();
    void haltStatement();
    void skipStatement();
    void grabStatement();
    void statement();

public:
    Compiler(const std::string& src, Chunk& targetChunk);
    bool compile();
};
