#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <memory>
#include <functional>
#include "chunk.h"

enum class ValueType { NUMBER, STRING, BOOL, NIL, FUNCTION, ARRAY, NATIVE };

struct Value;

using NativeFn = std::function<Value(int argCount, Value* args)>;

struct ObjFunction {
    int arity = 0;
    Chunk chunk;
    std::string name;
};

using ArrayPtr = std::shared_ptr<std::vector<Value>>;
using FunctionPtr = std::shared_ptr<ObjFunction>;

struct Value {
    ValueType type;
    double num;
    std::string str;
    bool boolean;
    FunctionPtr function;
    ArrayPtr array;
    NativeFn nativeFn;

    Value() : type(ValueType::NIL), num(0.0), str(""), boolean(false), function(nullptr), array(nullptr), nativeFn(nullptr) {}
    Value(double n) : type(ValueType::NUMBER), num(n), str(""), boolean(false), function(nullptr), array(nullptr), nativeFn(nullptr) {}
    Value(std::string s) : type(ValueType::STRING), num(0.0), str(s), boolean(false), function(nullptr), array(nullptr), nativeFn(nullptr) {}
    Value(bool b) : type(ValueType::BOOL), num(0.0), str(""), boolean(b), function(nullptr), array(nullptr), nativeFn(nullptr) {}
    Value(FunctionPtr fn) : type(ValueType::FUNCTION), num(0.0), str(""), boolean(false), function(fn), array(nullptr), nativeFn(nullptr) {}
    Value(ArrayPtr arr) : type(ValueType::ARRAY), num(0.0), str(""), boolean(false), function(nullptr), array(arr), nativeFn(nullptr) {}
    Value(NativeFn nfn) : type(ValueType::NATIVE), num(0.0), str(""), boolean(false), function(nullptr), array(nullptr), nativeFn(nfn) {}

    bool isNumber() const { return type == ValueType::NUMBER; }
    bool isString() const { return type == ValueType::STRING; }
    bool isBool() const { return type == ValueType::BOOL; }
    bool isNil() const { return type == ValueType::NIL; }
    bool isFunction() const { return type == ValueType::FUNCTION; }
    bool isArray() const { return type == ValueType::ARRAY; }
    bool isNative() const { return type == ValueType::NATIVE; }

    bool isFalsey() const {
        if (isNil()) return true;
        if (isBool()) return !boolean;
        if (isNumber()) return num == 0.0;
        if (isString()) return str.empty();
        if (isArray()) return array == nullptr || array->empty();
        return false;
    }

    bool isEqual(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case ValueType::NIL: return true;
            case ValueType::BOOL: return boolean == other.boolean;
            case ValueType::NUMBER: return num == other.num;
            case ValueType::STRING: return str == other.str;
            case ValueType::FUNCTION: return function == other.function;
            case ValueType::ARRAY: return array == other.array;
            case ValueType::NATIVE: return false;
        }
        return false;
    }

    std::string toString() const {
        if (isNil()) return "nil";
        if (isBool()) return boolean ? "true" : "false";
        if (isString()) return str;
        if (isNative()) return "<native task>";
        if (isFunction()) {
            if (function && !function->name.empty()) {
                return "<task " + function->name + ">";
            }
            return "<task>";
        }
        if (isArray()) {
            std::string result = "[";
            if (array) {
                for (size_t i = 0; i < array->size(); ++i) {
                    if (i > 0) result += ", ";
                    if ((*array)[i].isString()) {
                        result += "\"" + (*array)[i].toString() + "\"";
                    } else {
                        result += (*array)[i].toString();
                    }
                }
            }
            result += "]";
            return result;
        }
        std::ostringstream ss;
        ss << num;
        return ss.str();
    }
};
