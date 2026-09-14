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
#include <limits>

static uint16_t read16(const uint8_t*& ip) {
    uint16_t value = (static_cast<uint16_t>(ip[0]) << 8) | ip[1];
    ip += 2;
    return value;
}

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

void VM::resetStack() {
    stack.clear();
    frames.clear();
}

static bool checkAndCoerceValueType(const TypeSpec& expected, Value& val);

bool VM::call(FunctionPtr function, int argCount, bool isGrab) {
    if (argCount != function->arity) {
        std::cout << "[Runtime Error]: Expected " << function->arity << " arguments but got " << argCount << "." << std::endl;
        return false;
    }
    if (frames.size() >= 65536) {
        std::cout << "[Runtime Error]: Stack overflow." << std::endl;
        return false;
    }

    // Argument type validation
    for (size_t i = 0; i < function->paramTypes.size(); i++) {
        TypeSpec expected = function->paramTypes[i];
        if (expected.kind == TypeKind::ANY || expected.kind == TypeKind::UNTYPED) continue;
        Value arg = stack[stack.size() - argCount + i];

        if (!checkAndCoerceValueType(expected, stack[stack.size() - argCount + i])) {
            std::cout << "[Runtime Error]: Argument " << (i + 1) << " expects type " << expected.toString() << " but got " << arg.getTypeSpec().toString() << "." << std::endl;
            return false;
        }
    }

    CallFrame frame;
    frame.function = function;
    frame.ip = function->chunk.code.data();
    frame.slotsOffset = stack.size() - argCount - 1;
    frame.isGrab = isGrab;
    frames.push_back(frame);
    return true;
}

static std::string trimString(const std::string& str);

static TypeSpec parseTypeSpecString(const std::string& str) {
    if (str.empty() || str == "any") return TypeSpec{TypeKind::ANY};
    if (str == "int") return TypeSpec{TypeKind::INT};
    if (str == "float") return TypeSpec{TypeKind::FLOAT};
    if (str == "string") return TypeSpec{TypeKind::STRING};
    if (str == "bool") return TypeSpec{TypeKind::BOOL};
    if (str == "char") return TypeSpec{TypeKind::CHAR};
    if (str == "array") return TypeSpec{TypeKind::ARRAY};
    if (str == "map") return TypeSpec{TypeKind::MAP};
    if (str.rfind("array<", 0) == 0 && str.back() == '>') {
        std::string sub = str.substr(6, str.length() - 7);
        TypeSpec spec;
        spec.kind = TypeKind::ARRAY;
        TypeSpec elem = parseTypeSpecString(sub);
        spec.elementKind = elem.kind;
        spec.elemType = std::make_shared<TypeSpec>(elem);
        return spec;
    }
    if (str.rfind("map<", 0) == 0 && str.back() == '>') {
        std::string sub = str.substr(4, str.length() - 5);
        int angleDepth = 0;
        size_t comma = std::string::npos;
        for (size_t i = 0; i < sub.length(); ++i) {
            if (sub[i] == '<') angleDepth++;
            else if (sub[i] == '>') angleDepth--;
            else if (sub[i] == ',' && angleDepth == 0) {
                comma = i;
                break;
            }
        }
        if (comma != std::string::npos) {
            std::string kSub = trimString(sub.substr(0, comma));
            std::string vSub = trimString(sub.substr(comma + 1));
            TypeSpec spec;
            spec.kind = TypeKind::MAP;
            TypeSpec kSpec = parseTypeSpecString(kSub);
            TypeSpec vSpec = parseTypeSpecString(vSub);
            spec.keyKind = kSpec.kind;
            spec.valueKind = vSpec.kind;
            spec.keyType = std::make_shared<TypeSpec>(kSpec);
            spec.valType = std::make_shared<TypeSpec>(vSpec);
            return spec;
        }
    }
    return TypeSpec{TypeKind::ANY};
}

