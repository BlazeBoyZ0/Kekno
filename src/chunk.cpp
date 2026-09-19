#include "chunk.h"
#include "value.h"
#include <stdexcept>

void Chunk::writeByte(uint8_t byte, int line, int column) {
    code.push_back(byte);
    lines.push_back(line);
    columns.push_back(column);
}

void Chunk::write16(uint16_t value, int line, int column) {
    writeByte(static_cast<uint8_t>((value >> 8) & 0xff), line, column);
    writeByte(static_cast<uint8_t>(value & 0xff), line, column);
}

void Chunk::writeOp(OpCode op, int line, int column) {
    writeByte(static_cast<uint8_t>(op), line, column);
}

uint16_t Chunk::addConstant(Value value) {
    if (constants.size() >= 65536) {
        throw std::runtime_error("[Compiler Error]: Constant pool limit exceeded (maximum 65,536 constants).");
    }
    constants.push_back(value);
    return static_cast<uint16_t>(constants.size() - 1);
}

int Chunk::getLine(size_t offset) const {
    if (offset < lines.size()) return lines[offset];
    return 1;
}

int Chunk::getColumn(size_t offset) const {
    if (offset < columns.size()) return columns[offset];
    return 1;
}
