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
    std::unordered_map<std::string, Value> globals;
    std::unordered_map<std::string, TypeSpec> globalTypes;
    std::vector<std::unordered_map<std::string, Value>> grabSnapshots;
    UpvaluePtr openUpvalues = nullptr;

    void push(Value value);
    Value pop();
    Value peek(int distance);

    bool call(ClosurePtr closure, int argCount, bool isGrab = false);
    UpvaluePtr captureUpvalue(size_t stackIndex);
    void closeUpvalues(size_t lastSlotIndex);

public:
    VM();
    void run(Chunk& chunk);
    void resetStack();
};
