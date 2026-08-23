#pragma once
#include <vector>
#include <cstdint>
#include "opcode.h"

struct Value;

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;

    void writeByte(uint8_t byte);
    void writeOp(OpCode op);
    uint8_t addConstant(Value value);
};
