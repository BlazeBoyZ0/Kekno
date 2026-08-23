#pragma once
#include <vector>
#include <unordered_map>
#include "chunk.h"
#include "value.h"

class VM {
private:
    std::vector<Value> stack;
    std::unordered_map<std::string, Value> globals;

    void push(Value value);
    Value pop();

public:
    void run(Chunk& chunk);
};
