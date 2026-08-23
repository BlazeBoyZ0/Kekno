#pragma once
#include <vector>
#include <unordered_map>
#include "chunk.h"
#include "value.h"

struct CallFrame {
    FunctionPtr function;
    const uint8_t* ip = nullptr;
    size_t slotsOffset = 0;
};

class VM {
private:
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    std::unordered_map<std::string, Value> globals;

    void push(Value value);
    Value pop();
    Value peek(int distance);

    bool call(FunctionPtr function, int argCount);

public:
    VM();
    void run(Chunk& chunk);
};
