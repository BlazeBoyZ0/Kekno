#include "vm.h"
#include <iostream>
#include <cmath>

void VM::push(Value value) {
    stack.push_back(value);
}

Value VM::pop() {
    if (stack.empty()) return Value();
    Value val = stack.back();
    stack.pop_back();
    return val;
}

Value VM::peek(int distance) {
    if (distance >= static_cast<int>(stack.size())) return Value();
    return stack[stack.size() - 1 - distance];
}

void VM::run(Chunk& chunk) {
    const uint8_t* ip = chunk.code.data();

    while (true) {
        OpCode instruction = static_cast<OpCode>(*ip++);
        switch (instruction) {
            case OpCode::OP_CONSTANT: {
                uint8_t index = *ip++;
                push(chunk.constants[index]);
                break;
            }
            case OpCode::OP_NIL: {
                push(Value());
                break;
            }
            case OpCode::OP_TRUE: {
                push(Value(true));
                break;
            }
            case OpCode::OP_FALSE: {
                push(Value(false));
                break;
            }
            case OpCode::OP_DEFINE_GLOBAL: {
                uint8_t index = *ip++;
                std::string name = chunk.constants[index].str;
                globals[name] = pop();
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint8_t index = *ip++;
                std::string name = chunk.constants[index].str;
                auto it = globals.find(name);
                if (it == globals.end()) {
                    std::cout << "[Runtime Error]: Undefined variable '" << name << "'" << std::endl;
                    return;
                }
                push(it->second);
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                uint8_t index = *ip++;
                std::string name = chunk.constants[index].str;
                auto it = globals.find(name);
                if (it == globals.end()) {
                    std::cout << "[Runtime Error]: Variable '" << name << "' is not defined." << std::endl;
                    return;
                }
                it->second = pop();
                break;
            }
            case OpCode::OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(Value(a.isEqual(b)));
                break;
            }
            case OpCode::OP_GREATER: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '>' only supports numbers!" << std::endl;
                    return;
                }
                push(Value(a.num > b.num));
                break;
            }
            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '<' only supports numbers! (a: " << a.toString() << ", b: " << b.toString() << ")" << std::endl;
                    return;
                }
                push(Value(a.num < b.num));
                break;
            }
            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                if (a.isString() || b.isString()) {
                    push(Value(a.toString() + b.toString()));
                } else if (a.isNumber() && b.isNumber()) {
                    push(Value(a.num + b.num));
                } else {
                    std::cout << "[Runtime Error]: '+' operands must be two numbers or strings." << std::endl;
                    return;
                }
                break;
            }
            case OpCode::OP_SUBTRACT: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '-' only supports numbers!" << std::endl;
                    return;
                }
                push(Value(a.num - b.num));
                break;
            }
            case OpCode::OP_MULTIPLY: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '*' only supports numbers!" << std::endl;
                    return;
                }
                push(Value(a.num * b.num));
                break;
            }
            case OpCode::OP_DIVIDE: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '/' only supports numbers!" << std::endl;
                    return;
                }
                if (b.num == 0.0) {
                    std::cout << "[Runtime Error]: Division by zero!" << std::endl;
                    return;
                }
                push(Value(a.num / b.num));
                break;
            }
            case OpCode::OP_MODULO: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '%' only supports numbers!" << std::endl;
                    return;
                }
                if (b.num == 0.0) {
                    std::cout << "[Runtime Error]: Modulo by zero!" << std::endl;
                    return;
                }
                push(Value(std::fmod(a.num, b.num)));
                break;
            }
            case OpCode::OP_POWER: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '^' only supports numbers!" << std::endl;
                    return;
                }
                push(Value(std::pow(a.num, b.num)));
                break;
            }
            case OpCode::OP_NOT: {
                Value val = pop();
                push(Value(val.isFalsey()));
                break;
            }
            case OpCode::OP_JUMP: {
                uint16_t offset = (static_cast<uint16_t>(ip[0]) << 8) | ip[1];
                ip += 2 + offset;
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = (static_cast<uint16_t>(ip[0]) << 8) | ip[1];
                ip += 2;
                if (peek(0).isFalsey()) {
                    ip += offset;
                }
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = (static_cast<uint16_t>(ip[0]) << 8) | ip[1];
                ip += 2;
                ip -= offset;
                break;
            }
            case OpCode::OP_PRINT: {
                Value val = pop();
                std::cout << "=> " << val.toString() << std::endl;
                break;
            }
            case OpCode::OP_POP: {
                pop();
                break;
            }
            case OpCode::OP_RETURN: {
                return;
            }
        }
    }
}
