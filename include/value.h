#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdint>
#include <iomanip>
#include <cmath>
#include "chunk.h"

enum class ValueType { INT, FLOAT, CHAR, STRING, BOOL, NIL, FUNCTION, ARRAY, MAP, NATIVE };

enum class TypeKind {
    ANY, INT, FLOAT, CHAR, STRING, BOOL, ARRAY, MAP, UNTYPED, FUNC
};

struct TypeSpec {
    TypeKind kind = TypeKind::ANY;
    TypeKind elementKind = TypeKind::ANY;
    TypeKind keyKind = TypeKind::ANY;
    TypeKind valueKind = TypeKind::ANY;

    std::shared_ptr<TypeSpec> elemType = nullptr;
    std::shared_ptr<TypeSpec> keyType = nullptr;
    std::shared_ptr<TypeSpec> valType = nullptr;

    bool operator==(const TypeSpec& other) const {
        if (kind != other.kind || elementKind != other.elementKind ||
            keyKind != other.keyKind || valueKind != other.valueKind) return false;
        if ((elemType == nullptr) != (other.elemType == nullptr)) return false;
        if (elemType && !(*elemType == *other.elemType)) return false;
        if ((keyType == nullptr) != (other.keyType == nullptr)) return false;
        if (keyType && !(*keyType == *other.keyType)) return false;
        if ((valType == nullptr) != (other.valType == nullptr)) return false;
        if (valType && !(*valType == *other.valType)) return false;
        return true;
    }
    bool operator!=(const TypeSpec& other) const {
        return !(*this == other);
    }
    std::string toString() const;
};

struct Value;
struct ObjMap;
struct ObjArray;
struct ObjUpvalue;
struct ObjFunction;
struct ObjClosure;

using UpvaluePtr = std::shared_ptr<ObjUpvalue>;
using FunctionPtr = std::shared_ptr<ObjFunction>;
using ClosurePtr = std::shared_ptr<ObjClosure>;
using NativeFn = std::function<Value(int argCount, Value* args)>;

struct ObjArray {
    std::vector<Value> elements;
    TypeSpec typeSpec{TypeKind::ARRAY};

    size_t size() const { return elements.size(); }
    bool empty() const { return elements.empty(); }
    void resize(size_t n) { elements.resize(n); }
    void push_back(const Value& val) { elements.push_back(val); }
    Value& back() { return elements.back(); }
    void pop_back() { elements.pop_back(); }
    void erase(std::vector<Value>::iterator it) { elements.erase(it); }
    auto begin() { return elements.begin(); }
    auto end() { return elements.end(); }
    Value& operator[](size_t idx) { return elements[idx]; }
    const Value& operator[](size_t idx) const { return elements[idx]; }
};

using ArrayPtr = std::shared_ptr<ObjArray>;
using MapPtr = std::shared_ptr<ObjMap>;
using FunctionPtr = std::shared_ptr<ObjFunction>;

struct Value {
    ValueType type;
    union {
        int64_t intVal;
        double floatVal;
        char32_t charVal;
    };
    std::string str;
    bool boolean;
    FunctionPtr function;
    ClosurePtr closure;
    ArrayPtr array;
    MapPtr map;
    NativeFn nativeFn;

