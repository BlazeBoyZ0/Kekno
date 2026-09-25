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
#include <stdexcept>
#include <algorithm>
#include "chunk.h"

enum class ValueType { INT, FLOAT, CHAR, STRING, BOOL, NIL, FUNCTION, ARRAY, MAP, NATIVE, MODULE, SLICE, STRUCT_DEF, STRUCT_INSTANCE };

enum class TypeKind {
    ANY, INT, FLOAT, CHAR, STRING, BOOL, ARRAY, MAP, UNTYPED, FUNC, STRUCT
};

struct TypeSpec {
    TypeKind kind = TypeKind::ANY;
    TypeKind elementKind = TypeKind::ANY;
    TypeKind keyKind = TypeKind::ANY;
    TypeKind valueKind = TypeKind::ANY;
    std::string structName = "";

    std::shared_ptr<TypeSpec> elemType = nullptr;
    std::shared_ptr<TypeSpec> keyType = nullptr;
    std::shared_ptr<TypeSpec> valType = nullptr;

    bool operator==(const TypeSpec& other) const {
        if (kind != other.kind || elementKind != other.elementKind ||
            keyKind != other.keyKind || valueKind != other.valueKind) return false;
        if (kind == TypeKind::STRUCT && structName != other.structName) return false;
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
struct ObjModule;
struct ObjSlice;
struct ObjStructDef;
struct ObjStructInstance;

using UpvaluePtr = std::shared_ptr<ObjUpvalue>;
using FunctionPtr = std::shared_ptr<ObjFunction>;
using ClosurePtr = std::shared_ptr<ObjClosure>;
using ModulePtr = std::shared_ptr<ObjModule>;
using SlicePtr = std::shared_ptr<ObjSlice>;
using StructDefPtr = std::shared_ptr<ObjStructDef>;
using StructInstancePtr = std::shared_ptr<ObjStructInstance>;
using NativeFn = std::function<Value(int argCount, Value* args, const std::vector<std::string>& argNames)>;

struct StructField {
    std::string name;
    bool isConst = false;
    TypeSpec typeSpec;
};

struct ObjStructDef {
    std::string name;
    std::vector<StructField> fields;
    std::unordered_map<std::string, size_t> fieldIndices;
    ModulePtr module = nullptr;
};

struct ObjStructInstance {
    StructDefPtr def = nullptr;
    std::vector<Value> fields;
};

struct SymbolInfo {
    bool isPublic = false;
    bool isConst = false;
    TypeSpec typeSpec;
};

struct ObjModule {
    std::string name;
    std::string path;
    std::unordered_map<std::string, Value> globals;
    std::unordered_map<std::string, SymbolInfo> symbols;
    bool isInitialized = false;
};

struct ObjArray {
    std::vector<Value> elements;
    TypeSpec typeSpec{TypeKind::ARRAY};
    int lockCount = 0;

    void checkLock() const {
        if (lockCount > 0) {
            throw std::runtime_error("[Runtime Error]: Cannot structurally mutate collection during higher-order iteration.");
        }
    }

    size_t size() const { return elements.size(); }
    bool empty() const { return elements.empty(); }
    void resize(size_t n) { checkLock(); elements.resize(n); }
    void push_back(const Value& val) { checkLock(); elements.push_back(val); }
    Value& back() { return elements.back(); }
    void pop_back() { checkLock(); elements.pop_back(); }
    void erase(std::vector<Value>::iterator it) { checkLock(); elements.erase(it); }
    auto begin() { return elements.begin(); }
    auto end() { return elements.end(); }
    Value& operator[](size_t idx) { return elements[idx]; }
    const Value& operator[](size_t idx) const { return elements[idx]; }
};

using ArrayPtr = std::shared_ptr<ObjArray>;
using MapPtr = std::shared_ptr<ObjMap>;

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
    ModulePtr module;
    SlicePtr slice;
    StructDefPtr structDef;
    StructInstancePtr structInstance;

    Value() : type(ValueType::NIL), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(int64_t i) : type(ValueType::INT), intVal(i), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(double f) : type(ValueType::FLOAT), floatVal(f), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(char32_t c, bool /*isChar*/) : type(ValueType::CHAR), charVal(c), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(std::string s) : type(ValueType::STRING), intVal(0), str(s), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(bool b) : type(ValueType::BOOL), intVal(0), str(""), boolean(b), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(FunctionPtr fn);
    Value(ClosurePtr cl);
    Value(ArrayPtr arr) : type(ValueType::ARRAY), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(arr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(MapPtr m) : type(ValueType::MAP), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(m), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(NativeFn nfn) : type(ValueType::NATIVE), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nfn), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(ModulePtr mod) : type(ValueType::MODULE), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(mod), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}
    Value(SlicePtr sl) : type(ValueType::SLICE), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(sl), structDef(nullptr), structInstance(nullptr) {}
    Value(StructDefPtr def) : type(ValueType::STRUCT_DEF), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(def), structInstance(nullptr) {}
    Value(StructInstancePtr inst) : type(ValueType::STRUCT_INSTANCE), intVal(0), str(""), boolean(false), function(nullptr), closure(nullptr), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(inst) {}

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
    bool isModule() const { return type == ValueType::MODULE; }
    bool isSlice() const { return type == ValueType::SLICE; }
    bool isStructDef() const { return type == ValueType::STRUCT_DEF; }
    bool isStructInstance() const { return type == ValueType::STRUCT_INSTANCE; }

