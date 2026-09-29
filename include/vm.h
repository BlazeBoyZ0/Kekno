#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "chunk.h"
#include "value.h"

struct CallFrame {
    ClosurePtr closure;
    const uint8_t* ip = nullptr;
    size_t slotsOffset = 0;
    bool isGrab = false;
};

struct ExceptionHandler {
    uint16_t catchIP = 0xffff;
    uint16_t finallyIP = 0xffff;
    size_t frameIndex = 0;
    size_t stackDepth = 0;
};

enum class PendingKind {
    NONE,
    RETURN,
    DROP,
    HALT,
    SKIP,
    RUNTIME_ERROR
};

struct PendingControlFlow {
    PendingKind kind = PendingKind::NONE;
    Value value;
    int jumpIP = -1;
};

class VM {
private:
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    std::unordered_map<std::string, Value> builtins;
    std::unordered_map<std::string, ModulePtr> moduleCache;
    std::vector<std::string> loadingStackPaths;
    std::vector<std::string> loadingStackNames;
    UpvaluePtr openUpvalues = nullptr;
    ModulePtr rootModule = nullptr;

    StructDefPtr mapEntryDef = nullptr;
    StructDefPtr runtimeErrorDef = nullptr;
    std::vector<ExceptionHandler> tryHandlers;
    std::vector<PendingControlFlow> pendingControlFlowStack;

    bool wasErrorUnwound = false;
    bool uncaughtErrorPrinted = false;

    Value makeRuntimeErrorObject(const std::string& message, const std::string& typeName = "Runtime Error");
    bool unwindError(Value errorVal, bool isRuntimeError = false);
    bool raiseRuntimeError(const std::string& message, const std::string& typeName = "Runtime Error");
    bool runtimeError(const std::string& message, const std::string& typeName = "Runtime Error");

    void push(Value value);
    Value pop();
    Value peek(int distance);

    bool call(ClosurePtr closure, int argCount, const std::vector<std::string>& argNames, bool isGrab = false);
    bool callOperatorOverload(const std::string& opSymbol, const Value& receiver, const Value& rightArg, bool isUnary);
    Value runCallback(Value cb, const std::vector<Value>& availableArgs, int maxAllowedParams);
    bool executeInstruction(OpCode instruction, CallFrame& frame);
    UpvaluePtr captureUpvalue(size_t stackIndex);
    void closeUpvalues(size_t lastSlotIndex);
    ModulePtr loadModule(const std::string& modulePathStr, const std::string& requesterPath, ClosurePtr& outClosure, bool& isNew);

public:
    VM();
    bool run(Chunk& chunk, const std::string& scriptPath = "");
    void resetStack();
};
