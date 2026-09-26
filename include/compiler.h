#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include "lexer.h"
#include "chunk.h"
#include "value.h"

struct Local {
    std::string name;
    int depth = 0;
    bool isConst = false;
    TypeSpec typeSpec;
};

struct Upvalue {
    uint16_t index = 0;
    bool isLocal = false;
    std::string name;
    bool isConst = false;
    TypeSpec typeSpec;
};

struct Loop {
    int startIP;
    int scopeDepth;
    int continueIP = -1;
    std::vector<int> breakJumps;
    std::vector<int> continueJumps;
    Loop* enclosing = nullptr;
};

enum class FunctionType {
    TYPE_FUNCTION,
    TYPE_METHOD,
    TYPE_SCRIPT
};

struct CompilerContext {
    CompilerContext* enclosing = nullptr;
    FunctionPtr function = nullptr;
    FunctionType type = FunctionType::TYPE_SCRIPT;
    std::vector<Local> locals;
    std::vector<Upvalue> upvalues;
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
    std::unordered_map<std::string, bool> globalConsts;
    std::unordered_set<std::string> declaredStructs;
    std::unordered_set<std::string> compiledStructsInModule;
    std::unordered_set<std::string> knownModulePrefixes;
    bool hasError = false;
    bool panicMode = false;
    ObjStructDef* currentStructDef = nullptr;
    int expressionDepth = 0;
    static constexpr int MAX_EXPRESSION_DEPTH = 300;

    Chunk& chunk() { return currentContext->chunk; }

    void errorAt(const Token& token, const std::string& message, const std::string& errorType = "Syntax Error");
    void error(const std::string& message, const std::string& errorType = "Compiler Error");
    void advance();
    bool match(TokenType type);
    void consume(TokenType type, const std::string& errMsg);
    uint16_t addConstant(Value value);
    void emitConstant(Value value);

    int emitJump(OpCode op);
    void patchJump(int offset);
    void emitLoop(int loopStart);

    void beginScope();
    void endScope();
    void addLocal(const std::string& name, bool isConst = false, TypeSpec typeSpec = TypeSpec{TypeKind::ANY});
    int resolveLocal(CompilerContext* context, const std::string& name);
    int resolveUpvalue(CompilerContext* context, const std::string& name);
    int addUpvalue(CompilerContext* context, uint16_t index, bool isLocal, const std::string& name, bool isConst, TypeSpec typeSpec);

    uint16_t argumentList();
    TypeSpec parseTypeDeclaration();
    void parseParameter(TypeSpec& outType, std::string& outName);

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
    void expression();

    void varDeclaration(bool isPublic = false);
    void taskDeclaration(bool isPublic = false);
    void buildDeclaration(bool isPublic = false);
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