static bool checkAndCoerceValueType(const TypeSpec& expected, Value& val) {
    if (expected.kind == TypeKind::ANY || expected.kind == TypeKind::UNTYPED) {
        return true;
    }
    if (expected.kind == TypeKind::INT) {
        return val.isInt();
    }
    if (expected.kind == TypeKind::FLOAT) {
        if (val.isFloat()) return true;
        if (val.isInt()) {
            val = Value(static_cast<double>(val.intVal));
            return true;
        }
        return false;
    }
    if (expected.kind == TypeKind::STRING) {
        return val.isString();
    }
    if (expected.kind == TypeKind::CHAR) {
        return val.isChar();
    }
    if (expected.kind == TypeKind::BOOL) {
        return val.isBool();
    }
    if (expected.kind == TypeKind::ARRAY) {
        if (!val.isArray()) return false;
        if (!val.array) return true;
        if (expected.elementKind != TypeKind::ANY && expected.elementKind != TypeKind::UNTYPED) {
            TypeSpec elemSpec = expected.elemType ? *expected.elemType : TypeSpec{expected.elementKind};
            for (size_t i = 0; i < val.array->elements.size(); ++i) {
                if (!checkAndCoerceValueType(elemSpec, val.array->elements[i])) {
                    return false;
                }
            }
        }
        val.array->typeSpec = expected;
        return true;
    }
    if (expected.kind == TypeKind::MAP) {
        if (!val.isMap()) return false;
        if (!val.map) return true;
        TypeSpec keySpec = expected.keyType ? *expected.keyType : TypeSpec{expected.keyKind != TypeKind::ANY ? expected.keyKind : TypeKind::STRING};
        TypeSpec valSpec = expected.valType ? *expected.valType : TypeSpec{expected.valueKind};
        for (auto& pair : val.map->table) {
            Value kVal(pair.first);
            if (!checkAndCoerceValueType(keySpec, kVal)) return false;
            if (!checkAndCoerceValueType(valSpec, pair.second)) return false;
        }
        val.map->typeSpec = expected;
        return true;
    }
    return true;
}

static std::string trimString(const std::string& str) {
    size_t start = 0;
    while (start < str.length() && std::isspace(static_cast<unsigned char>(str[start]))) start++;
    size_t end = str.length();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) end--;
    return str.substr(start, end - start);
}