    Value() : type(ValueType::NIL), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(int64_t i) : type(ValueType::INT), intVal(i), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(double f) : type(ValueType::FLOAT), floatVal(f), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(char32_t c, bool /*isChar*/) : type(ValueType::CHAR), charVal(c), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(std::string s) : type(ValueType::STRING), intVal(0), str(s), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(bool b) : type(ValueType::BOOL), intVal(0), str(""), boolean(b), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr) {}
    Value(FunctionPtr fn);
    Value(ClosurePtr cl);
    Value(ArrayPtr arr) : type(ValueType::ARRAY), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(arr), map(nullptr), nativeFn(nullptr) {}
    Value(MapPtr m) : type(ValueType::MAP), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(m), nativeFn(nullptr) {}
    Value(NativeFn nfn) : type(ValueType::NATIVE), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nfn) {}

    bool isInt() const { return type == ValueType::INT; }
    bool isFloat() const { return type == ValueType::FLOAT; }
    bool isNumber() const { return type == ValueType::INT || type == ValueType::FLOAT; }
    bool isChar() const { return type == ValueType::CHAR; }
    bool isString() const { return type == ValueType::STRING; }
    bool isBool() const { return type == ValueType::BOOL; }
    bool isNil() const { return type == ValueType::NIL; }
    bool isFunction() const { return type == ValueType::FUNCTION; }
    bool isArray() const { return type == ValueType::ARRAY; }
    bool isMap() const { return type == ValueType::MAP; }
    bool isNative() const { return type == ValueType::NATIVE; }

    double asFloat() const {
        if (type == ValueType::INT) return static_cast<double>(intVal);
        if (type == ValueType::FLOAT) return floatVal;
        return 0.0;
    }

    bool isFalsey() const;

    bool isEqual(const Value& other) const {
        if (isInt() && other.isInt()) return intVal == other.intVal;
        if (isFloat() && other.isFloat()) return floatVal == other.floatVal;
        if (isNumber() && other.isNumber()) {
            return asFloat() == other.asFloat();
        }
        if (type != other.type) return false;
        switch (type) {
            case ValueType::NIL: return true;
            case ValueType::BOOL: return boolean == other.boolean;
            case ValueType::INT: return intVal == other.intVal;
            case ValueType::FLOAT: return floatVal == other.floatVal;
            case ValueType::CHAR: return charVal == other.charVal;
            case ValueType::STRING: return str == other.str;
            case ValueType::FUNCTION: return closure == other.closure;
            case ValueType::ARRAY: return array == other.array;
            case ValueType::MAP: return map == other.map;
            case ValueType::NATIVE: return false;
        }
        return false;
    }

    std::string toString() const;
    TypeSpec getTypeSpec() const;
};

struct ObjUpvalue {
    size_t stackIndex = 0;
    Value closed;
    bool isClosed = false;
    TypeSpec typeSpec;
    bool isConst = false;
    std::shared_ptr<ObjUpvalue> next = nullptr;

    Value* getValuePtr(std::vector<Value>& stack) {
        return isClosed ? &closed : &stack[stackIndex];
    }
};

struct ObjFunction {
    int arity = 0;
    int upvalueCount = 0;
    Chunk chunk;
    std::string name;
    std::vector<TypeSpec> paramTypes;
    std::vector<TypeSpec> localTypes;
};

struct ObjClosure {
    FunctionPtr function;
    std::vector<UpvaluePtr> upvalues;
};

