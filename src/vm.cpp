#include "vm.h"
#include "compiler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <random>
#include <stdexcept>
#include <algorithm>

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
    globals["size"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: size() expects 1 argument.");
        if (args[0].isArray()) {
            return Value(static_cast<double>(args[0].array ? args[0].array->size() : 0));
        } else if (args[0].isString()) {
            return Value(static_cast<double>(args[0].str.length()));
        } else if (args[0].isMap()) {
            return Value(static_cast<double>(args[0].map ? args[0].map->table.size() : 0));
        }
        throw std::runtime_error("[Runtime Error]: size() expects array, map, or string argument.");
    }));

    globals["keys"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: keys() expects 1 argument.");
        if (!args[0].isMap() || !args[0].map) {
            throw std::runtime_error("[Runtime Error]: keys() expects map as argument.");
        }
        ArrayPtr arr = std::make_shared<std::vector<Value>>();
        for (const std::string& key : args[0].map->keys) {
            arr->push_back(Value(key));
        }
        return Value(arr);
    }));

    globals["values"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: values() expects 1 argument.");
        if (!args[0].isMap() || !args[0].map) {
            throw std::runtime_error("[Runtime Error]: values() expects map as argument.");
        }
        ArrayPtr arr = std::make_shared<std::vector<Value>>();
        for (const std::string& key : args[0].map->keys) {
            auto it = args[0].map->table.find(key);
            if (it != args[0].map->table.end()) {
                arr->push_back(it->second);
            }
        }
        return Value(arr);
    }));

    globals["has"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: has() expects 2 arguments.");
        if (args[0].isMap() && args[0].map) {
            if (!args[1].isString()) {
                throw std::runtime_error("[Runtime Error]: map key must be a string in has().");
            }
            return Value(args[0].map->table.find(args[1].str) != args[0].map->table.end());
        } else if (args[0].isArray() && args[0].array) {
            for (const Value& elem : *args[0].array) {
                if (elem.isEqual(args[1])) return Value(true);
            }
            return Value(false);
        }
        throw std::runtime_error("[Runtime Error]: has() expects map or array as first argument.");
    }));

    globals["purge"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: purge() expects 2 arguments.");
        if (args[0].isMap() && args[0].map) {
            if (!args[1].isString()) {
                throw std::runtime_error("[Runtime Error]: purge() map key must be a string.");
            }
            bool removed = args[0].map->remove(args[1].str);
            return Value(removed);
        } else if (args[0].isArray() && args[0].array) {
            if (!args[1].isNumber()) {
                throw std::runtime_error("[Runtime Error]: purge() array index must be a number.");
            }
            int idx = static_cast<int>(args[1].num);
            if (args[1].num != idx || idx < 0 || idx >= static_cast<int>(args[0].array->size())) {
                throw std::runtime_error("[Runtime Error]: purge() array index out of bounds.");
            }
            args[0].array->erase(args[0].array->begin() + idx);
            return Value(true);
        }
        throw std::runtime_error("[Runtime Error]: purge() expects map or array as first argument.");
    }));

    globals["inject"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: inject() expects 2 arguments.");
        if (!args[0].isArray() || !args[0].array) {
            throw std::runtime_error("[Runtime Error]: inject() expects array as first argument.");
        }
        args[0].array->push_back(args[1]);
        return Value();
    }));

    globals["expel"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: expel() expects 1 argument.");
        if (!args[0].isArray() || !args[0].array) {
            throw std::runtime_error("[Runtime Error]: expel() expects array argument.");
        }
        if (args[0].array->empty()) {
            throw std::runtime_error("[Runtime Error]: Cannot expel from empty array.");
        }
        Value last = args[0].array->back();
        args[0].array->pop_back();
        return last;
    }));

    globals["read"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: read() expects 1 argument.");
        std::cout << args[0].toString();
        std::cout.flush();
        std::string input;
        std::getline(std::cin, input);
        return Value(input);
    }));

    globals["scan"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: scan() expects 1 argument.");
        switch (args[0].type) {
            case ValueType::NUMBER: return Value(std::string("number"));
            case ValueType::STRING: return Value(std::string("string"));
            case ValueType::BOOL: return Value(std::string("bool"));
            case ValueType::ARRAY: return Value(std::string("array"));
            case ValueType::MAP: return Value(std::string("map"));
            case ValueType::FUNCTION:
            case ValueType::NATIVE: return Value(std::string("task"));
            case ValueType::NIL: return Value(std::string("nil"));
        }
        return Value(std::string("nil"));
    }));

    globals["cast_num"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_num() expects 1 argument.");
        if (args[0].isNumber()) return args[0];
        if (args[0].isBool()) return Value(args[0].boolean ? 1.0 : 0.0);
        if (args[0].isString()) {
            if (args[0].str.empty()) throw std::runtime_error("[Runtime Error]: Cannot cast empty string to number.");
            size_t pos = 0;
            try {
                double val = std::stod(args[0].str, &pos);
                if (pos == args[0].str.length()) return Value(val);
            } catch (...) {}
            throw std::runtime_error("[Runtime Error]: Cannot cast string '" + args[0].str + "' to number.");
        }
        throw std::runtime_error("[Runtime Error]: Cannot cast value to number.");
    }));

    globals["cast_str"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_str() expects 1 argument.");
        return Value(args[0].toString());
    }));

    globals["clock"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 0) throw std::runtime_error("[Runtime Error]: clock() expects 0 arguments.");
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
        double seconds = std::chrono::duration<double>(now).count();
        return Value(seconds);
    }));

    globals["rand"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 0) throw std::runtime_error("[Runtime Error]: rand() expects 0 arguments.");
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value(dist(rng));
    }));

    globals["abs"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: abs() expects 1 argument.");
        if (!args[0].isNumber()) throw std::runtime_error("[Runtime Error]: abs() expects a number.");
        return Value(std::abs(args[0].num));
    }));

    globals["floor"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: floor() expects 1 argument.");
        if (!args[0].isNumber()) throw std::runtime_error("[Runtime Error]: floor() expects a number.");
        return Value(std::floor(args[0].num));
    }));

    globals["ceil"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: ceil() expects 1 argument.");
        if (!args[0].isNumber()) throw std::runtime_error("[Runtime Error]: ceil() expects a number.");
        return Value(std::ceil(args[0].num));
    }));

    globals["sqrt"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: sqrt() expects 1 argument.");
        if (!args[0].isNumber()) throw std::runtime_error("[Runtime Error]: sqrt() expects a number.");
        return Value(std::sqrt(args[0].num));
    }));

    globals["clamp"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 3) throw std::runtime_error("[Runtime Error]: clamp() expects 3 arguments.");
        if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber()) {
            throw std::runtime_error("[Runtime Error]: clamp() expects numbers.");
        }
        double val = args[0].num;
        double minVal = args[1].num;
        double maxVal = args[2].num;
        return Value(std::max(minVal, std::min(val, maxVal)));
    }));
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
                } else if (callee.isNative()) {
                    try {
                        Value* args = &stack[stack.size() - argCount];
                        Value result = callee.nativeFn(argCount, args);
                        stack.resize(stack.size() - argCount - 1);
                        push(result);
                    } catch (const std::exception& ex) {
                        std::cout << ex.what() << std::endl;
                        return;
                    }
                } else {
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
            case OpCode::OP_BUILD_MAP: {
                uint8_t entryCount = *frame->ip++;
                MapPtr mapObj = std::make_shared<ObjMap>();
                std::vector<std::pair<std::string, Value>> entries(entryCount);
                for (int i = entryCount - 1; i >= 0; --i) {
                    Value val = pop();
                    Value keyVal = pop();
                    if (!keyVal.isString()) {
                        std::cout << "[Runtime Error]: Map key must be a string." << std::endl;
                        return;
                    }
                    entries[i] = {keyVal.str, val};
                }
                for (int i = 0; i < entryCount; ++i) {
                    mapObj->set(entries[i].first, entries[i].second);
                }
                push(Value(mapObj));
                break;
            }
            case OpCode::OP_GET_INDEX: {
                Value indexVal = pop();
                Value target = pop();

                if (target.isMap()) {
                    if (!indexVal.isString()) {
                        std::cout << "[Runtime Error]: Map key must be a string." << std::endl;
                        return;
                    }
                    if (target.map) {
                        auto it = target.map->table.find(indexVal.str);
                        if (it != target.map->table.end()) {
                            push(it->second);
                        } else {
                            push(Value()); // nil if key not found
                        }
                    } else {
                        push(Value());
                    }
                } else if (target.isArray()) {
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
                    push((*target.array)[index]);
                } else if (target.isString()) {
                    if (!indexVal.isNumber()) {
                        std::cout << "[Runtime Error]: String index must be a number." << std::endl;
                        return;
                    }
                    int index = static_cast<int>(indexVal.num);
                    if (indexVal.num != index) {
                        std::cout << "[Runtime Error]: String index must be an integer." << std::endl;
                        return;
                    }
                    if (index < 0 || index >= static_cast<int>(target.str.length())) {
                        std::cout << "[Runtime Error]: String index " << index << " out of bounds." << std::endl;
                        return;
                    }
                    push(Value(std::string(1, target.str[index])));
                } else {
                    std::cout << "[Runtime Error]: Only arrays, maps, and strings can be indexed." << std::endl;
                    return;
                }
                break;
            }
            case OpCode::OP_SET_INDEX: {
                Value val = pop();
                Value indexVal = pop();
                Value target = pop();

                if (target.isMap()) {
                    if (!indexVal.isString()) {
                        std::cout << "[Runtime Error]: Map key must be a string." << std::endl;
                        return;
                    }
                    if (!target.map) {
                        std::cout << "[Runtime Error]: Invalid map target." << std::endl;
                        return;
                    }
                    target.map->set(indexVal.str, val);
                    push(val);
                } else if (target.isArray()) {
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
                } else {
                    std::cout << "[Runtime Error]: Only arrays and maps support index assignment." << std::endl;
                    return;
                }
                break;
            }
            case OpCode::OP_GRAB: {
                Value pathVal = pop();
                if (!pathVal.isString()) {
                    std::cout << "[Runtime Error]: grab path must be a string." << std::endl;
                    return;
                }
                std::ifstream file(pathVal.str);
                if (!file.is_open()) {
                    std::cout << "[Runtime Error]: Could not open grab file \"" << pathVal.str << "\"." << std::endl;
                    return;
                }
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string grabSource = buffer.str();

                FunctionPtr grabFn = std::make_shared<ObjFunction>();
                grabFn->name = pathVal.str;
                grabFn->arity = 0;
                Compiler compiler(grabSource, grabFn->chunk);
                if (!compiler.compile()) {
                    std::cout << "[Runtime Error]: Could not compile grab file \"" << pathVal.str << "\"." << std::endl;
                    return;
                }
                push(Value(grabFn));
                if (!call(grabFn, 0)) {
                    return;
                }
                frame = &frames.back();
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
