#include "chunk.h"
#include "value.h"
#include <stdexcept>

void Chunk::writeByte(uint8_t byte) {
    code.push_back(byte);
}

void Chunk::write16(uint16_t value) {
    code.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    code.push_back(static_cast<uint8_t>(value & 0xff));
}

void Chunk::writeOp(OpCode op) {
    code.push_back(static_cast<uint8_t>(op));
}

uint16_t Chunk::addConstant(Value value) {
    if (constants.size() >= 65536) {
        throw std::runtime_error("[Compiler Error]: Constant pool limit exceeded (maximum 65,536 constants).");
    }
    constants.push_back(value);
    return static_cast<uint16_t>(constants.size() - 1);
}