VM::VM() {
    globals["size"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: size() expects 1 argument.");
        if (args[0].isArray()) {
            return Value(static_cast<int64_t>(args[0].array ? args[0].array->size() : 0));
        } else if (args[0].isString()) {
            return Value(static_cast<int64_t>(args[0].str.length()));
        } else if (args[0].isMap()) {
            return Value(static_cast<int64_t>(args[0].map ? args[0].map->table.size() : 0));
        }
        throw std::runtime_error("[Runtime Error]: size() expects array, map, or string argument.");
    }));

    globals["keys"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: keys() expects 1 argument.");
        if (!args[0].isMap() || !args[0].map) {
            throw std::runtime_error("[Runtime Error]: keys() expects map as argument.");
        }
        ArrayPtr arr = std::make_shared<ObjArray>();
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
        ArrayPtr arr = std::make_shared<ObjArray>();
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
        if (args[0].isMap()) {
            if (!args[1].isString()) {
                throw std::runtime_error("[Runtime Error]: map key must be a string in has().");
            }
            if (!args[0].map) return Value(false);
            return Value(args[0].map->table.find(args[1].str) != args[0].map->table.end());
        } else if (args[0].isArray()) {
            if (!args[0].array) return Value(false);
            for (const Value& elem : *args[0].array) {
                if (elem.isEqual(args[1])) return Value(true);
            }
            return Value(false);
        }
        throw std::runtime_error("[Runtime Error]: has() expects map or array as first argument.");
    }));

    globals["purge"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: purge() expects 2 arguments.");
        if (args[0].isMap()) {
            if (!args[0].map) throw std::runtime_error("[Runtime Error]: purge() expects non-null map as first argument.");
            if (!args[1].isString()) {
                throw std::runtime_error("[Runtime Error]: purge() map key must be a string.");
            }
            bool removed = args[0].map->remove(args[1].str);
            return Value(removed);
        } else if (args[0].isArray()) {
            if (!args[0].array) throw std::runtime_error("[Runtime Error]: purge() expects non-null array as first argument.");
            if (!args[1].isInt()) {
                throw std::runtime_error("[Runtime Error]: purge() array index must be an integer.");
            }
            int64_t idx = args[1].intVal;
            if (idx < 0 || idx >= static_cast<int64_t>(args[0].array->size())) {
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
        Value val = args[1];
        if (args[0].array->typeSpec.elementKind != TypeKind::ANY && args[0].array->typeSpec.elementKind != TypeKind::UNTYPED) {
            TypeSpec expectedElemSpec{args[0].array->typeSpec.elementKind};
            if (!checkAndCoerceValueType(expectedElemSpec, val)) {
                throw std::runtime_error("[Runtime Error]: inject() type mismatch: expected " +
                                           expectedElemSpec.toString() + " but got " + val.getTypeSpec().toString() + ".");
            }
        }
        args[0].array->push_back(val);
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
            case ValueType::INT: return Value(std::string("int"));
            case ValueType::FLOAT: return Value(std::string("float"));
            case ValueType::CHAR: return Value(std::string("char"));
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
        if (args[0].isInt()) return args[0];
        if (args[0].isFloat()) return args[0];
        if (args[0].isBool()) return Value(static_cast<int64_t>(args[0].boolean ? 1 : 0));
        if (args[0].isString()) {
            if (args[0].str.empty()) throw std::runtime_error("[Runtime Error]: Cannot cast empty string to number.");
            size_t pos = 0;
            try {
                if (args[0].str.find('.') != std::string::npos) {
                    double val = std::stod(args[0].str, &pos);
                    if (pos == args[0].str.length()) return Value(val);
                } else {
                    int64_t val = std::stoll(args[0].str, &pos, 10);
                    if (pos == args[0].str.length()) return Value(val);
                }
            } catch (...) {}
            throw std::runtime_error("[Runtime Error]: Cannot cast string '" + args[0].str + "' to number.");
        }
        throw std::runtime_error("[Runtime Error]: Cannot cast value to number.");
    }));

    globals["cast_str"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_str() expects 1 argument.");
        return Value(args[0].toString());
    }));

    // Explicit Type Casts
    globals["cast_int"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_int() expects 1 argument.");
        if (args[0].isInt()) return args[0];
        if (args[0].isFloat()) {
            double f = args[0].floatVal;
            if (std::isnan(f) || std::isinf(f)) {
                throw std::runtime_error("[Runtime Error]: Cannot cast NaN or Infinity to int.");
            }
            double rounded = (f >= 0) ? std::floor(f + 0.5) : std::ceil(f - 0.5);
            if (rounded < -9223372036854775808.0 || rounded >= 9223372036854775808.0) {
                throw std::runtime_error("[Runtime Error]: Float value out of 64-bit integer range in cast_int().");
            }
            return Value(static_cast<int64_t>(rounded));
        }
        if (args[0].isBool()) return Value(static_cast<int64_t>(args[0].boolean ? 1 : 0));
        if (args[0].isString()) {
            try {
                size_t pos = 0;
                int64_t val = std::stoll(args[0].str, &pos, 10);
                if (pos == args[0].str.length()) return Value(val);
            } catch (...) {}
            throw std::runtime_error("[Runtime Error]: Cannot cast string '" + args[0].str + "' to int.");
        }
        throw std::runtime_error("[Runtime Error]: Invalid conversion to int.");
    }));

    globals["cast_float"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_float() expects 1 argument.");
        if (args[0].isFloat()) return args[0];
        if (args[0].isInt()) return Value(static_cast<double>(args[0].intVal));
        if (args[0].isBool()) return Value(args[0].boolean ? 1.0 : 0.0);
        if (args[0].isString()) {
            try {
                size_t pos = 0;
                double val = std::stod(args[0].str, &pos);
                if (pos == args[0].str.length()) return Value(val);
            } catch (...) {}
            throw std::runtime_error("[Runtime Error]: Cannot cast string '" + args[0].str + "' to float.");
        }
        throw std::runtime_error("[Runtime Error]: Invalid conversion to float.");
    }));

    globals["cast_string"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_string() expects 1 argument.");
        return Value(args[0].toString());
    }));

    globals["cast_char"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_char() expects 1 argument.");
        if (args[0].isChar()) return args[0];
        if (args[0].isString()) {
            std::string s = args[0].str;
            if (s.empty()) throw std::runtime_error("[Runtime Error]: Cannot cast empty string to char.");
            // Parse UTF-8 character length
            unsigned char ch = static_cast<unsigned char>(s[0]);
            size_t charLen = 1;
            char32_t code = 0;
            if (ch < 0x80) { code = ch; charLen = 1; }
            else if ((ch & 0xE0) == 0xC0) { code = (ch & 0x1F) << 6 | (s[1] & 0x3F); charLen = 2; }
            else if ((ch & 0xF0) == 0xE0) { code = (ch & 0x0F) << 12 | (s[1] & 0x3F) << 6 | (s[2] & 0x3F); charLen = 3; }
            else if ((ch & 0xF8) == 0xF0) { code = (ch & 0x07) << 18 | (s[1] & 0x3F) << 12 | (s[2] & 0x3F) << 6 | (s[3] & 0x3F); charLen = 4; }

            if (charLen != s.length()) {
                throw std::runtime_error("[Runtime Error]: cast_char() requires a single-character string.");
            }
            return Value(code, true);
        }
        throw std::runtime_error("[Runtime Error]: cast_char() requires an actual single-character value.");
    }));

    globals["cast_array"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_array() expects 1 argument.");
        if (args[0].isArray()) return args[0];
        if (args[0].isString()) {
            ArrayPtr arr = std::make_shared<ObjArray>();
            for (char c : args[0].str) {
                arr->push_back(Value(static_cast<char32_t>(c), true));
            }
            return Value(arr);
        }
        throw std::runtime_error("[Runtime Error]: Cannot cast value to array.");
    }));

    globals["cast_map"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: cast_map() expects 1 argument.");
        if (args[0].isMap()) return args[0];
        throw std::runtime_error("[Runtime Error]: Cannot cast value to map.");
    }));

    // String Builtins
    globals["upper"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: upper() expects 1 argument.");
        if (!args[0].isString()) throw std::runtime_error("[Runtime Error]: upper() expects string.");
        std::string s = args[0].str;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return Value(s);
    }));

    globals["lower"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: lower() expects 1 argument.");
        if (!args[0].isString()) throw std::runtime_error("[Runtime Error]: lower() expects string.");
        std::string s = args[0].str;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return Value(s);
    }));

    globals["trim"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: trim() expects 1 argument.");
        if (!args[0].isString()) throw std::runtime_error("[Runtime Error]: trim() expects string.");
        return Value(trimString(args[0].str));
    }));

    globals["contains"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: contains() expects 2 arguments.");
        if (!args[0].isString() || !args[1].isString()) throw std::runtime_error("[Runtime Error]: contains() expects string arguments.");
        return Value(args[0].str.find(args[1].str) != std::string::npos);
    }));

    globals["starts_with"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: starts_with() expects 2 arguments.");
        if (!args[0].isString() || !args[1].isString()) throw std::runtime_error("[Runtime Error]: starts_with() expects string arguments.");
        return Value(args[0].str.rfind(args[1].str, 0) == 0);
    }));

    globals["ends_with"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: ends_with() expects 2 arguments.");
        if (!args[0].isString() || !args[1].isString()) throw std::runtime_error("[Runtime Error]: ends_with() expects string arguments.");
        if (args[1].str.length() > args[0].str.length()) return Value(false);
        return Value(args[0].str.compare(args[0].str.length() - args[1].str.length(), args[1].str.length(), args[1].str) == 0);
    }));

    globals["split"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: split() expects 2 arguments.");
        if (!args[0].isString() || !args[1].isString()) throw std::runtime_error("[Runtime Error]: split() expects string arguments.");
        ArrayPtr arr = std::make_shared<ObjArray>();
        std::string str = args[0].str;
        std::string delim = args[1].str;
        if (delim.empty()) {
            for (char c : str) arr->push_back(Value(std::string(1, c)));
        } else {
            size_t start = 0;
            size_t end = str.find(delim);
            while (end != std::string::npos) {
                arr->push_back(Value(str.substr(start, end - start)));
                start = end + delim.length();
                end = str.find(delim, start);
            }
            arr->push_back(Value(str.substr(start)));
        }
        return Value(arr);
    }));

    globals["join"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 2) throw std::runtime_error("[Runtime Error]: join() expects 2 arguments.");
        if (!args[0].isArray() || !args[1].isString()) throw std::runtime_error("[Runtime Error]: join() expects array and string arguments.");
        std::string result = "";
        std::string delim = args[1].str;
        if (args[0].array) {
            for (size_t i = 0; i < args[0].array->size(); i++) {
                if (i > 0) result += delim;
                result += (*args[0].array)[i].toString();
            }
        }
        return Value(result);
    }));

    globals["replace"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 3) throw std::runtime_error("[Runtime Error]: replace() expects 3 arguments.");
        if (!args[0].isString() || !args[1].isString() || !args[2].isString()) {
            throw std::runtime_error("[Runtime Error]: replace() expects string arguments.");
        }
        std::string str = args[0].str;
        std::string from = args[1].str;
        std::string to = args[2].str;
        if (from.empty()) return Value(str);
        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
        return Value(str);
    }));

    // Math Functions
    globals["clock"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        (void)args;
        if (argCount != 0) throw std::runtime_error("[Runtime Error]: clock() expects 0 arguments.");
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
        double seconds = std::chrono::duration<double>(now).count();
        return Value(seconds);
    }));

    globals["rand"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        (void)args;
        if (argCount != 0) throw std::runtime_error("[Runtime Error]: rand() expects 0 arguments.");
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value(dist(rng));
    }));

    globals["abs"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: abs() expects 1 argument.");
        if (args[0].isInt()) return Value(std::abs(args[0].intVal));
        if (args[0].isFloat()) return Value(std::abs(args[0].floatVal));
        throw std::runtime_error("[Runtime Error]: abs() expects a number.");
    }));

    globals["floor"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: floor() expects 1 argument.");
        if (args[0].isInt()) return args[0];
        if (args[0].isFloat()) return Value(std::floor(args[0].floatVal));
        throw std::runtime_error("[Runtime Error]: floor() expects a number.");
    }));

    globals["ceil"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: ceil() expects 1 argument.");
        if (args[0].isInt()) return args[0];
        if (args[0].isFloat()) return Value(std::ceil(args[0].floatVal));
        throw std::runtime_error("[Runtime Error]: ceil() expects a number.");
    }));

    globals["sqrt"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 1) throw std::runtime_error("[Runtime Error]: sqrt() expects 1 argument.");
        if (!args[0].isNumber()) throw std::runtime_error("[Runtime Error]: sqrt() expects a number.");
        double val = args[0].asFloat();
        if (val < 0.0) throw std::runtime_error("[Runtime Error]: Cannot calculate square root of negative number.");
        return Value(std::sqrt(val));
    }));

    globals["clamp"] = Value(NativeFn([](int argCount, Value* args) -> Value {
        if (argCount != 3) throw std::runtime_error("[Runtime Error]: clamp() expects 3 arguments.");
        if (!args[0].isNumber() || !args[1].isNumber() || !args[2].isNumber()) {
            throw std::runtime_error("[Runtime Error]: clamp() expects numbers.");
        }
        if (args[1].asFloat() > args[2].asFloat()) {
            throw std::runtime_error("[Runtime Error]: clamp() min value cannot be greater than max value.");
        }
        if (args[0].isInt() && args[1].isInt() && args[2].isInt()) {
            int64_t val = args[0].intVal;
            int64_t minVal = args[1].intVal;
            int64_t maxVal = args[2].intVal;
            return Value(std::max(minVal, std::min(val, maxVal)));
        }
        double val = args[0].asFloat();
        double minVal = args[1].asFloat();
        double maxVal = args[2].asFloat();
        return Value(std::max(minVal, std::min(val, maxVal)));
    }));
}

