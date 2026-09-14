#pragma once
#include <vector>
#include <cstdint>
#include "opcode.h"

struct Value;
struct TypeSpec;

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<TypeSpec> localTypes;

    void writeByte(uint8_t byte);
    void write16(uint16_t value);
    void writeOp(OpCode op);
    uint16_t addConstant(Value value);
};
