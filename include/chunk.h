#pragma once
#include <vector>
#include <cstdint>
#include "value.h"
#include "opcode.h"

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;

    void writeByte(uint8_t byte);
    void writeOp(OpCode op);
    uint8_t addConstant(Value value);
};