struct ObjMap {
    std::unordered_map<std::string, Value> table;
    std::vector<std::string> keys;
    TypeSpec typeSpec{TypeKind::MAP};

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

inline Value::Value(FunctionPtr fn)
    : type(ValueType::FUNCTION), intVal(0), str(""), boolean(false), function(fn), closure(std::make_shared<ObjClosure>()), array(nullptr), map(nullptr), nativeFn(nullptr) {
    closure->function = fn;
}

inline Value::Value(ClosurePtr cl)
    : type(ValueType::FUNCTION), intVal(0), str(""), boolean(false), function(cl ? cl->function : nullptr), closure(cl), array(nullptr), map(nullptr), nativeFn(nullptr) {}

inline bool Value::isFalsey() const {
    if (isNil()) return true;
    if (isBool()) return !boolean;
    if (isInt()) return intVal == 0;
    if (isFloat()) return floatVal == 0.0;
    if (isChar()) return charVal == 0;
    if (isString()) return str.empty();
    if (isArray()) return array == nullptr || array->empty();
    if (isMap()) return map == nullptr || map->table.empty();
    return false;
}

inline uint16_t encodeTypeSpec(const TypeSpec& spec) {
    return (static_cast<uint16_t>(spec.kind) << 12) |
           (static_cast<uint16_t>(spec.elementKind) << 8) |
           (static_cast<uint16_t>(spec.keyKind) << 4) |
           static_cast<uint16_t>(spec.valueKind);
}

inline TypeSpec decodeTypeSpec(uint16_t encoded) {
    TypeSpec spec;
    spec.kind = static_cast<TypeKind>((encoded >> 12) & 0x0F);
    spec.elementKind = static_cast<TypeKind>((encoded >> 8) & 0x0F);
    spec.keyKind = static_cast<TypeKind>((encoded >> 4) & 0x0F);
    spec.valueKind = static_cast<TypeKind>(encoded & 0x0F);
    return spec;
}

inline std::string TypeSpec::toString() const {
    switch (kind) {
        case TypeKind::ANY: return "any";
        case TypeKind::INT: return "int";
        case TypeKind::FLOAT: return "float";
        case TypeKind::CHAR: return "char";
        case TypeKind::STRING: return "string";
        case TypeKind::BOOL: return "bool";
        case TypeKind::ARRAY:
            if (elemType) {
                return "array<" + elemType->toString() + ">";
            }
            if (elementKind != TypeKind::ANY) {
                TypeSpec elem{elementKind};
                return "array<" + elem.toString() + ">";
            }
            return "array";
        case TypeKind::MAP:
            if (keyType || valType) {
                TypeSpec kSpec = keyType ? *keyType : TypeSpec{keyKind};
                TypeSpec vSpec = valType ? *valType : TypeSpec{valueKind};
                return "map<" + kSpec.toString() + ", " + vSpec.toString() + ">";
            }
            if (keyKind != TypeKind::ANY || valueKind != TypeKind::ANY) {
                TypeSpec kSpec{keyKind}, vSpec{valueKind};
                return "map<" + kSpec.toString() + ", " + vSpec.toString() + ">";
            }
            return "map";
        case TypeKind::FUNC: return "func";
        case TypeKind::UNTYPED: return "untyped";
    }
    return "any";
}

inline std::string Value::toString() const {
    if (isNil()) return "nil";
    if (isBool()) return boolean ? "true" : "false";
    if (isInt()) return std::to_string(intVal);
    if (isFloat()) {
        std::ostringstream ss;
        ss << floatVal;
        std::string s = ss.str();
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
            s += ".0";
        }
        return s;
    }
    if (isChar()) {
        std::string s;
        if (charVal <= 0x7F) {
            s += static_cast<char>(charVal);
        } else {
            if (charVal <= 0x7FF) {
                s += static_cast<char>(0xC0 | ((charVal >> 6) & 0x1F));
                s += static_cast<char>(0x80 | (charVal & 0x3F));
            } else if (charVal <= 0xFFFF) {
                s += static_cast<char>(0xE0 | ((charVal >> 12) & 0x0F));
                s += static_cast<char>(0x80 | ((charVal >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (charVal & 0x3F));
            } else {
                s += static_cast<char>(0xF0 | ((charVal >> 18) & 0x07));
                s += static_cast<char>(0x80 | ((charVal >> 12) & 0x3F));
                s += static_cast<char>(0x80 | ((charVal >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (charVal & 0x3F));
            }
        }
        return s;
    }
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
                } else if ((*array)[i].isChar()) {
                    result += "'" + (*array)[i].toString() + "'";
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
                    } else if (it->second.isChar()) {
                        result += "'" + it->second.toString() + "'";
                    } else {
                        result += it->second.toString();
                    }
                }
            }
        }
        result += "}";
        return result;
    }
    return "nil";
}

inline TypeSpec Value::getTypeSpec() const {
    if (isInt()) return TypeSpec{TypeKind::INT};
    if (isFloat()) return TypeSpec{TypeKind::FLOAT};
    if (isChar()) return TypeSpec{TypeKind::CHAR};
    if (isString()) return TypeSpec{TypeKind::STRING};
    if (isBool()) return TypeSpec{TypeKind::BOOL};
    if (isArray()) return array ? array->typeSpec : TypeSpec{TypeKind::ARRAY};
    if (isMap()) return map ? map->typeSpec : TypeSpec{TypeKind::MAP};
    if (isFunction() || isNative()) return TypeSpec{TypeKind::FUNC};
    return TypeSpec{TypeKind::ANY};
}
