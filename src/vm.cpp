#include "vm.h"
#include <iostream>

void VM::push(Value value) {
    stack.push_back(value);
}

Value VM::pop() {
    if (stack.empty()) return Value(0.0);
    Value val = stack.back();
    stack.pop_back();
    return val;
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
            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                if (a.isString() || b.isString()) {
                    push(Value(a.toString() + b.toString()));
                } else {
                    push(Value(a.num + b.num));
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