    double asFloat() const {
        if (type == ValueType::INT) return static_cast<double>(intVal);
        if (type == ValueType::FLOAT) return floatVal;
        return 0.0;
    }

    bool isFalsey() const;

    bool isEqual(const Value& other) const {
        std::vector<std::pair<const void*, const void*>> visited;
        return isEqualCycleSafe(other, visited);
    }

    bool isEqualCycleSafe(const Value& other, std::vector<std::pair<const void*, const void*>>& visited) const;

    std::string toString() const;
    std::string toStringCycleSafe(std::vector<const void*>& visited) const;
    TypeSpec getTypeSpec() const;
};

struct ObjSlice {
    Value start;
    Value end;
    Value step;
    bool hasStart = false;
    bool hasEnd = false;
    bool hasStep = false;
    bool isCallMarker = false;
};

struct MapKey {
    Value val;
    bool operator==(const MapKey& other) const {
        return val.isEqual(other.val);
    }
};

struct MapKeyHash {
    std::size_t operator()(const MapKey& k) const {
        const Value& v = k.val;
        if (v.isNumber()) {
            return std::hash<double>()(v.asFloat());
        }
        if (v.isString()) return std::hash<std::string>()(v.str);
        if (v.isChar()) return std::hash<char32_t>()(v.charVal);
        if (v.isBool()) return std::hash<bool>()(v.boolean);
        return 0;
    }
};

struct ObjMap {
    std::unordered_map<MapKey, Value, MapKeyHash> table;
    std::vector<Value> keys;
    TypeSpec typeSpec{TypeKind::MAP};
    int lockCount = 0;

    void checkLock() const {
        if (lockCount > 0) {
            throw std::runtime_error("[Runtime Error]: Cannot structurally mutate collection during higher-order iteration.");
        }
    }

    static bool isSupportedKey(const Value& val) {
        return val.isInt() || val.isFloat() || val.isString() || val.isChar() || val.isBool();
    }

    void set(const Value& key, const Value& val) {
        checkLock();
        MapKey mk{key};
        auto it = table.find(mk);
        if (it != table.end()) {
            for (auto kIt = keys.begin(); kIt != keys.end(); ++kIt) {
                if (kIt->isEqual(key)) {
                    keys.erase(kIt);
                    break;
                }
            }
            keys.push_back(key);
            it->second = val;
        } else {
            keys.push_back(key);
            table[mk] = val;
        }
    }

    Value get(const Value& key) const {
        MapKey mk{key};
        auto it = table.find(mk);
        if (it != table.end()) return it->second;
        return Value();
    }

    bool contains(const Value& key) const {
        MapKey mk{key};
        return table.find(mk) != table.end();
    }

    bool remove(const Value& key, Value* outVal = nullptr) {
        checkLock();
        MapKey mk{key};
        auto it = table.find(mk);
        if (it != table.end()) {
            if (outVal) *outVal = it->second;
            table.erase(it);
            for (auto kIt = keys.begin(); kIt != keys.end(); ++kIt) {
                if (kIt->isEqual(key)) {
                    keys.erase(kIt);
                    break;
                }
            }
            return true;
        }
        return false;
    }
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
    std::vector<std::string> paramNames;
    std::vector<TypeSpec> paramTypes;
    std::vector<TypeSpec> localTypes;
    ModulePtr module = nullptr;
};

struct ObjClosure {
    FunctionPtr function;
    std::vector<UpvaluePtr> upvalues;
    ModulePtr module = nullptr;
};

inline Value::Value(FunctionPtr fn)
    : type(ValueType::FUNCTION), intVal(0), str(""), boolean(false), function(fn), closure(std::make_shared<ObjClosure>()), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {
    closure->function = fn;
}

inline Value::Value(ClosurePtr cl)
    : type(ValueType::FUNCTION), intVal(0), str(""), boolean(false), function(cl ? cl->function : nullptr), closure(cl), array(nullptr), map(nullptr), nativeFn(nullptr), module(nullptr), slice(nullptr), structDef(nullptr), structInstance(nullptr) {}

