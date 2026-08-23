#include "chunk.h"

void Chunk::writeByte(uint8_t byte) {
    code.push_back(byte);
}

void Chunk::writeOp(OpCode op) {
    code.push_back(static_cast<uint8_t>(op));
}

uint8_t Chunk::addConstant(Value value) {
    constants.push_back(value);
    return static_cast<uint8_t>(constants.size() - 1);
}
