#pragma once
#include <vector>
#include <unordered_map>
#include "chunk.h"
#include "value.h"

struct CallFrame {
    ClosurePtr closure;
    const uint8_t* ip = nullptr;
    size_t slotsOffset = 0;
    bool isGrab = false;
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

    void push(Value value);
    Value pop();
    Value peek(int distance);

    bool call(ClosurePtr closure, int argCount, bool isGrab = false);
    UpvaluePtr captureUpvalue(size_t stackIndex);
    void closeUpvalues(size_t lastSlotIndex);
    ModulePtr loadModule(const std::string& modulePathStr, const std::string& requesterPath, ClosurePtr& outClosure, bool& isNew);

public:
    VM();
    void run(Chunk& chunk, const std::string& scriptPath = "");
    void resetStack();
};