inline bool Value::isFalsey() const {
    if (isNil()) return true;
    if (isBool()) return !boolean;
    if (isInt()) return intVal == 0;
    if (isFloat()) return floatVal == 0.0;
    if (isChar()) return charVal == 0;
    if (isString()) return str.empty();
    if (isArray()) return array == nullptr || array->empty();
    if (isMap()) return map == nullptr || map->keys.empty();
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
        case TypeKind::STRUCT: return structName.empty() ? "struct" : structName;
    }
    return "any";
}

inline std::string Value::toString() const {
    std::vector<const void*> visited;
    return toStringCycleSafe(visited);
}

inline std::string Value::toStringCycleSafe(std::vector<const void*>& visited) const {
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
    if (isModule()) {
        if (module && !module->name.empty()) {
            return "<module '" + module->name + "'>";
        }
        return "<module>";
    }
    if (isFunction()) {
        if (function && !function->name.empty()) {
            return "<task " + function->name + ">";
        }
        return "<task>";
    }
    if (isArray()) {
        if (!array) return "[]";
        const void* ptr = static_cast<const void*>(array.get());
        if (std::find(visited.begin(), visited.end(), ptr) != visited.end()) {
            return "[...]";
        }
        visited.push_back(ptr);
        std::string result = "[";
        for (size_t i = 0; i < array->size(); ++i) {
            if (i > 0) result += ", ";
            const Value& elem = (*array)[i];
            if (elem.isString()) {
                result += "\"" + elem.toStringCycleSafe(visited) + "\"";
            } else if (elem.isChar()) {
                result += "'" + elem.toStringCycleSafe(visited) + "'";
            } else {
                result += elem.toStringCycleSafe(visited);
            }
        }
        result += "]";
        visited.pop_back();
        return result;
    }
    if (isMap()) {
        if (!map) return "{}";
        const void* ptr = static_cast<const void*>(map.get());
        if (std::find(visited.begin(), visited.end(), ptr) != visited.end()) {
            return "{...}";
        }
        visited.push_back(ptr);
        std::string result = "{";
        for (size_t i = 0; i < map->keys.size(); ++i) {
            if (i > 0) result += ", ";
            const Value& k = map->keys[i];
            if (k.isString()) result += "\"" + k.toStringCycleSafe(visited) + "\": ";
            else if (k.isChar()) result += "'" + k.toStringCycleSafe(visited) + "': ";
            else result += k.toStringCycleSafe(visited) + ": ";

            Value val = map->get(k);
            if (val.isString()) {
                result += "\"" + val.toStringCycleSafe(visited) + "\"";
            } else if (val.isChar()) {
                result += "'" + val.toStringCycleSafe(visited) + "'";
            } else {
                result += val.toStringCycleSafe(visited);
            }
        }
        result += "}";
        visited.pop_back();
        return result;
    }
    if (isStructDef()) {
        if (structDef && !structDef->name.empty()) {
            return "<struct " + structDef->name + ">";
        }
        return "<struct>";
    }
    if (isStructInstance()) {
        if (!structInstance || !structInstance->def) return "struct{}";
        const void* ptr = static_cast<const void*>(structInstance.get());
        if (std::find(visited.begin(), visited.end(), ptr) != visited.end()) {
            return structInstance->def->name + "{...}";
        }
        visited.push_back(ptr);
        std::string result = structInstance->def->name + "{";
        for (size_t i = 0; i < structInstance->def->fields.size(); ++i) {
            if (i > 0) result += ", ";
            result += structInstance->def->fields[i].name + ": ";
            Value fVal = (i < structInstance->fields.size()) ? structInstance->fields[i] : Value();
            if (fVal.isString()) {
                result += "\"" + fVal.toStringCycleSafe(visited) + "\"";
            } else if (fVal.isChar()) {
                result += "'" + fVal.toStringCycleSafe(visited) + "'";
            } else {
                result += fVal.toStringCycleSafe(visited);
            }
        }
        result += "}";
        visited.pop_back();
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
    if (isStructInstance()) {
        TypeSpec spec;
        spec.kind = TypeKind::STRUCT;
        spec.structName = (structInstance && structInstance->def) ? structInstance->def->name : "";
        return spec;
    }
    if (isStructDef()) {
        TypeSpec spec;
        spec.kind = TypeKind::STRUCT;
        spec.structName = structDef ? structDef->name : "";
        return spec;
    }
    return TypeSpec{TypeKind::ANY};
}

inline bool Value::isEqualCycleSafe(const Value& other, std::vector<std::pair<const void*, const void*>>& visited) const {
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
        case ValueType::MODULE: return module == other.module;
        case ValueType::SLICE: return slice == other.slice;
        case ValueType::NATIVE: return false;
        case ValueType::STRUCT_DEF: return structDef == other.structDef;
        case ValueType::ARRAY: {
            if (array == other.array) return true;
            if (!array || !other.array) return false;
            if (array->size() != other.array->size()) return false;
            auto pair = std::make_pair(static_cast<const void*>(array.get()), static_cast<const void*>(other.array.get()));
            if (std::find(visited.begin(), visited.end(), pair) != visited.end()) return true;
            visited.push_back(pair);
            for (size_t i = 0; i < array->size(); ++i) {
                if (!(*array)[i].isEqualCycleSafe((*other.array)[i], visited)) {
                    visited.pop_back();
                    return false;
                }
            }
            visited.pop_back();
            return true;
        }
        case ValueType::MAP: {
            if (map == other.map) return true;
            if (!map || !other.map) return false;
            if (map->keys.size() != other.map->keys.size()) return false;
            auto pair = std::make_pair(static_cast<const void*>(map.get()), static_cast<const void*>(other.map.get()));
            if (std::find(visited.begin(), visited.end(), pair) != visited.end()) return true;
            visited.push_back(pair);
            for (size_t i = 0; i < map->keys.size(); ++i) {
                const Value& k = map->keys[i];
                if (!other.map->contains(k)) {
                    visited.pop_back();
                    return false;
                }
                if (!map->get(k).isEqualCycleSafe(other.map->get(k), visited)) {
                    visited.pop_back();
                    return false;
                }
            }
            visited.pop_back();
            return true;
        }
        case ValueType::STRUCT_INSTANCE: {
            if (structInstance == other.structInstance) return true;
            if (!structInstance || !other.structInstance) return false;
            if (!structInstance->def || !other.structInstance->def) return false;
            if (structInstance->def->name != other.structInstance->def->name) return false;
            if (structInstance->fields.size() != other.structInstance->fields.size()) return false;
            auto pair = std::make_pair(static_cast<const void*>(structInstance.get()), static_cast<const void*>(other.structInstance.get()));
            if (std::find(visited.begin(), visited.end(), pair) != visited.end()) return true;
            visited.push_back(pair);
            for (size_t i = 0; i < structInstance->fields.size(); ++i) {
                if (!structInstance->fields[i].isEqualCycleSafe(other.structInstance->fields[i], visited)) {
                    visited.pop_back();
                    return false;
                }
            }
            visited.pop_back();
            return true;
        }
    }
    return false;
}