void VM::run(Chunk& mainChunk) {
    resetStack();
    grabSnapshots.clear();

    struct VMRunGuard {
        VM* vm;
        VMRunGuard(VM* v) : vm(v) {}
        ~VMRunGuard() {
            if (!vm->grabSnapshots.empty()) {
                vm->globals = vm->grabSnapshots.front();
                vm->grabSnapshots.clear();
            }
            vm->resetStack();
        }
    } runGuard(this);

    FunctionPtr mainFn = std::make_shared<ObjFunction>();
    mainFn->chunk = mainChunk;
    mainFn->localTypes = mainChunk.localTypes;
    mainFn->name = "main";
    mainFn->arity = 0;

    push(Value(mainFn));
    call(mainFn, 0);

    CallFrame* frame = &frames.back();

    while (true) {
        OpCode instruction = static_cast<OpCode>(*frame->ip++);
        switch (instruction) {
            case OpCode::OP_CONSTANT: {
                uint16_t index = read16(frame->ip);
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
                uint16_t index = read16(frame->ip);
                std::string name = frame->function->chunk.constants[index].str;
                globals[name] = pop();
                globalTypes[name] = TypeSpec{TypeKind::ANY};
                break;
            }
            case OpCode::OP_DEFINE_GLOBAL_TYPED: {
                uint16_t index = read16(frame->ip);
                uint16_t typeSpecIdx = read16(frame->ip);
                TypeSpec expected = parseTypeSpecString(frame->function->chunk.constants[typeSpecIdx].str);
                std::string name = frame->function->chunk.constants[index].str;
                Value val = pop();
                if (!checkAndCoerceValueType(expected, val)) {
                    std::cout << "[Runtime Error]: Type mismatch for global variable '" << name
                              << "': expected " << expected.toString() << " but got "
                              << val.getTypeSpec().toString() << "." << std::endl;
                    return;
                }
                globals[name] = val;
                globalTypes[name] = expected;
                break;
            }
            case OpCode::OP_CHECK_LOCAL_TYPE: {
                uint16_t typeSpecIdx = read16(frame->ip);
                TypeSpec expected = parseTypeSpecString(frame->function->chunk.constants[typeSpecIdx].str);
                Value val = peek(0);
                if (!checkAndCoerceValueType(expected, val)) {
                    std::cout << "[Runtime Error]: Type mismatch: expected "
                              << expected.toString() << " but got "
                              << val.getTypeSpec().toString() << "." << std::endl;
                    return;
                }
                stack.back() = val;
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint16_t index = read16(frame->ip);
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
                uint16_t index = read16(frame->ip);
                std::string name = frame->function->chunk.constants[index].str;
                auto it = globals.find(name);
                if (it == globals.end()) {
                    std::cout << "[Runtime Error]: Variable '" << name << "' is not defined." << std::endl;
                    return;
                }
                auto typeIt = globalTypes.find(name);
                if (typeIt != globalTypes.end() && typeIt->second.kind != TypeKind::ANY && typeIt->second.kind != TypeKind::UNTYPED) {
                    TypeSpec expected = typeIt->second;
                    Value val = peek(0);
                    if (!checkAndCoerceValueType(expected, val)) {
                        std::cout << "[Runtime Error]: Type mismatch for variable '" << name
                                  << "': expected " << expected.toString() << " but got "
                                  << val.getTypeSpec().toString() << "." << std::endl;
                        return;
                    }
                    it->second = val;
                } else {
                    it->second = peek(0);
                }
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint16_t slot = read16(frame->ip);
                push(stack[frame->slotsOffset + slot]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint16_t slot = read16(frame->ip);
                if (slot < frame->function->localTypes.size()) {
                    TypeSpec expected = frame->function->localTypes[slot];
                    if (expected.kind != TypeKind::ANY && expected.kind != TypeKind::UNTYPED) {
                        Value val = peek(0);
                        if (!checkAndCoerceValueType(expected, val)) {
                            std::cout << "[Runtime Error]: Type mismatch for local variable: expected "
                                      << expected.toString() << " but got "
                                      << val.getTypeSpec().toString() << "." << std::endl;
                            return;
                        }
                        stack[frame->slotsOffset + slot] = val;
                        break;
                    }
                }
                stack[frame->slotsOffset + slot] = peek(0);
                break;
            }
            case OpCode::OP_CALL: {
                uint16_t argCount = read16(frame->ip);
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
                uint16_t elementCount = read16(frame->ip);
                ArrayPtr arr = std::make_shared<ObjArray>();
                arr->resize(elementCount);
                for (int i = elementCount - 1; i >= 0; --i) {
                    (*arr)[i] = pop();
                }
                push(Value(arr));
                break;
            }
            case OpCode::OP_BUILD_MAP: {
                uint16_t entryCount = read16(frame->ip);
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
                    if (!indexVal.isInt()) {
                        std::cout << "[Runtime Error]: Array index must be an integer." << std::endl;
                        return;
                    }
                    int64_t index = indexVal.intVal;
                    if (!target.array || index < 0 || index >= static_cast<int64_t>(target.array->size())) {
                        std::cout << "[Runtime Error]: Array index " << index << " out of bounds." << std::endl;
                        return;
                    }
                    push((*target.array)[index]);
                } else if (target.isString()) {
                    if (!indexVal.isInt()) {
                        std::cout << "[Runtime Error]: String index must be an integer." << std::endl;
                        return;
                    }
                    int64_t index = indexVal.intVal;
                    if (index < 0 || index >= static_cast<int64_t>(target.str.length())) {
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
                    if (target.map->typeSpec.valueKind != TypeKind::ANY && target.map->typeSpec.valueKind != TypeKind::UNTYPED) {
                        TypeSpec expectedValSpec = target.map->typeSpec.valType ? *target.map->typeSpec.valType : TypeSpec{target.map->typeSpec.valueKind};
                        if (!checkAndCoerceValueType(expectedValSpec, val)) {
                            std::cout << "[Runtime Error]: Type mismatch for map assignment: expected "
                                      << expectedValSpec.toString() << " but got "
                                      << val.getTypeSpec().toString() << "." << std::endl;
                            return;
                        }
                    }
                    target.map->set(indexVal.str, val);
                    push(val);
                } else if (target.isArray()) {
                    if (!indexVal.isInt()) {
                        std::cout << "[Runtime Error]: Array index must be an integer." << std::endl;
                        return;
                    }
                    int64_t index = indexVal.intVal;
                    if (!target.array || index < 0 || index >= static_cast<int64_t>(target.array->size())) {
                        std::cout << "[Runtime Error]: Array index " << index << " out of bounds." << std::endl;
                        return;
                    }
                    if (target.array->typeSpec.elementKind != TypeKind::ANY && target.array->typeSpec.elementKind != TypeKind::UNTYPED) {
                        TypeSpec expectedElemSpec = target.array->typeSpec.elemType ? *target.array->typeSpec.elemType : TypeSpec{target.array->typeSpec.elementKind};
                        if (!checkAndCoerceValueType(expectedElemSpec, val)) {
                            std::cout << "[Runtime Error]: Type mismatch for array assignment: expected "
                                      << expectedElemSpec.toString() << " but got "
                                      << val.getTypeSpec().toString() << "." << std::endl;
                            return;
                        }
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
                grabSnapshots.push_back(globals);
                push(Value(grabFn));
                if (!call(grabFn, 0, true)) {
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
                if (a.isInt() && b.isInt()) {
                    push(Value(a.intVal > b.intVal));
                } else {
                    push(Value(a.asFloat() > b.asFloat()));
                }
                break;
            }
            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '<' only supports numbers!" << std::endl;
                    return;
                }
                if (a.isInt() && b.isInt()) {
                    push(Value(a.intVal < b.intVal));
                } else {
                    push(Value(a.asFloat() < b.asFloat()));
                }
                break;
            }
            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                if (a.isString() || b.isString() || a.isChar() || b.isChar()) {
                    push(Value(a.toString() + b.toString()));
                } else if (a.isInt() && b.isInt()) {
                    int64_t res;
                    if (__builtin_add_overflow(a.intVal, b.intVal, &res)) {
                        std::cout << "[Runtime Error]: 64-bit integer addition overflow." << std::endl;
                        return;
                    }
                    push(Value(res));
                } else if (a.isNumber() && b.isNumber()) {
                    push(Value(a.asFloat() + b.asFloat()));
                } else {
                    std::cout << "[Runtime Error]: '+' operands must be numbers, strings, or chars." << std::endl;
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
                if (a.isInt() && b.isInt()) {
                    int64_t res;
                    if (__builtin_sub_overflow(a.intVal, b.intVal, &res)) {
                        std::cout << "[Runtime Error]: 64-bit integer subtraction overflow." << std::endl;
                        return;
                    }
                    push(Value(res));
                } else {
                    push(Value(a.asFloat() - b.asFloat()));
                }
                break;
            }
            case OpCode::OP_MULTIPLY: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '*' only supports numbers!" << std::endl;
                    return;
                }
                if (a.isInt() && b.isInt()) {
                    int64_t res;
                    if (__builtin_mul_overflow(a.intVal, b.intVal, &res)) {
                        std::cout << "[Runtime Error]: 64-bit integer multiplication overflow." << std::endl;
                        return;
                    }
                    push(Value(res));
                } else {
                    push(Value(a.asFloat() * b.asFloat()));
                }
                break;
            }
            case OpCode::OP_DIVIDE: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '/' only supports numbers!" << std::endl;
                    return;
                }
                if (b.asFloat() == 0.0) {
                    std::cout << "[Runtime Error]: Division by zero!" << std::endl;
                    return;
                }
                if (a.isInt() && b.isInt()) {
                    if (a.intVal == std::numeric_limits<int64_t>::min() && b.intVal == -1) {
                        std::cout << "[Runtime Error]: 64-bit integer division overflow." << std::endl;
                        return;
                    }
                    if (a.intVal % b.intVal == 0) {
                        push(Value(a.intVal / b.intVal));
                    } else {
                        push(Value(static_cast<double>(a.intVal) / static_cast<double>(b.intVal)));
                    }
                } else {
                    push(Value(a.asFloat() / b.asFloat()));
                }
                break;
            }
            case OpCode::OP_MODULO: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '%' only supports numbers!" << std::endl;
                    return;
                }
                if (b.asFloat() == 0.0) {
                    std::cout << "[Runtime Error]: Modulo by zero!" << std::endl;
                    return;
                }
                if (a.isInt() && b.isInt()) {
                    if (a.intVal == std::numeric_limits<int64_t>::min() && b.intVal == -1) {
                        std::cout << "[Runtime Error]: 64-bit integer modulo overflow." << std::endl;
                        return;
                    }
                    push(Value(a.intVal % b.intVal));
                } else {
                    push(Value(std::fmod(a.asFloat(), b.asFloat())));
                }
                break;
            }
            case OpCode::OP_POWER: {
                Value b = pop();
                Value a = pop();
                if (!a.isNumber() || !b.isNumber()) {
                    std::cout << "[Runtime Error]: '^' only supports numbers!" << std::endl;
                    return;
                }
                if (a.asFloat() == 0.0 && b.asFloat() < 0.0) {
                    std::cout << "[Runtime Error]: Zero cannot be raised to a negative power." << std::endl;
                    return;
                }
                if (a.isInt() && b.isInt() && b.intVal >= 0) {
                    bool overflow = false;
                    int64_t result = 1;
                    int64_t base = a.intVal;
                    int64_t exp = b.intVal;

                    while (exp > 0) {
                        if (exp & 1) {
                            if (__builtin_mul_overflow(result, base, &result)) {
                                overflow = true;
                                break;
                            }
                        }
                        exp >>= 1;
                        if (exp > 0) {
                            if (__builtin_mul_overflow(base, base, &base)) {
                                overflow = true;
                                break;
                            }
                        }
                    }

                    if (overflow) {
                        std::cout << "[Runtime Error]: 64-bit integer power overflow." << std::endl;
                        return;
                    }
                    push(Value(result));
                } else {
                    double powRes = std::pow(a.asFloat(), b.asFloat());
                    if (std::isinf(powRes) || std::isnan(powRes)) {
                        std::cout << "[Runtime Error]: Floating point power overflow or invalid result." << std::endl;
                        return;
                    }
                    push(Value(powRes));
                }
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
            case OpCode::OP_DUP: {
                push(peek(0));
                break;
            }
            case OpCode::OP_DUP_2: {
                Value v1 = peek(1);
                Value v0 = peek(0);
                push(v1);
                push(v0);
                break;
            }
            case OpCode::OP_RETURN: {
                Value result = pop();
                size_t slotsOffset = frame->slotsOffset;
                bool wasGrab = frame->isGrab;
                frames.pop_back();
                if (wasGrab && !grabSnapshots.empty()) {
                    grabSnapshots.pop_back();
                }
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
