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

bool VM::call(FunctionPtr function, int argCount) {
    if (argCount != function->arity) {
        std::cout << "[Runtime Error]: Expected " << function->arity << " arguments but got " << argCount << "." << std::endl;
        return false;
    }
    if (frames.size() >= 256) {
        std::cout << "[Runtime Error]: Stack overflow." << std::endl;
        return false;
    }
    CallFrame frame;
    frame.function = function;
    frame.ip = function->chunk.code.data();
    frame.slotsOffset = stack.size() - argCount - 1;
    frames.push_back(frame);
    return true;
}

VM::VM() {
    globals["len"] = Value(std::string("len"));
}

void VM::run(Chunk& mainChunk) {
    FunctionPtr mainFn = std::make_shared<ObjFunction>();
    mainFn->chunk = mainChunk;
    mainFn->name = "main";
    mainFn->arity = 0;

    frames.clear();
    stack.clear();

    push(Value(mainFn));
    call(mainFn, 0);

    CallFrame* frame = &frames.back();

    while (true) {
        OpCode instruction = static_cast<OpCode>(*frame->ip++);
        switch (instruction) {
            case OpCode::OP_CONSTANT: {
                uint8_t index = *frame->ip++;
                push(frame->function->chunk.constants[index]);
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
                uint8_t index = *frame->ip++;
                std::string name = frame->function->chunk.constants[index].str;
                globals[name] = pop();
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint8_t index = *frame->ip++;
                std::string name = frame->function->chunk.constants[index].str;
                auto it = globals.find(name);
                if (it == globals.end()) {
                    std::cout << "[Runtime Error]: Undefined variable '" << name << "'" << std::endl;
                    return;
                }
                push(it->second);
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                uint8_t index = *frame->ip++;
                std::string name = frame->function->chunk.constants[index].str;
                auto it = globals.find(name);
                if (it == globals.end()) {
                    std::cout << "[Runtime Error]: Variable '" << name << "' is not defined." << std::endl;
                    return;
                }
                it->second = peek(0);
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = *frame->ip++;
                push(stack[frame->slotsOffset + slot]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = *frame->ip++;
                stack[frame->slotsOffset + slot] = peek(0);
                break;
            }
            case OpCode::OP_CALL: {
                uint8_t argCount = *frame->ip++;
                Value callee = peek(argCount);
                if (callee.isFunction()) {
                    if (!call(callee.function, argCount)) {
                        return;
                    }
                    frame = &frames.back();
                } else if (callee.isString() && callee.str == "len") { // native function len check if bound as function
                    // check if len was called as len(arr)
                    if (argCount != 1) {
                        std::cout << "[Runtime Error]: len() expects exactly 1 argument." << std::endl;
                        return;
                    }
                    Value arg = pop(); // pop arg
                    pop(); // pop callee
                    if (arg.isArray()) {
                        push(Value(static_cast<double>(arg.array ? arg.array->size() : 0)));
                    } else if (arg.isString()) {
                        push(Value(static_cast<double>(arg.str.length())));
                    } else {
                        std::cout << "[Runtime Error]: len() expects string or array argument." << std::endl;
                        return;
                    }
                } else {
                    // Check if it is native function len stored in global or local
                    std::cout << "[Runtime Error]: Can only call functions." << std::endl;
                    return;
                }
                break;
            }
            case OpCode::OP_BUILD_ARRAY: {
                uint8_t elementCount = *frame->ip++;
                ArrayPtr arr = std::make_shared<std::vector<Value>>();
                arr->resize(elementCount);
                for (int i = elementCount - 1; i >= 0; --i) {
                    (*arr)[i] = pop();
                }
                push(Value(arr));
                break;
            }
            case OpCode::OP_GET_INDEX: {
                Value indexVal = pop();
                Value target = pop();

                if (!indexVal.isNumber()) {
                    std::cout << "[Runtime Error]: Array index must be a number." << std::endl;
                    return;
                }

                int index = static_cast<int>(indexVal.num);
                if (indexVal.num != index) {
                    std::cout << "[Runtime Error]: Array index must be an integer." << std::endl;
                    return;
                }

                if (target.isArray()) {
                    if (!target.array || index < 0 || index >= static_cast<int>(target.array->size())) {
                        std::cout << "[Runtime Error]: Array index " << index << " out of bounds." << std::endl;
                        return;
                    }
                    push((*target.array)[index]);
                } else if (target.isString()) {
                    if (index < 0 || index >= static_cast<int>(target.str.length())) {
                        std::cout << "[Runtime Error]: String index " << index << " out of bounds." << std::endl;
                        return;
                    }
                    push(Value(std::string(1, target.str[index])));
                } else {
                    std::cout << "[Runtime Error]: Only arrays and strings can be indexed." << std::endl;
                    return;
                }
                break;
            }
            case OpCode::OP_SET_INDEX: {
                Value val = pop();
                Value indexVal = pop();
                Value target = pop();

                if (!target.isArray()) {
                    std::cout << "[Runtime Error]: Only arrays support index assignment." << std::endl;
                    return;
                }

                if (!indexVal.isNumber()) {
                    std::cout << "[Runtime Error]: Array index must be a number." << std::endl;
                    return;
                }

                int index = static_cast<int>(indexVal.num);
                if (indexVal.num != index) {
                    std::cout << "[Runtime Error]: Array index must be an integer." << std::endl;
                    return;
                }

                if (!target.array || index < 0 || index >= static_cast<int>(target.array->size())) {
                    std::cout << "[Runtime Error]: Array index " << index << " out of bounds." << std::endl;
                    return;
                }

                (*target.array)[index] = val;
                push(val);
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
                uint16_t offset = (static_cast<uint16_t>(frame->ip[0]) << 8) | frame->ip[1];
                frame->ip += 2 + offset;
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = (static_cast<uint16_t>(frame->ip[0]) << 8) | frame->ip[1];
                frame->ip += 2;
                if (peek(0).isFalsey()) {
                    frame->ip += offset;
                }
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = (static_cast<uint16_t>(frame->ip[0]) << 8) | frame->ip[1];
                frame->ip += 2;
                frame->ip -= offset;
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
                Value result = pop();
                size_t slotsOffset = frame->slotsOffset;
                frames.pop_back();
                if (frames.empty()) {
                    pop(); // pop main function
                    return;
                }
                stack.resize(slotsOffset);
                push(result);
                frame = &frames.back();
                break;
            }
        }
    }
}
