#pragma once
#include <vector>
#include <unordered_map>
#include "chunk.h"
#include "value.h"

struct CallFrame {
    FunctionPtr function;
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

    void push(Value value);
    Value pop();
    Value peek(int distance);

    bool call(FunctionPtr function, int argCount, bool isGrab = false);

public:
    VM();
    void run(Chunk& chunk);
    void resetStack();
};
