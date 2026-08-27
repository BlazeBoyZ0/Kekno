#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include "chunk.h"

enum class ValueType { NUMBER, STRING, BOOL, NIL, FUNCTION, ARRAY, MAP, NATIVE };

struct Value;
struct ObjMap;

using NativeFn = std::function<Value(int argCount, Value* args)>;

struct ObjFunction {
    int arity = 0;
    Chunk chunk;
    std::string name;
};

using ArrayPtr = std::shared_ptr<std::vector<Value>>;
using MapPtr = std::shared_ptr<ObjMap>;
using FunctionPtr = std::shared_ptr<ObjFunction>;

struct Value {
    ValueType type;
    double num;
    std::string str;
    bool boolean;
    FunctionPtr function;
    ArrayPtr array;
    MapPtr map;
    NativeFn nativeFn;

    Value() : type(ValueType::NIL), num(0.0), str(""), boolean(false), function(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(double n) : type(ValueType::NUMBER), num(n), str(""), boolean(false), function(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(std::string s) : type(ValueType::STRING), num(0.0), str(s), boolean(false), function(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(bool b) : type(ValueType::BOOL), num(0.0), str(""), boolean(b), function(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(FunctionPtr fn) : type(ValueType::FUNCTION), num(0.0), str(""), boolean(false), function(fn), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(ArrayPtr arr) : type(ValueType::ARRAY), num(0.0), str(""), boolean(false), function(nullptr), array(arr), map(nullptr), nativeFn(nullptr) {}
    Value(MapPtr m) : type(ValueType::MAP), num(0.0), str(""), boolean(false), function(nullptr), array(nullptr), map(m), nativeFn(nullptr) {}
    Value(NativeFn nfn) : type(ValueType::NATIVE), num(0.0), str(""), boolean(false), function(nullptr), array(nullptr), map(nullptr), nativeFn(nfn) {}

    bool isNumber() const { return type == ValueType::NUMBER; }
    bool isString() const { return type == ValueType::STRING; }
    bool isBool() const { return type == ValueType::BOOL; }
    bool isNil() const { return type == ValueType::NIL; }
    bool isFunction() const { return type == ValueType::FUNCTION; }
    bool isArray() const { return type == ValueType::ARRAY; }
    bool isMap() const { return type == ValueType::MAP; }
    bool isNative() const { return type == ValueType::NATIVE; }

    bool isFalsey() const;

    bool isEqual(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case ValueType::NIL: return true;
            case ValueType::BOOL: return boolean == other.boolean;
            case ValueType::NUMBER: return num == other.num;
            case ValueType::STRING: return str == other.str;
            case ValueType::FUNCTION: return function == other.function;
            case ValueType::ARRAY: return array == other.array;
            case ValueType::MAP: return map == other.map;
            case ValueType::NATIVE: return false;
        }
        return false;
    }

    std::string toString() const;
};

struct ObjMap {
    std::unordered_map<std::string, Value> table;
    std::vector<std::string> keys;

    void set(const std::string& key, const Value& val) {
        if (table.find(key) == table.end()) {
            keys.push_back(key);
        }
        table[key] = val;
    }

    bool remove(const std::string& key) {
        auto it = table.find(key);
        if (it != table.end()) {
            table.erase(it);
            for (auto kIt = keys.begin(); kIt != keys.end(); ++kIt) {
                if (*kIt == key) {
                    keys.erase(kIt);
                    break;
                }
            }
            return true;
        }
        return false;
    }
};

inline bool Value::isFalsey() const {
    if (isNil()) return true;
    if (isBool()) return !boolean;
    if (isNumber()) return num == 0.0;
    if (isString()) return str.empty();
    if (isArray()) return array == nullptr || array->empty();
    if (isMap()) return map == nullptr || map->table.empty();
    return false;
}

inline std::string Value::toString() const {
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
    if (isMap()) {
        std::string result = "{";
        if (map) {
            for (size_t i = 0; i < map->keys.size(); ++i) {
                if (i > 0) result += ", ";
                const std::string& k = map->keys[i];
                result += "\"" + k + "\": ";
                auto it = map->table.find(k);
                if (it != map->table.end()) {
                    if (it->second.isString()) {
                        result += "\"" + it->second.toString() + "\"";
                    } else {
                        result += it->second.toString();
                    }
                }
            }
        }
        result += "}";
        return result;
    }
    std::ostringstream ss;
    ss << num;
    return ss.str();
}