inline Value copyValueCycleSafe(const Value& val, std::unordered_map<const void*, Value>& visited) {
    if (val.isStructInstance()) {
        if (!val.structInstance) return val;
        const void* ptr = static_cast<const void*>(val.structInstance.get());
        auto it = visited.find(ptr);
        if (it != visited.end()) return it->second;

        StructInstancePtr newInst = std::make_shared<ObjStructInstance>();
        newInst->def = val.structInstance->def;
        Value newVal(newInst);
        visited[ptr] = newVal;

        newInst->fields.resize(val.structInstance->fields.size());
        for (size_t i = 0; i < val.structInstance->fields.size(); ++i) {
            newInst->fields[i] = copyValueCycleSafe(val.structInstance->fields[i], visited);
        }
        return newVal;
    }
    if (val.isArray()) {
        if (!val.array) return val;
        const void* ptr = static_cast<const void*>(val.array.get());
        auto it = visited.find(ptr);
        if (it != visited.end()) return it->second;

        ArrayPtr newArr = std::make_shared<ObjArray>();
        newArr->typeSpec = val.array->typeSpec;
        Value newVal(newArr);
        visited[ptr] = newVal;

        for (size_t i = 0; i < val.array->elements.size(); ++i) {
            newArr->push_back(copyValueCycleSafe(val.array->elements[i], visited));
        }
        return newVal;
    }
    if (val.isMap()) {
        if (!val.map) return val;
        const void* ptr = static_cast<const void*>(val.map.get());
        auto it = visited.find(ptr);
        if (it != visited.end()) return it->second;

        MapPtr newMap = std::make_shared<ObjMap>();
        newMap->typeSpec = val.map->typeSpec;
        Value newVal(newMap);
        visited[ptr] = newVal;

        for (size_t i = 0; i < val.map->keys.size(); ++i) {
            Value kCopy = copyValueCycleSafe(val.map->keys[i], visited);
            Value vCopy = copyValueCycleSafe(val.map->get(val.map->keys[i]), visited);
            newMap->set(kCopy, vCopy);
        }
        return newVal;
    }
    return val;
}

inline Value copyValue(const Value& val) {
    std::unordered_map<const void*, Value> visited;
    return copyValueCycleSafe(val, visited);
}
