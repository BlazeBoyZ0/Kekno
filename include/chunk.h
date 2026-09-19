#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "opcode.h"

struct Value;
struct TypeSpec;

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<TypeSpec> localTypes;
    std::vector<int> lines;
    std::vector<int> columns;
    std::string source;

    void writeByte(uint8_t byte, int line = 1, int column = 1);
    void write16(uint16_t value, int line = 1, int column = 1);
    void writeOp(OpCode op, int line = 1, int column = 1);
    uint16_t addConstant(Value value);
    int getLine(size_t offset) const;
    int getColumn(size_t offset) const;
};
