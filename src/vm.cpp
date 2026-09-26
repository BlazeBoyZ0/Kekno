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
#include <filesystem>

namespace fs = std::filesystem;

static uint16_t read16(const uint8_t*& ip) {
    uint16_t value = (static_cast<uint16_t>(ip[0]) << 8) | ip[1];
    ip += 2;
    return value;
}

// UTF-8 Helper Functions
static std::vector<std::string> utf8_to_chars(const std::string& str) {
    std::vector<std::string> chars;
    size_t i = 0;
    while (i < str.length()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        size_t len = 1;
        if (c < 0x80) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;

        if (i + len > str.length()) len = str.length() - i;
        chars.push_back(str.substr(i, len));
        i += len;
    }
    return chars;
}

static size_t utf8_length(const std::string& str) {
    return utf8_to_chars(str).size();
}

static char32_t utf8_code_point(const std::string& charStr) {
    if (charStr.empty()) return 0;
    unsigned char c = static_cast<unsigned char>(charStr[0]);
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && charStr.length() >= 2) {
        return ((c & 0x1F) << 6) | (static_cast<unsigned char>(charStr[1]) & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && charStr.length() >= 3) {
        return ((c & 0x0F) << 12) | ((static_cast<unsigned char>(charStr[1]) & 0x3F) << 6) | (static_cast<unsigned char>(charStr[2]) & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && charStr.length() >= 4) {
        return ((c & 0x07) << 18) | ((static_cast<unsigned char>(charStr[1]) & 0x3F) << 12) | ((static_cast<unsigned char>(charStr[2]) & 0x3F) << 6) | (static_cast<unsigned char>(charStr[3]) & 0x3F);
    }
    return c;
}

static bool is_unicode_space(char32_t cp) {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f') return true;
    if (cp == 0x00A0 || cp == 0x1680 || cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF) return true;
    if (cp >= 0x2000 && cp <= 0x200B) return true;
    if (cp == 0x2028 || cp == 0x2029) return true;
    return false;
}

static std::string utf8_trim(const std::string& str) {
    std::vector<std::string> chars = utf8_to_chars(str);
    if (chars.empty()) return "";

    size_t start = 0;
    while (start < chars.size() && is_unicode_space(utf8_code_point(chars[start]))) {
        start++;
    }

    size_t end = chars.size();
    while (end > start && is_unicode_space(utf8_code_point(chars[end - 1]))) {
        end--;
    }

    std::string result = "";
    for (size_t i = start; i < end; ++i) {
        result += chars[i];
    }
    return result;
}

static std::string utf8_upper(const std::string& str) {
    std::vector<std::string> chars = utf8_to_chars(str);
    std::string res = "";
    for (const auto& c : chars) {
        if (c.length() == 1) {
            res += static_cast<char>(std::toupper(static_cast<unsigned char>(c[0])));
        } else {
            res += c;
        }
    }
    return res;
}

static std::string utf8_lower(const std::string& str) {
    std::vector<std::string> chars = utf8_to_chars(str);
    std::string res = "";
    for (const auto& c : chars) {
        if (c.length() == 1) {
            res += static_cast<char>(std::tolower(static_cast<unsigned char>(c[0])));
        } else {
            res += c;
        }
    }
    return res;
}

struct ArgMap {
    std::unordered_map<std::string, Value> map;
    bool has(const std::string& name) const { return map.find(name) != map.end(); }
    Value get(const std::string& name, Value defaultVal = Value()) const {
        auto it = map.find(name);
        return (it != map.end()) ? it->second : defaultVal;
    }
};

static ArgMap parseCallArgs(int argCount, Value* args, const std::vector<std::string>& argNames,
                            const std::vector<std::vector<std::string>>& paramSpecs, const std::string& fnName) {
    int namedCount = static_cast<int>(argNames.size());
    int posCount = argCount - namedCount;
    if (posCount < 0) throw std::runtime_error("[Runtime Error]: Invalid argument count for " + fnName + ".");

    if (posCount > static_cast<int>(paramSpecs.size())) {
        throw std::runtime_error("[Runtime Error]: Too many arguments provided for " + fnName + ".");
    }

    ArgMap result;
    std::vector<bool> slotProvided(paramSpecs.size(), false);

    for (int i = 0; i < posCount; ++i) {
        slotProvided[i] = true;
        for (const auto& name : paramSpecs[i]) {
            result.map[name] = args[i];
        }
    }

    for (int k = 0; k < namedCount; ++k) {
        std::string name = argNames[k];
        int matchedSlot = -1;
        for (size_t s = 0; s < paramSpecs.size(); ++s) {
            for (const auto& alias : paramSpecs[s]) {
                if (alias == name) {
                    matchedSlot = static_cast<int>(s);
                    break;
                }
            }
            if (matchedSlot != -1) break;
        }

        if (matchedSlot == -1) {
            throw std::runtime_error("[Runtime Error]: Unexpected argument name '" + name + "' for " + fnName + ".");
        }

        if (slotProvided[matchedSlot]) {
            throw std::runtime_error("[Runtime Error]: Duplicate argument '" + name + "' provided for " + fnName + ".");
        }

        slotProvided[matchedSlot] = true;
        Value val = args[posCount + k];
        for (const auto& alias : paramSpecs[matchedSlot]) {
            result.map[alias] = val;
        }
    }

    return result;
}

static Value performSlice(const Value& target, const SlicePtr& slice) {
    if (slice->hasStart && !slice->start.isInt()) {
        throw std::runtime_error("[Runtime Error]: Slice start index must be an integer.");
    }
    if (slice->hasEnd && !slice->end.isInt()) {
        throw std::runtime_error("[Runtime Error]: Slice end index must be an integer.");
    }
    int64_t step = 1;
    if (slice->hasStep) {
        if (!slice->step.isInt()) {
            throw std::runtime_error("[Runtime Error]: Slice step must be an integer.");
        }
        step = slice->step.intVal;
        if (step == 0) {
            throw std::runtime_error("[Runtime Error]: Slice step cannot be zero.");
        }
    }

    if (target.isArray()) {
        ArrayPtr arr = target.array;
        int64_t len = static_cast<int64_t>(arr ? arr->size() : 0);
        int64_t start, end;

        if (step > 0) {
            start = slice->hasStart ? (slice->start.isInt() ? slice->start.intVal : 0) : 0;
            end = slice->hasEnd ? (slice->end.isInt() ? slice->end.intVal : len) : len;

            if (slice->hasStart && start < 0) start = len + start;
            if (slice->hasEnd && end < 0) end = len + end;

            start = std::max(int64_t(0), std::min(start, len));
            end = std::max(int64_t(0), std::min(end, len));

            ArrayPtr result = std::make_shared<ObjArray>();
            result->typeSpec = arr ? arr->typeSpec : TypeSpec{TypeKind::ARRAY};
            for (int64_t i = start; i < end; i += step) {
                result->push_back((*arr)[i]);
            }
            return Value(result);
        } else {
            start = slice->hasStart ? (slice->start.isInt() ? slice->start.intVal : len - 1) : len - 1;
            end = slice->hasEnd ? (slice->end.isInt() ? slice->end.intVal : -len - 1) : -len - 1;

            if (slice->hasStart && start < 0) start = len + start;
            if (slice->hasEnd && end < 0) end = len + end;

            start = std::max(int64_t(-1), std::min(start, len - 1));
            end = std::max(int64_t(-1), std::min(end, len - 1));

            ArrayPtr result = std::make_shared<ObjArray>();
            result->typeSpec = arr ? arr->typeSpec : TypeSpec{TypeKind::ARRAY};
            for (int64_t i = start; i > end; i += step) {
                result->push_back((*arr)[i]);
            }
            return Value(result);
        }
    } else if (target.isString()) {
        std::string str = target.str;
        std::vector<std::string> chars = utf8_to_chars(str);
        int64_t len = static_cast<int64_t>(chars.size());
        int64_t start, end;

        if (step > 0) {
            start = slice->hasStart ? (slice->start.isInt() ? slice->start.intVal : 0) : 0;
            end = slice->hasEnd ? (slice->end.isInt() ? slice->end.intVal : len) : len;

            if (slice->hasStart && start < 0) start = len + start;
            if (slice->hasEnd && end < 0) end = len + end;

            start = std::max(int64_t(0), std::min(start, len));
            end = std::max(int64_t(0), std::min(end, len));

            std::string result = "";
            for (int64_t i = start; i < end; i += step) {
                result += chars[i];
            }
            return Value(result);
        } else {
            start = slice->hasStart ? (slice->start.isInt() ? slice->start.intVal : len - 1) : len - 1;
            end = slice->hasEnd ? (slice->end.isInt() ? slice->end.intVal : -len - 1) : -len - 1;

            if (slice->hasStart && start < 0) start = len + start;
            if (slice->hasEnd && end < 0) end = len + end;

            start = std::max(int64_t(-1), std::min(start, len - 1));
            end = std::max(int64_t(-1), std::min(end, len - 1));

            std::string result = "";
            for (int64_t i = start; i > end; i += step) {
                result += chars[i];
            }
            return Value(result);
        }
    }
    throw std::runtime_error("[Runtime Error]: Only arrays and strings can be sliced.");
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
    openUpvalues = nullptr;
    moduleCache.clear();
    loadingStackPaths.clear();
    loadingStackNames.clear();
    rootModule = nullptr;
}

static bool checkAndCoerceValueType(const TypeSpec& expected, Value& val, ModulePtr currentModule = nullptr);

bool VM::call(ClosurePtr closure, int argCount, const std::vector<std::string>& argNames, bool isGrab) {
    FunctionPtr function = closure->function;
    std::string nameStr = (function && !function->name.empty()) ? "Task '" + function->name + "' " : "Task ";

    if (frames.size() >= 65536) {
        runtimeError("Stack overflow.");
        return false;
    }

    for (int i = 0; i < argCount; ++i) {
        size_t idx = stack.size() - argCount + i;
        bool isSelf = (i == 0 && function && !function->paramNames.empty() && function->paramNames[0] == "self");
        if (!isSelf) {
            stack[idx] = cloneValue(stack[idx]);
        }
    }

    int totalDeclared = function->arity;
    int namedCount = static_cast<int>(argNames.size());
    int posCount = argCount - namedCount;

    if (posCount < 0) {
        runtimeError(nameStr + "invalid argument count.");
        return false;
    }

    // Fast path for exact positional call
    if (argNames.empty() && posCount == totalDeclared) {
        for (size_t i = 0; i < function->paramTypes.size(); i++) {
            TypeSpec expected = function->paramTypes[i];
            if (expected.kind == TypeKind::ANY || expected.kind == TypeKind::UNTYPED) continue;

            if (!checkAndCoerceValueType(expected, stack[stack.size() - argCount + i], closure->module)) {
                runtimeError(nameStr + "argument " + std::to_string(i + 1) + " expects type " +
                             expected.toString() + " but got " + stack[stack.size() - argCount + i].getTypeSpec().toString() + ".");
                return false;
            }
        }

        CallFrame frame;
        frame.closure = closure;
        frame.ip = function->chunk.code.data();
        frame.slotsOffset = stack.size() - argCount - 1;
        frame.isGrab = isGrab;
        frames.push_back(frame);
        return true;
    }

    if (posCount > totalDeclared) {
        runtimeError(nameStr + "expected " + std::to_string(totalDeclared) + " arguments but got " + std::to_string(argCount) + ".");
        return false;
    }

    std::vector<Value> finalArgs(totalDeclared);
    std::vector<bool> provided(totalDeclared, false);

    size_t stackArgsStart = stack.size() - argCount;
    for (int i = 0; i < posCount; ++i) {
        finalArgs[i] = stack[stackArgsStart + i];
        provided[i] = true;
    }

    for (int k = 0; k < namedCount; ++k) {
        const std::string& name = argNames[k];
        Value val = stack[stackArgsStart + posCount + k];

        int paramIdx = -1;
        for (size_t p = 0; p < function->paramNames.size(); ++p) {
            if (function->paramNames[p] == name) {
                paramIdx = static_cast<int>(p);
                break;
            }
        }

        if (paramIdx == -1) {
            runtimeError(nameStr + "has no parameter named '" + name + "'.");
            return false;
        }

        if (provided[paramIdx]) {
            runtimeError(nameStr + "duplicate argument '" + name + "' provided.");
            return false;
        }

        finalArgs[paramIdx] = val;
        provided[paramIdx] = true;
    }

    for (int i = 0; i < totalDeclared; ++i) {
        if (!provided[i]) {
            std::string pName = (i < static_cast<int>(function->paramNames.size())) ? function->paramNames[i] : std::to_string(i + 1);
            runtimeError(nameStr + "missing required argument '" + pName + "'.");
            return false;
        }
    }

    for (int i = 0; i < totalDeclared; ++i) {
        if (i < static_cast<int>(function->paramTypes.size())) {
            TypeSpec expected = function->paramTypes[i];
            if (expected.kind != TypeKind::ANY && expected.kind != TypeKind::UNTYPED) {
                if (!checkAndCoerceValueType(expected, finalArgs[i], closure->module)) {
                    std::string pName = (i < static_cast<int>(function->paramNames.size())) ? function->paramNames[i] : std::to_string(i + 1);
                    runtimeError(nameStr + "argument '" + pName + "' expects type " +
                                 expected.toString() + " but got " + finalArgs[i].getTypeSpec().toString() + ".");
                    return false;
                }
            }
        }
    }

    stack.resize(stackArgsStart);
    for (int i = 0; i < totalDeclared; ++i) {
        stack.push_back(finalArgs[i]);
    }

    CallFrame frame;
    frame.closure = closure;
    frame.ip = function->chunk.code.data();
    frame.slotsOffset = stack.size() - totalDeclared - 1;
    frame.isGrab = isGrab;
    frames.push_back(frame);
    return true;
}

static bool hasOperatorOverload(const std::string& opSymbol, const Value& receiver, bool isUnary = false) {
    if (!receiver.isStruct() || !receiver.structInstance || !receiver.structInstance->def) return false;
    for (const auto& op : receiver.structInstance->def->operators) {
        if (op.opName == opSymbol && op.isUnary == isUnary) {
            return true;
        }
    }
    return false;
}

bool VM::callOperatorOverload(const std::string& opSymbol, const Value& receiver, const Value& rightArg, bool isUnary) {
    if (!receiver.isStruct() || !receiver.structInstance || !receiver.structInstance->def) return false;
    StructDefPtr sDef = receiver.structInstance->def;

    FunctionPtr matchedFn = nullptr;
    for (const auto& op : sDef->operators) {
        if (op.opName == opSymbol && op.isUnary == isUnary) {
            if (isUnary) {
                matchedFn = op.function;
                break;
            } else {
                Value tempRight = rightArg;
                if (checkAndCoerceValueType(op.rightType, tempRight, sDef->module)) {
                    matchedFn = op.function;
                    break;
                }
            }
        }
    }

    if (!matchedFn) return false;

    ClosurePtr closure = std::make_shared<ObjClosure>();
    closure->function = matchedFn;
    closure->module = sDef->module;

    std::vector<std::string> emptyNames;
    if (isUnary) {
        push(Value(closure));
        push(receiver);
        return call(closure, 1, emptyNames);
    } else {
        push(Value(closure));
        push(receiver);
        push(rightArg);
        return call(closure, 2, emptyNames);
    }
}

Value VM::runCallback(Value cb, const std::vector<Value>& availableArgs, int maxAllowedParams) {
    if (cb.isNative()) {
        std::vector<std::string> emptyNames;
        int count = std::min(static_cast<int>(availableArgs.size()), maxAllowedParams);
        std::vector<Value> args(availableArgs.begin(), availableArgs.begin() + count);
        return cb.nativeFn(count, args.data(), emptyNames);
    }
    if (!cb.isFunction() || !cb.closure || !cb.closure->function) {
        throw std::runtime_error("[Runtime Error]: Callback must be a task.");
    }

    FunctionPtr fn = cb.closure->function;
    int arity = fn->arity;
    if (arity > maxAllowedParams) {
        throw std::runtime_error("[Runtime Error]: Callback declares more parameters (" +
                                   std::to_string(arity) + ") than available (" + std::to_string(maxAllowedParams) + ").");
    }

    size_t initialFrameCount = frames.size();
    push(cb);
    for (int i = 0; i < arity; ++i) {
        if (i < static_cast<int>(availableArgs.size())) {
            push(availableArgs[i]);
        } else {
            push(Value()); // nil
        }
    }

    std::vector<std::string> emptyNames;
    if (!call(cb.closure, arity, emptyNames)) {
        throw std::runtime_error("[Runtime Error]: Callback invocation failed.");
    }

    while (frames.size() > initialFrameCount) {
        CallFrame& frame = frames.back();
        OpCode instruction = static_cast<OpCode>(*frame.ip++);
        if (!executeInstruction(instruction, frame)) {
            throw std::runtime_error("[Runtime Error]: Error during callback execution.");
        }
    }

    return pop();
}

UpvaluePtr VM::captureUpvalue(size_t stackIndex) {
    UpvaluePtr prevUpvalue = nullptr;
    UpvaluePtr upvalue = openUpvalues;
    while (upvalue != nullptr && upvalue->stackIndex > stackIndex) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != nullptr && upvalue->stackIndex == stackIndex) {
        return upvalue;
    }

    UpvaluePtr created = std::make_shared<ObjUpvalue>();
    created->stackIndex = stackIndex;
    created->next = upvalue;

    if (prevUpvalue == nullptr) {
        openUpvalues = created;
    } else {
        prevUpvalue->next = created;
    }

    return created;
}

void VM::closeUpvalues(size_t lastSlotIndex) {
    while (openUpvalues != nullptr && openUpvalues->stackIndex >= lastSlotIndex) {
        UpvaluePtr upvalue = openUpvalues;
        upvalue->closed = stack[upvalue->stackIndex];
        upvalue->isClosed = true;
        openUpvalues = upvalue->next;
    }
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
    if (str == "func") return TypeSpec{TypeKind::FUNC};
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
    TypeSpec spec;
    spec.kind = TypeKind::STRUCT;
    spec.structName = str;
    return spec;
}

static StructDefPtr resolveStructDef(const std::string& structName, ModulePtr mod) {
    if (structName.empty() || !mod) return nullptr;

    std::vector<std::string> parts;
    std::string part;
    std::stringstream ss(structName);
    while (std::getline(ss, part, '.')) {
        parts.push_back(part);
    }

    if (parts.empty()) return nullptr;

    ModulePtr currentMod = mod;
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        auto it = currentMod->globals.find(parts[i]);
        if (it == currentMod->globals.end() || !it->second.isModule() || !it->second.module) {
            return nullptr;
        }
        currentMod = it->second.module;
    }

    auto it = currentMod->globals.find(parts.back());
    if (it != currentMod->globals.end() && it->second.isStructDef() && it->second.structDef) {
        return it->second.structDef;
    }

    return nullptr;
}

static bool checkAndCoerceValueType(const TypeSpec& expected, Value& val, ModulePtr currentModule) {
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
    if (expected.kind == TypeKind::FUNC) {
        return val.isFunction() || val.isNative();
    }
    if (expected.kind == TypeKind::STRUCT) {
        if (!val.isStruct()) return false;
        if (!val.structInstance || !val.structInstance->def) return false;
        StructDefPtr valDef = val.structInstance->def;

        if (expected.structName.empty()) {
            return true;
        }

        if (currentModule) {
            StructDefPtr expectedDef = resolveStructDef(expected.structName, currentModule);
            if (expectedDef) {
                return valDef == expectedDef;
            }
        }

        if (valDef->name != expected.structName) return false;
        return true;
    }
    if (expected.kind == TypeKind::ARRAY) {
        if (!val.isArray()) return false;
        if (!val.array) return true;

        if (val.array->typeSpec != expected) {
            ArrayPtr targetArr = std::make_shared<ObjArray>();
            targetArr->typeSpec = expected;
            TypeSpec elemSpec = expected.elemType ? *expected.elemType : TypeSpec{expected.elementKind};

            for (size_t i = 0; i < val.array->elements.size(); ++i) {
                Value elemCopy = val.array->elements[i];
                if (expected.elementKind != TypeKind::ANY && expected.elementKind != TypeKind::UNTYPED) {
                    if (!checkAndCoerceValueType(elemSpec, elemCopy, currentModule)) {
                        return false;
                    }
                }
                targetArr->push_back(elemCopy);
            }
            val = Value(targetArr);
        } else {
            if (expected.elementKind != TypeKind::ANY && expected.elementKind != TypeKind::UNTYPED) {
                TypeSpec elemSpec = expected.elemType ? *expected.elemType : TypeSpec{expected.elementKind};
                for (size_t i = 0; i < val.array->elements.size(); ++i) {
                    if (!checkAndCoerceValueType(elemSpec, val.array->elements[i], currentModule)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    if (expected.kind == TypeKind::MAP) {
        if (!val.isMap()) return false;
        if (!val.map) return true;

        if (val.map->typeSpec != expected) {
            MapPtr targetMap = std::make_shared<ObjMap>();
            targetMap->typeSpec = expected;
            TypeSpec keySpec = expected.keyType ? *expected.keyType : TypeSpec{expected.keyKind};
            TypeSpec valSpec = expected.valType ? *expected.valType : TypeSpec{expected.valueKind};

            for (size_t i = 0; i < val.map->keys.size(); ++i) {
                Value kCopy = val.map->keys[i];
                Value vCopy = val.map->get(kCopy);

                if (keySpec.kind != TypeKind::ANY && keySpec.kind != TypeKind::UNTYPED) {
                    if (!checkAndCoerceValueType(keySpec, kCopy, currentModule)) return false;
                }
                if (valSpec.kind != TypeKind::ANY && valSpec.kind != TypeKind::UNTYPED) {
                    if (!checkAndCoerceValueType(valSpec, vCopy, currentModule)) return false;
                }
                targetMap->set(kCopy, vCopy);
            }
            val = Value(targetMap);
        } else {
            TypeSpec keySpec = expected.keyType ? *expected.keyType : TypeSpec{expected.keyKind};
            TypeSpec valSpec = expected.valType ? *expected.valType : TypeSpec{expected.valueKind};
            for (auto& pair : val.map->table) {
                Value kVal = pair.first.val;
                if (keySpec.kind != TypeKind::ANY && keySpec.kind != TypeKind::UNTYPED) {
                    if (!checkAndCoerceValueType(keySpec, kVal, currentModule)) return false;
                }
                if (valSpec.kind != TypeKind::ANY && valSpec.kind != TypeKind::UNTYPED) {
                    if (!checkAndCoerceValueType(valSpec, pair.second, currentModule)) return false;
                }
            }
        }
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

ModulePtr VM::loadModule(const std::string& modulePathStr, const std::string& requesterPath, ClosurePtr& outClosure, bool& isNew) {
    outClosure = nullptr;
    isNew = false;

    std::string relPathStr = modulePathStr;
    std::replace(relPathStr.begin(), relPathStr.end(), '.', '/');
    relPathStr += ".kek";

    fs::path targetPath;
    if (!requesterPath.empty()) {
        fs::path reqP(requesterPath);
        targetPath = reqP.parent_path() / fs::path(relPathStr);
    } else {
        targetPath = fs::path(relPathStr);
    }

    std::string canonicalPath;
    try {
        if (fs::exists(targetPath)) {
            canonicalPath = fs::canonical(targetPath).string();
        } else {
            canonicalPath = fs::absolute(targetPath).string();
        }
    } catch (...) {
        canonicalPath = fs::absolute(targetPath).string();
    }

    auto cacheIt = moduleCache.find(canonicalPath);
    if (cacheIt != moduleCache.end()) {
        auto pathIt = std::find(loadingStackPaths.begin(), loadingStackPaths.end(), canonicalPath);
        if (pathIt != loadingStackPaths.end()) {
            std::string cycleChain = "";
            for (auto it = pathIt; it != loadingStackPaths.end(); ++it) {
                size_t idx = std::distance(loadingStackPaths.begin(), it);
                if (!cycleChain.empty()) cycleChain += " -> ";
                cycleChain += loadingStackNames[idx];
            }
            if (!cycleChain.empty()) cycleChain += " -> ";
            cycleChain += modulePathStr;

            std::cout << "[Module Error]: Circular module dependency detected: " << cycleChain << std::endl;
            return nullptr;
        }
        isNew = false;
        return cacheIt->second;
    }

    std::ifstream file(canonicalPath);
    if (!file.is_open()) {
        std::string reqName = requesterPath.empty() ? "main program" : "'" + fs::path(requesterPath).filename().string() + "'";
        std::cout << "[Module Error]: Could not find module '" << modulePathStr << "' required by " << reqName << std::endl;
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string moduleSource = buffer.str();

    ModulePtr module = std::make_shared<ObjModule>();
    module->name = modulePathStr;
    module->path = canonicalPath;

    for (const auto& pair : builtins) {
        module->globals[pair.first] = pair.second;
        module->symbols[pair.first] = SymbolInfo{true, false, TypeSpec{TypeKind::ANY}};
    }

    moduleCache[canonicalPath] = module;

    loadingStackPaths.push_back(canonicalPath);
    loadingStackNames.push_back(modulePathStr);

    FunctionPtr modFn = std::make_shared<ObjFunction>();
    modFn->name = modulePathStr;
    modFn->arity = 0;
    modFn->module = module;

    Compiler compiler(moduleSource, modFn->chunk);
    if (!compiler.compile()) {
        loadingStackPaths.pop_back();
        loadingStackNames.pop_back();
        moduleCache.erase(canonicalPath);
        std::cout << "[Module Error]: Could not compile module '" << modulePathStr << "'." << std::endl;
        return nullptr;
    }

    ClosurePtr modClosure = std::make_shared<ObjClosure>();
    modClosure->function = modFn;
    modClosure->module = module;

    outClosure = modClosure;
    isNew = true;
    return module;
}

VM::VM() {
    builtins["size"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "coll"}}, "size()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: size() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isArray()) {
            return Value(static_cast<int64_t>(val.array ? val.array->size() : 0));
        } else if (val.isString()) {
            return Value(static_cast<int64_t>(utf8_length(val.str)));
        } else if (val.isMap()) {
            return Value(static_cast<int64_t>(val.map ? val.map->keys.size() : 0));
        }
        throw std::runtime_error("[Runtime Error]: size() expects array, map, or string argument.");
    }));

    builtins["keys"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"map"}}, "keys()");
        if (!aMap.has("map")) throw std::runtime_error("[Runtime Error]: keys() expects map as argument.");
        Value mapVal = aMap.get("map");
        if (!mapVal.isMap() || !mapVal.map) {
            throw std::runtime_error("[Runtime Error]: keys() expects map as argument.");
        }
        ArrayPtr arr = std::make_shared<ObjArray>();
        for (const Value& key : mapVal.map->keys) {
            arr->push_back(key);
        }
        return Value(arr);
    }));

    builtins["values"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"map"}}, "values()");
        if (!aMap.has("map")) throw std::runtime_error("[Runtime Error]: values() expects map as argument.");
        Value mapVal = aMap.get("map");
        if (!mapVal.isMap() || !mapVal.map) {
            throw std::runtime_error("[Runtime Error]: values() expects map as argument.");
        }
        ArrayPtr arr = std::make_shared<ObjArray>();
        for (const Value& key : mapVal.map->keys) {
            arr->push_back(mapVal.map->get(key));
        }
        return Value(arr);
    }));

    builtins["has"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"coll", "map", "array"}, {"key", "val"}}, "has()");
        if (!aMap.has("coll") || !aMap.has("key")) {
            throw std::runtime_error("[Runtime Error]: has() expects 2 arguments.");
        }
        Value coll = aMap.get("coll");
        Value key = aMap.get("key");
        if (coll.isMap()) {
            if (!ObjMap::isSupportedKey(key)) {
                throw std::runtime_error("[Runtime Error]: unsupported map key in has().");
            }
            if (!coll.map) return Value(false);
            return Value(coll.map->contains(key));
        } else if (coll.isArray()) {
            if (!coll.array) return Value(false);
            for (const Value& elem : *coll.array) {
                if (elem.isEqual(key)) return Value(true);
            }
            return Value(false);
        }
        throw std::runtime_error("[Runtime Error]: has() expects map or array as first argument.");
    }));

    builtins["read"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"prompt"}}, "read()");
        if (!aMap.has("prompt")) throw std::runtime_error("[Runtime Error]: read() expects 1 argument.");
        Value promptVal = aMap.get("prompt");
        std::cout << promptVal.toString();
        std::cout.flush();
        std::string input;
        std::getline(std::cin, input);
        return Value(input);
    }));

    auto typeofFn = NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "x", "target"}}, "typeof()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: typeof() expects 1 argument.");
        Value val = aMap.get("val");
        switch (val.type) {
            case ValueType::INT: return Value(std::string("int"));
            case ValueType::FLOAT: return Value(std::string("float"));
            case ValueType::CHAR: return Value(std::string("char"));
            case ValueType::STRING: return Value(std::string("string"));
            case ValueType::BOOL: return Value(std::string("bool"));
            case ValueType::ARRAY: return Value(std::string("array"));
            case ValueType::MAP: return Value(std::string("map"));
            case ValueType::FUNCTION:
            case ValueType::NATIVE: return Value(std::string("func"));
            case ValueType::MODULE: return Value(std::string("module"));
            case ValueType::NIL: return Value(std::string("nil"));
            case ValueType::SLICE: return Value(std::string("slice"));
            case ValueType::STRUCT: return Value(val.structInstance && val.structInstance->def ? val.structInstance->def->name : std::string("struct"));
            case ValueType::BOUND_METHOD: return Value(std::string("func"));
            case ValueType::STRUCT_DEF: return Value(std::string("struct_def"));
        }
        return Value(std::string("nil"));
    });

    builtins["typeof"] = Value(typeofFn);
    builtins["scan"] = Value(typeofFn);

    // Explicit Type Casts
    builtins["cast_int"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val"}}, "cast_int()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: cast_int() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isInt()) return val;
        if (val.isFloat()) {
            double f = val.floatVal;
            if (std::isnan(f) || std::isinf(f)) {
                throw std::runtime_error("[Runtime Error]: Cannot cast NaN or Infinity to int.");
            }
            double rounded = (f >= 0) ? std::floor(f + 0.5) : std::ceil(f - 0.5);
            if (rounded < -9223372036854775808.0 || rounded >= 9223372036854775808.0) {
                throw std::runtime_error("[Runtime Error]: Float value out of 64-bit integer range in cast_int().");
            }
            return Value(static_cast<int64_t>(rounded));
        }
        if (val.isBool()) return Value(static_cast<int64_t>(val.boolean ? 1 : 0));
        if (val.isString()) {
            try {
                size_t pos = 0;
                int64_t v = std::stoll(val.str, &pos, 10);
                if (pos == val.str.length()) return Value(v);
            } catch (...) {}
            throw std::runtime_error("[Runtime Error]: Cannot cast string '" + val.str + "' to int.");
        }
        throw std::runtime_error("[Runtime Error]: Invalid conversion to int.");
    }));

    builtins["cast_float"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val"}}, "cast_float()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: cast_float() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isFloat()) return val;
        if (val.isInt()) return Value(static_cast<double>(val.intVal));
        if (val.isBool()) return Value(val.boolean ? 1.0 : 0.0);
        if (val.isString()) {
            try {
                size_t pos = 0;
                double v = std::stod(val.str, &pos);
                if (pos == val.str.length()) return Value(v);
            } catch (...) {}
            throw std::runtime_error("[Runtime Error]: Cannot cast string '" + val.str + "' to float.");
        }
        throw std::runtime_error("[Runtime Error]: Invalid conversion to float.");
    }));

    builtins["cast_string"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val"}}, "cast_string()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: cast_string() expects 1 argument.");
        Value val = aMap.get("val");
        return Value(val.toString());
    }));

    builtins["cast_char"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val"}}, "cast_char()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: cast_char() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isChar()) return val;
        if (val.isString()) {
            std::string s = val.str;
            std::vector<std::string> chars = utf8_to_chars(s);
            if (chars.size() != 1) {
                throw std::runtime_error("[Runtime Error]: cast_char() requires a single-character string.");
            }
            return Value(utf8_code_point(chars[0]), true);
        }
        throw std::runtime_error("[Runtime Error]: cast_char() requires an actual single-character value.");
    }));

    builtins["cast_array"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val"}}, "cast_array()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: cast_array() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isArray()) return val;
        if (val.isString()) {
            ArrayPtr arr = std::make_shared<ObjArray>();
            for (const auto& cStr : utf8_to_chars(val.str)) {
                arr->push_back(Value(utf8_code_point(cStr), true));
            }
            return Value(arr);
        }
        throw std::runtime_error("[Runtime Error]: Cannot cast value to array.");
    }));

    builtins["cast_map"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val"}}, "cast_map()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: cast_map() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isMap()) return val;
        throw std::runtime_error("[Runtime Error]: Cannot cast value to map.");
    }));

    // Math Functions
    builtins["clock"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        (void)args;
        parseCallArgs(argCount, args, argNames, {}, "clock()");
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
        double seconds = std::chrono::duration<double>(now).count();
        return Value(seconds);
    }));

    builtins["rand"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        (void)args;
        parseCallArgs(argCount, args, argNames, {}, "rand()");
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value(dist(rng));
    }));

    builtins["abs"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "x"}}, "abs()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: abs() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isInt()) {
            if (val.intVal == std::numeric_limits<int64_t>::min()) {
                throw std::runtime_error("[Runtime Error]: 64-bit integer overflow in abs().");
            }
            return Value(std::abs(val.intVal));
        }
        if (val.isFloat()) return Value(std::abs(val.floatVal));
        throw std::runtime_error("[Runtime Error]: abs() expects a number.");
    }));

    builtins["floor"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "x"}}, "floor()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: floor() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isInt()) return val;
        if (val.isFloat()) return Value(std::floor(val.floatVal));
        throw std::runtime_error("[Runtime Error]: floor() expects a number.");
    }));

    builtins["ceil"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "x"}}, "ceil()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: ceil() expects 1 argument.");
        Value val = aMap.get("val");
        if (val.isInt()) return val;
        if (val.isFloat()) return Value(std::ceil(val.floatVal));
        throw std::runtime_error("[Runtime Error]: ceil() expects a number.");
    }));

    builtins["sqrt"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "x"}}, "sqrt()");
        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: sqrt() expects 1 argument.");
        Value val = aMap.get("val");
        if (!val.isNumber()) throw std::runtime_error("[Runtime Error]: sqrt() expects a number.");
        double f = val.asFloat();
        if (f < 0.0) throw std::runtime_error("[Runtime Error]: Cannot calculate square root of negative number.");
        return Value(std::sqrt(f));
    }));

    builtins["clamp"] = Value(NativeFn([](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "x"}, {"min"}, {"max"}}, "clamp()");
        if (!aMap.has("val") || !aMap.has("min") || !aMap.has("max")) {
            throw std::runtime_error("[Runtime Error]: clamp() expects 3 arguments.");
        }
        Value vVal = aMap.get("val");
        Value minVal = aMap.get("min");
        Value maxVal = aMap.get("max");
        if (!vVal.isNumber() || !minVal.isNumber() || !maxVal.isNumber()) {
            throw std::runtime_error("[Runtime Error]: clamp() expects numbers.");
        }
        if (minVal.asFloat() > maxVal.asFloat()) {
            throw std::runtime_error("[Runtime Error]: clamp() min value cannot be greater than max value.");
        }
        if (vVal.isInt() && minVal.isInt() && maxVal.isInt()) {
            int64_t v = vVal.intVal;
            int64_t mn = minVal.intVal;
            int64_t mx = maxVal.intVal;
            return Value(std::max(mn, std::min(v, mx)));
        }
        double v = vVal.asFloat();
        double mn = minVal.asFloat();
        double mx = maxVal.asFloat();
        return Value(std::max(mn, std::min(v, mx)));
    }));
}

bool VM::executeInstruction(OpCode instruction, CallFrame& frame) {
    switch (instruction) {
        case OpCode::OP_STRUCT_DEF: {
            uint16_t index = read16(frame.ip);
            Value val = frame.closure->function->chunk.constants[index];
            if (val.isStructDef() && val.structDef) {
                val.structDef->module = frame.closure->module;
                for (auto& pair : val.structDef->methods) {
                    if (pair.second.function) {
                        pair.second.function->module = frame.closure->module;
                    }
                }
                for (auto& op : val.structDef->operators) {
                    if (op.function) {
                        op.function->module = frame.closure->module;
                    }
                }
            }
            push(val);
            break;
        }
        case OpCode::OP_CONSTANT: {
            uint16_t index = read16(frame.ip);
            push(frame.closure->function->chunk.constants[index]);
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
            uint16_t index = read16(frame.ip);
            uint8_t flags = *frame.ip++;
            bool isPublic = (flags & 1) != 0;
            bool isConst = (flags & 2) != 0;
            std::string name = frame.closure->function->chunk.constants[index].str;
            Value val = cloneValue(pop());
            if (isConst) val = makeConst(val);
            if (val.isFunction()) {
                if (val.closure) val.closure->module = frame.closure->module;
                if (val.function) val.function->module = frame.closure->module;
            }
            if (val.isStructDef() && val.structDef) {
                val.structDef->module = frame.closure->module;
            }
            frame.closure->module->globals[name] = val;
            frame.closure->module->symbols[name] = SymbolInfo{isPublic, isConst, TypeSpec{TypeKind::ANY}};
            break;
        }
        case OpCode::OP_DEFINE_GLOBAL_TYPED: {
            uint16_t index = read16(frame.ip);
            uint16_t typeSpecIdx = read16(frame.ip);
            uint8_t flags = *frame.ip++;
            bool isPublic = (flags & 1) != 0;
            bool isConst = (flags & 2) != 0;
            TypeSpec expected = parseTypeSpecString(frame.closure->function->chunk.constants[typeSpecIdx].str);
            std::string name = frame.closure->function->chunk.constants[index].str;
            Value val = cloneValue(pop());
            if (isConst) val = makeConst(val);
            if (!checkAndCoerceValueType(expected, val, frame.closure->module)) {
                std::cout << "[Runtime Error]: Type mismatch for global variable '" << name
                          << "': expected " << expected.toString() << " but got "
                          << val.getTypeSpec().toString() << "." << std::endl;
                return false;
            }
            if (val.isFunction()) {
                if (val.closure) val.closure->module = frame.closure->module;
                if (val.function) val.function->module = frame.closure->module;
            }
            if (val.isStructDef() && val.structDef) {
                val.structDef->module = frame.closure->module;
            }
            frame.closure->module->globals[name] = val;
            frame.closure->module->symbols[name] = SymbolInfo{isPublic, isConst, expected};
            break;
        }
        case OpCode::OP_CHECK_LOCAL_TYPE: {
            uint16_t typeSpecIdx = read16(frame.ip);
            TypeSpec expected = parseTypeSpecString(frame.closure->function->chunk.constants[typeSpecIdx].str);
            Value val = peek(0);
            if (!checkAndCoerceValueType(expected, val, frame.closure->module)) {
                std::cout << "[Runtime Error]: Type mismatch: expected "
                          << expected.toString() << " but got "
                          << val.getTypeSpec().toString() << "." << std::endl;
                return false;
            }
            stack.back() = val;
            break;
        }
        case OpCode::OP_GET_GLOBAL: {
            uint16_t index = read16(frame.ip);
            std::string name = frame.closure->function->chunk.constants[index].str;
            ModulePtr mod = frame.closure->module;
            auto it = mod->globals.find(name);
            if (it == mod->globals.end()) {
                std::cout << "[Runtime Error]: Undefined variable '" << name << "'" << std::endl;
                return false;
            }
            push(it->second);
            break;
        }
        case OpCode::OP_SET_GLOBAL: {
            uint16_t index = read16(frame.ip);
            std::string name = frame.closure->function->chunk.constants[index].str;
            ModulePtr mod = frame.closure->module;
            auto it = mod->globals.find(name);
            if (it == mod->globals.end()) {
                std::cout << "[Runtime Error]: Variable '" << name << "' is not defined." << std::endl;
                return false;
            }
            auto symIt = mod->symbols.find(name);
            if (symIt != mod->symbols.end() && symIt->second.isConst) {
                if (it->second.isModule()) {
                    std::cout << "[Runtime Error]: Cannot reassign module alias '" << name << "'." << std::endl;
                } else {
                    std::cout << "[Runtime Error]: Cannot reassign constant variable '" << name << "'." << std::endl;
                }
                return false;
            }
            if (symIt != mod->symbols.end() && symIt->second.typeSpec.kind != TypeKind::ANY && symIt->second.typeSpec.kind != TypeKind::UNTYPED) {
                TypeSpec expected = symIt->second.typeSpec;
                Value val = peek(0);
                if (!checkAndCoerceValueType(expected, val, frame.closure->module)) {
                    std::cout << "[Runtime Error]: Type mismatch for variable '" << name
                              << "': expected " << expected.toString() << " but got "
                              << val.getTypeSpec().toString() << "." << std::endl;
                    return false;
                }
                it->second = val;
            } else {
                it->second = peek(0);
            }
            break;
        }
        case OpCode::OP_GET_LOCAL: {
            uint16_t slot = read16(frame.ip);
            push(stack[frame.slotsOffset + slot]);
            break;
        }
        case OpCode::OP_SET_LOCAL: {
            uint16_t slot = read16(frame.ip);
            Value val = cloneValue(peek(0));
            if (slot < frame.closure->function->localTypes.size()) {
                TypeSpec expected = frame.closure->function->localTypes[slot];
                if (expected.kind != TypeKind::ANY && expected.kind != TypeKind::UNTYPED) {
                    if (!checkAndCoerceValueType(expected, val, frame.closure->module)) {
                        std::cout << "[Runtime Error]: Type mismatch for local variable: expected "
                                  << expected.toString() << " but got "
                                  << val.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                    stack[frame.slotsOffset + slot] = val;
                    break;
                }
            }
            stack[frame.slotsOffset + slot] = val;
            break;
        }
        case OpCode::OP_GET_UPVALUE: {
            uint16_t slot = read16(frame.ip);
            push(*frame.closure->upvalues[slot]->getValuePtr(stack));
            break;
        }
        case OpCode::OP_SET_UPVALUE: {
            uint16_t slot = read16(frame.ip);
            UpvaluePtr upvalue = frame.closure->upvalues[slot];
            Value val = cloneValue(peek(0));
            if (upvalue->typeSpec.kind != TypeKind::ANY && upvalue->typeSpec.kind != TypeKind::UNTYPED) {
                if (!checkAndCoerceValueType(upvalue->typeSpec, val, frame.closure->module)) {
                    std::cout << "[Runtime Error]: Type mismatch for variable: expected "
                              << upvalue->typeSpec.toString() << " but got "
                              << val.getTypeSpec().toString() << "." << std::endl;
                    return false;
                }
            }
            *upvalue->getValuePtr(stack) = val;
            break;
        }
        case OpCode::OP_CLOSURE: {
            uint16_t constantIdx = read16(frame.ip);
            FunctionPtr fn = frame.closure->function->chunk.constants[constantIdx].function;
            ClosurePtr closure = std::make_shared<ObjClosure>();
            closure->function = fn;
            closure->module = frame.closure->module;
            fn->module = frame.closure->module;
            for (int i = 0; i < fn->upvalueCount; i++) {
                uint8_t isLocal = *frame.ip++;
                uint16_t index = read16(frame.ip);
                if (isLocal) {
                    closure->upvalues.push_back(captureUpvalue(frame.slotsOffset + index));
                } else {
                    closure->upvalues.push_back(frame.closure->upvalues[index]);
                }
            }
            push(Value(closure));
            break;
        }
        case OpCode::OP_CLOSE_UPVALUE: {
            if (!stack.empty()) {
                closeUpvalues(stack.size() - 1);
            }
            pop();
            break;
        }
        case OpCode::OP_INC: {
            Value val = pop();
            if (val.isInt()) {
                int64_t res;
                if (__builtin_add_overflow(val.intVal, 1, &res)) {
                    std::cout << "[Runtime Error]: 64-bit integer addition overflow." << std::endl;
                    return false;
                }
                push(Value(res));
            } else if (val.isFloat()) {
                push(Value(val.floatVal + 1.0));
            } else {
                std::cout << "[Runtime Error]: '++' operand must be a number!" << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_DEC: {
            Value val = pop();
            if (val.isInt()) {
                int64_t res;
                if (__builtin_sub_overflow(val.intVal, 1, &res)) {
                    std::cout << "[Runtime Error]: 64-bit integer subtraction overflow." << std::endl;
                    return false;
                }
                push(Value(res));
            } else if (val.isFloat()) {
                push(Value(val.floatVal - 1.0));
            } else {
                std::cout << "[Runtime Error]: '--' operand must be a number!" << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_CALL: {
            uint16_t argCount = read16(frame.ip);
            Value callee = peek(argCount);
            std::vector<std::string> emptyNames;
            if (callee.isStructDef()) {
                StructDefPtr sDef = callee.structDef;
                auto instance = std::make_shared<ObjStructInstance>();
                instance->def = sDef;

                int totalFields = static_cast<int>(sDef->fields.size());
                int posCount = argCount;

                if (posCount > totalFields) {
                    std::cout << "[Runtime Error]: Too many positional arguments for constructor of '" << sDef->name << "'." << std::endl;
                    return false;
                }

                std::vector<Value> fieldValues(totalFields, Value());
                size_t stackArgsStart = stack.size() - argCount;
                for (int i = 0; i < posCount; ++i) {
                    fieldValues[i] = cloneValue(stack[stackArgsStart + i]);
                }

                for (int i = 0; i < totalFields; ++i) {
                    const auto& fInfo = sDef->fields[i];
                    Value& fieldVal = fieldValues[i];
                    if (i < posCount && !fieldVal.isNil()) {
                        if (!checkAndCoerceValueType(fInfo.typeSpec, fieldVal, sDef->module ? sDef->module : frame.closure->module)) {
                            std::cout << "[Runtime Error]: Type mismatch for field '" << fInfo.name << "' in constructor of '" << sDef->name << "': expected " << fInfo.typeSpec.toString() << " but got " << fieldVal.getTypeSpec().toString() << "." << std::endl;
                            return false;
                        }
                    }
                    instance->fields[fInfo.name] = fieldVal;
                }

                stack.resize(stackArgsStart - 1);
                push(Value(instance));
                break;
            } else if (callee.isBoundMethod()) {
                BoundMethodPtr bm = callee.boundMethod;
                ClosurePtr methodClosure = std::make_shared<ObjClosure>();
                methodClosure->function = bm->method;
                methodClosure->module = bm->method->module ? bm->method->module : frame.closure->module;

                size_t calleePos = stack.size() - argCount - 1;
                stack[calleePos] = Value(methodClosure);
                stack.insert(stack.begin() + calleePos + 1, bm->receiver);
                if (!call(methodClosure, argCount + 1, emptyNames)) return false;
                break;
            } else if (callee.isFunction()) {
                if (!call(callee.closure, argCount, emptyNames)) {
                    return false;
                }
            } else if (callee.isNative()) {
                try {
                    Value* args = &stack[stack.size() - argCount];
                    Value result = callee.nativeFn(argCount, args, emptyNames);
                    stack.resize(stack.size() - argCount - 1);
                    push(result);
                } catch (const std::exception& ex) {
                    std::cout << ex.what() << std::endl;
                    return false;
                }
            } else {
                std::cout << "[Runtime Error]: Can only call task values (got type " << callee.getTypeSpec().toString() << ")." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_CALL_NAMED: {
            uint16_t argCount = read16(frame.ip);
            uint16_t namedCount = read16(frame.ip);
            std::vector<std::string> argNames(namedCount);
            for (uint16_t i = 0; i < namedCount; ++i) {
                uint16_t nameIdx = read16(frame.ip);
                argNames[i] = frame.closure->function->chunk.constants[nameIdx].str;
            }
            Value callee = peek(argCount);
            if (callee.isStructDef()) {
                StructDefPtr sDef = callee.structDef;
                auto instance = std::make_shared<ObjStructInstance>();
                instance->def = sDef;

                int totalFields = static_cast<int>(sDef->fields.size());
                int posCount = argCount - namedCount;

                if (posCount > totalFields) {
                    std::cout << "[Runtime Error]: Too many positional arguments for constructor of '" << sDef->name << "'." << std::endl;
                    return false;
                }

                std::vector<bool> fieldSet(totalFields, false);
                std::vector<Value> fieldValues(totalFields, Value());

                size_t stackArgsStart = stack.size() - argCount;
                for (int i = 0; i < posCount; ++i) {
                    fieldValues[i] = cloneValue(stack[stackArgsStart + i]);
                    fieldSet[i] = true;
                }

                for (int k = 0; k < namedCount; ++k) {
                    const std::string& fieldName = argNames[k];
                    int fIdx = sDef->findField(fieldName);
                    if (fIdx == -1) {
                        std::cout << "[Runtime Error]: Unknown named argument '" << fieldName << "' in constructor of '" << sDef->name << "'." << std::endl;
                        return false;
                    }
                    if (fieldSet[fIdx]) {
                        std::cout << "[Runtime Error]: Duplicate argument for field '" << fieldName << "' in constructor of '" << sDef->name << "'." << std::endl;
                        return false;
                    }
                    fieldValues[fIdx] = cloneValue(stack[stackArgsStart + posCount + k]);
                    fieldSet[fIdx] = true;
                }

                for (int i = 0; i < totalFields; ++i) {
                    const auto& fInfo = sDef->fields[i];
                    Value& fieldVal = fieldValues[i];
                    if (fieldSet[i] && !fieldVal.isNil()) {
                        if (!checkAndCoerceValueType(fInfo.typeSpec, fieldVal, sDef->module ? sDef->module : frame.closure->module)) {
                            std::cout << "[Runtime Error]: Type mismatch for field '" << fInfo.name << "' in constructor of '" << sDef->name << "': expected " << fInfo.typeSpec.toString() << " but got " << fieldVal.getTypeSpec().toString() << "." << std::endl;
                            return false;
                        }
                    }
                    instance->fields[fInfo.name] = fieldVal;
                }

                stack.resize(stackArgsStart - 1);
                push(Value(instance));
                break;
            } else if (callee.isBoundMethod()) {
                BoundMethodPtr bm = callee.boundMethod;
                ClosurePtr methodClosure = std::make_shared<ObjClosure>();
                methodClosure->function = bm->method;
                methodClosure->module = bm->method->module ? bm->method->module : frame.closure->module;

                size_t calleePos = stack.size() - argCount - 1;
                stack[calleePos] = Value(methodClosure);
                stack.insert(stack.begin() + calleePos + 1, bm->receiver);
                if (!call(methodClosure, argCount + 1, argNames)) return false;
                break;
            } else if (callee.isFunction()) {
                if (!call(callee.closure, argCount, argNames)) return false;

            } else if (callee.isNative()) {
                try {
                    Value* args = &stack[stack.size() - argCount];
                    Value result = callee.nativeFn(argCount, args, argNames);
                    stack.resize(stack.size() - argCount - 1);
                    push(result);
                } catch (const std::exception& ex) {
                    std::cout << ex.what() << std::endl;
                    return false;
                }
            } else {
                std::cout << "[Runtime Error]: Can only call task values (got type " << callee.getTypeSpec().toString() << ")." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_BEGIN_CALL: {
            SlicePtr marker = std::make_shared<ObjSlice>();
            marker->isCallMarker = true;
            push(Value(marker));
            break;
        }
        case OpCode::OP_SPREAD_ARG: {
            Value arrVal = pop();
            if (!arrVal.isArray() || !arrVal.array) {
                std::cout << "[Runtime Error]: Spread operator requires an array." << std::endl;
                return false;
            }
            for (size_t i = 0; i < arrVal.array->size(); ++i) {
                push((*arrVal.array)[i]);
            }
            break;
        }
        case OpCode::OP_CALL_VAR: {
            int markerIdx = -1;
            for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i) {
                if (stack[i].isSlice() && stack[i].slice && stack[i].slice->isCallMarker) {
                    markerIdx = i;
                    break;
                }
            }
            if (markerIdx == -1) {
                std::cout << "[Runtime Error]: Invalid variable call stack." << std::endl;
                return false;
            }
            int totalArgCount = static_cast<int>(stack.size() - 1 - markerIdx);
            stack.erase(stack.begin() + markerIdx);

            Value callee = peek(totalArgCount);
            std::vector<std::string> emptyNames;
            if (callee.isFunction()) {
                if (!call(callee.closure, totalArgCount, emptyNames)) return false;

            } else if (callee.isNative()) {
                try {
                    Value* args = &stack[stack.size() - totalArgCount];
                    Value result = callee.nativeFn(totalArgCount, args, emptyNames);
                    stack.resize(stack.size() - totalArgCount - 1);
                    push(result);
                } catch (const std::exception& ex) {
                    std::cout << ex.what() << std::endl;
                    return false;
                }
            } else {
                std::cout << "[Runtime Error]: Can only call task values (got type " << callee.getTypeSpec().toString() << ")." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_CALL_VAR_NAMED: {
            uint16_t namedCount = read16(frame.ip);
            std::vector<std::string> argNames(namedCount);
            for (uint16_t i = 0; i < namedCount; ++i) {
                uint16_t nameIdx = read16(frame.ip);
                argNames[i] = frame.closure->function->chunk.constants[nameIdx].str;
            }

            int markerIdx = -1;
            for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i) {
                if (stack[i].isSlice() && stack[i].slice && stack[i].slice->isCallMarker) {
                    markerIdx = i;
                    break;
                }
            }
            if (markerIdx == -1) {
                std::cout << "[Runtime Error]: Invalid variable call stack." << std::endl;
                return false;
            }
            int totalArgCount = static_cast<int>(stack.size() - 1 - markerIdx);
            stack.erase(stack.begin() + markerIdx);

            Value callee = peek(totalArgCount);
            if (callee.isFunction()) {
                if (!call(callee.closure, totalArgCount, argNames)) return false;

            } else if (callee.isNative()) {
                try {
                    Value* args = &stack[stack.size() - totalArgCount];
                    Value result = callee.nativeFn(totalArgCount, args, argNames);
                    stack.resize(stack.size() - totalArgCount - 1);
                    push(result);
                } catch (const std::exception& ex) {
                    std::cout << ex.what() << std::endl;
                    return false;
                }
            } else {
                std::cout << "[Runtime Error]: Can only call task values (got type " << callee.getTypeSpec().toString() << ")." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_BUILD_SLICE: {
            uint8_t flags = *frame.ip++;
            Value stepVal = pop();
            Value endVal = pop();
            Value startVal = pop();

            SlicePtr slice = std::make_shared<ObjSlice>();
            slice->start = startVal;
            slice->end = endVal;
            slice->step = stepVal;
            slice->hasStart = (flags & 1) != 0;
            slice->hasEnd = (flags & 2) != 0;
            slice->hasStep = (flags & 4) != 0;

            push(Value(slice));
            break;
        }
        case OpCode::OP_GET_MEMBER: {
            uint16_t nameIdx = read16(frame.ip);
            std::string memberName = frame.closure->function->chunk.constants[nameIdx].str;
            Value target = pop();

            if (target.isModule() && target.module) {
                ModulePtr mod = target.module;
                auto symIt = mod->symbols.find(memberName);
                if (symIt == mod->symbols.end()) {
                    std::cout << "[Member Error]: Member '" << memberName << "' does not exist in module '" << mod->name << "'." << std::endl;
                    return false;
                }

                if (!symIt->second.isPublic && frame.closure->module != mod) {
                    std::cout << "[Module Error]: '" << memberName << "' is private in module '" << mod->name << "'." << std::endl;
                    return false;
                }

                push(mod->globals[memberName]);
                break;
            }

            if (target.isStruct()) {
                StructInstancePtr inst = target.structInstance;
                if (!inst || !inst->def) {
                    std::cout << "[Runtime Error]: Accessing member on invalid struct instance." << std::endl;
                    return false;
                }
                int fIdx = inst->def->findField(memberName);
                if (fIdx != -1) {
                    const auto& fInfo = inst->def->fields[fIdx];
                    if (!fInfo.isPublic) {
                        bool allowed = false;
                        if (frame.closure->function && frame.closure->function->isMethod && stack.size() > frame.slotsOffset + 1) {
                            const Value& rec = stack[frame.slotsOffset + 1];
                            if (rec.isStruct() && rec.structInstance && rec.structInstance->def && rec.structInstance->def == inst->def) {
                                allowed = true;
                            }
                        }
                        if (!allowed) {
                            std::cout << "[Module Error]: Cannot access private field '" << memberName << "' of struct '" << inst->def->name << "'." << std::endl;
                            return false;
                        }
                    }
                    auto it = inst->fields.find(memberName);
                    push((it != inst->fields.end()) ? it->second : Value());
                    break;
                }

                auto mIt = inst->def->methods.find(memberName);
                if (mIt != inst->def->methods.end()) {
                    const auto& mInfo = mIt->second;
                    if (!mInfo.isPublic) {
                        bool allowed = false;
                        if (frame.closure->function && frame.closure->function->isMethod && stack.size() > frame.slotsOffset + 1) {
                            const Value& rec = stack[frame.slotsOffset + 1];
                            if (rec.isStruct() && rec.structInstance && rec.structInstance->def && rec.structInstance->def == inst->def) {
                                allowed = true;
                            }
                        }
                        if (!allowed) {
                            std::cout << "[Module Error]: Cannot access private method '" << memberName << "' of struct '" << inst->def->name << "'." << std::endl;
                            return false;
                        }
                    }
                    auto bm = std::make_shared<ObjBoundMethod>();
                    bm->receiver = target;
                    bm->method = mInfo.function;
                    if (bm->method && !bm->method->module) {
                        bm->method->module = inst->def->module;
                    }
                    push(Value(bm));
                    break;
                }

                std::cout << "[Member Error]: Struct '" << inst->def->name << "' has no field or method named '" << memberName << "'." << std::endl;
                return false;
            }

            if (target.isArray()) {
                ArrayPtr arr = target.array;
                if (memberName == "length") {
                    push(Value(static_cast<int64_t>(arr ? arr->size() : 0)));
                } else if (memberName == "push") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        if (!arr) throw std::runtime_error("[Runtime Error]: Invalid array target.");
                        if (arr->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const array.");
                        if (argNames.empty()) {
                            if (argCount < 1) throw std::runtime_error("[Runtime Error]: push() expects at least 1 argument.");
                            for (int i = 0; i < argCount; ++i) {
                                Value val = args[i];
                                if (arr->typeSpec.elementKind != TypeKind::ANY && arr->typeSpec.elementKind != TypeKind::UNTYPED) {
                                    TypeSpec expectedElemSpec = arr->typeSpec.elemType ? *arr->typeSpec.elemType : TypeSpec{arr->typeSpec.elementKind};
                                    if (!checkAndCoerceValueType(expectedElemSpec, val)) {
                                        throw std::runtime_error("[Runtime Error]: Array push type mismatch.");
                                    }
                                }
                                arr->push_back(cloneStructValue(val));
                            }
                        } else {
                            ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val"}}, "push()");
                            if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: push() expects at least 1 argument.");
                            Value val = aMap.get("val");
                            if (arr->typeSpec.elementKind != TypeKind::ANY && arr->typeSpec.elementKind != TypeKind::UNTYPED) {
                                TypeSpec expectedElemSpec = arr->typeSpec.elemType ? *arr->typeSpec.elemType : TypeSpec{arr->typeSpec.elementKind};
                                if (!checkAndCoerceValueType(expectedElemSpec, val)) {
                                    throw std::runtime_error("[Runtime Error]: Array push type mismatch.");
                                }
                            }
                            arr->push_back(cloneStructValue(val));
                        }
                        return Value(arr);
                    })));
                } else if (memberName == "pop") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        if (!arr || arr->empty()) throw std::runtime_error("[Runtime Error]: Cannot pop from empty array.");
                        if (arr->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const array.");
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"ind"}}, "pop()");
                        int64_t idx = arr->size() - 1;
                        if (aMap.has("ind")) {
                            Value indVal = aMap.get("ind");
                            if (!indVal.isInt()) throw std::runtime_error("[Runtime Error]: pop() ind must be an integer.");
                            idx = indVal.intVal;
                            if (idx < 0) idx = static_cast<int64_t>(arr->size()) + idx;
                        }
                        if (idx < 0 || idx >= static_cast<int64_t>(arr->size())) {
                            throw std::runtime_error("[Runtime Error]: Array pop index out of bounds.");
                        }
                        Value removed = (*arr)[idx];
                        arr->erase(arr->begin() + idx);
                        return removed;
                    })));
                } else if (memberName == "insert") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        if (!arr) throw std::runtime_error("[Runtime Error]: Invalid array target.");
                        if (arr->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const array.");
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"ind"}, {"val"}}, "insert()");
                        if (!aMap.has("ind") || !aMap.has("val")) {
                            throw std::runtime_error("[Runtime Error]: insert() requires 'ind' and 'val' arguments.");
                        }
                        Value indVal = aMap.get("ind");
                        Value val = aMap.get("val");
                        if (!indVal.isInt()) throw std::runtime_error("[Runtime Error]: insert() ind must be an integer.");

                        int64_t len = static_cast<int64_t>(arr->size());
                        int64_t idx = indVal.intVal;
                        if (idx == -1) idx = len;
                        else if (idx < 0) idx = len + 1 + idx;

                        if (idx < 0 || idx > len) {
                            throw std::runtime_error("[Runtime Error]: Array insert index out of bounds.");
                        }

                        if (arr->typeSpec.elementKind != TypeKind::ANY && arr->typeSpec.elementKind != TypeKind::UNTYPED) {
                            TypeSpec expectedElemSpec = arr->typeSpec.elemType ? *arr->typeSpec.elemType : TypeSpec{arr->typeSpec.elementKind};
                            if (!checkAndCoerceValueType(expectedElemSpec, val)) {
                                throw std::runtime_error("[Runtime Error]: Array insert type mismatch.");
                            }
                        }

                        arr->checkLock();
                        arr->elements.insert(arr->elements.begin() + idx, cloneStructValue(val));
                        return Value(arr);
                    })));
                } else if (memberName == "remove") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        if (!arr) throw std::runtime_error("[Runtime Error]: Invalid array target.");
                        if (arr->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const array.");
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"ind"}, {"val"}}, "remove()");
                        bool hasInd = aMap.has("ind");
                        bool hasVal = aMap.has("val");

                        if ((hasInd && hasVal) || (!hasInd && !hasVal)) {
                            throw std::runtime_error("[Runtime Error]: remove() expects either 'ind' or 'val', not both.");
                        }

                        if (hasInd) {
                            Value indVal = aMap.get("ind");
                            if (!indVal.isInt()) throw std::runtime_error("[Runtime Error]: remove() ind must be an integer.");
                            int64_t len = static_cast<int64_t>(arr->size());
                            int64_t idx = indVal.intVal;
                            if (idx < 0) idx = len + idx;
                            if (idx < 0 || idx >= len) {
                                throw std::runtime_error("[Runtime Error]: Array remove index out of bounds.");
                            }
                            Value removed = (*arr)[idx];
                            arr->erase(arr->begin() + idx);
                            return removed;
                        } else {
                            Value valVal = aMap.get("val");
                            for (auto it = arr->elements.begin(); it != arr->elements.end(); ++it) {
                                if (it->isEqual(valVal)) {
                                    Value removed = *it;
                                    arr->erase(it);
                                    return removed;
                                }
                            }
                            return Value(); // nil when not found
                        }
                    })));
                } else if (memberName == "contains") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "item", "element"}}, "contains()");
                        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: contains() expects 1 argument.");
                        Value searchVal = aMap.get("val");
                        if (!arr) return Value(false);
                        for (const Value& elem : arr->elements) {
                            if (elem.isEqual(searchVal)) return Value(true);
                        }
                        return Value(false);
                    })));
                } else if (memberName == "index_of") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "item", "element"}}, "index_of()");
                        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: index_of() expects 1 argument.");
                        Value searchVal = aMap.get("val");
                        if (!arr) return Value();
                        for (size_t i = 0; i < arr->size(); ++i) {
                            if ((*arr)[i].isEqual(searchVal)) return Value(static_cast<int64_t>(i));
                        }
                        return Value(); // nil
                    })));
                } else if (memberName == "reverse") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "reverse()");
                        if (arr) {
                            if (arr->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const array.");
                            arr->checkLock();
                            std::reverse(arr->elements.begin(), arr->elements.end());
                        }
                        return Value(arr);
                    })));
                } else if (memberName == "sort") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "sort()");
                        if (arr) {
                            if (arr->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const array.");
                            arr->checkLock();
                            std::stable_sort(arr->elements.begin(), arr->elements.end(), [](const Value& a, const Value& b) {
                                if (a.isNumber() && b.isNumber()) return a.asFloat() < b.asFloat();
                                if (a.isString() && b.isString()) return a.str < b.str;
                                if (a.isChar() && b.isChar()) return a.charVal < b.charVal;
                                if (a.isBool() && b.isBool()) return (!a.boolean && b.boolean);
                                throw std::runtime_error("[Runtime Error]: Array sort encountered non-comparable values.");
                            });
                        }
                        return Value(arr);
                    })));
                } else if (memberName == "slice") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"start"}, {"end"}, {"step"}}, "slice()");
                        SlicePtr slice = std::make_shared<ObjSlice>();
                        slice->start = aMap.get("start"); slice->hasStart = aMap.has("start");
                        slice->end = aMap.get("end"); slice->hasEnd = aMap.has("end");
                        slice->step = aMap.get("step"); slice->hasStep = aMap.has("step");
                        return performSlice(Value(arr), slice);
                    })));
                } else if (memberName == "clear") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "clear()");
                        if (arr) {
                            if (arr->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const array.");
                            arr->checkLock();
                            arr->elements.clear();
                        }
                        return Value(arr);
                    })));
                } else if (memberName == "join") {
                    push(Value(NativeFn([arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"separator", "sep", "delim"}}, "join()");
                        std::string delim = aMap.has("separator") ? aMap.get("separator").toString() : "";
                        if (!arr || arr->empty()) return Value(std::string(""));
                        std::string result = "";
                        for (size_t i = 0; i < arr->size(); ++i) {
                            if (i > 0) result += delim;
                            result += (*arr)[i].toString();
                        }
                        return Value(result);
                    })));
                } else if (memberName == "map") {
                    push(Value(NativeFn([this, arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"fn", "cb", "callback"}}, "map()");
                        if (!aMap.has("fn")) throw std::runtime_error("[Runtime Error]: map() expects 1 callback argument.");
                        Value cb = aMap.get("fn");
                        if (!arr) return Value(std::make_shared<ObjArray>());

                        arr->lockCount++;
                        ArrayPtr result = std::make_shared<ObjArray>();
                        result->typeSpec = arr->typeSpec;
                        try {
                            for (size_t i = 0; i < arr->size(); ++i) {
                                Value res = runCallback(cb, {(*arr)[i], Value(static_cast<int64_t>(i)), Value(arr)}, 3);
                                if (arr->typeSpec.elementKind != TypeKind::ANY && arr->typeSpec.elementKind != TypeKind::UNTYPED) {
                                    TypeSpec elemSpec = arr->typeSpec.elemType ? *arr->typeSpec.elemType : TypeSpec{arr->typeSpec.elementKind};
                                    if (!checkAndCoerceValueType(elemSpec, res)) {
                                        throw std::runtime_error("[Runtime Error]: Callback returned value of type " +
                                                                 res.getTypeSpec().toString() + " incompatible with array element type " +
                                                                 elemSpec.toString() + ".");
                                    }
                                }
                                result->push_back(res);
                            }
                        } catch (...) {
                            arr->lockCount--;
                            throw;
                        }
                        arr->lockCount--;
                        return Value(result);
                    })));
                } else if (memberName == "filter") {
                    push(Value(NativeFn([this, arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"fn", "cb", "callback"}}, "filter()");
                        if (!aMap.has("fn")) throw std::runtime_error("[Runtime Error]: filter() expects 1 callback argument.");
                        Value cb = aMap.get("fn");
                        if (!arr) return Value(std::make_shared<ObjArray>());

                        arr->lockCount++;
                        ArrayPtr result = std::make_shared<ObjArray>();
                        result->typeSpec = arr->typeSpec;
                        try {
                            for (size_t i = 0; i < arr->size(); ++i) {
                                Value res = runCallback(cb, {(*arr)[i], Value(static_cast<int64_t>(i)), Value(arr)}, 3);
                                if (!res.isFalsey()) {
                                    result->push_back((*arr)[i]);
                                }
                            }
                        } catch (...) {
                            arr->lockCount--;
                            throw;
                        }
                        arr->lockCount--;
                        return Value(result);
                    })));
                } else if (memberName == "reduce") {
                    push(Value(NativeFn([this, arr](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"fn", "cb", "callback"}, {"initial", "acc"}}, "reduce()");
                        if (!aMap.has("fn")) throw std::runtime_error("[Runtime Error]: reduce() expects callback argument.");
                        Value cb = aMap.get("fn");
                        bool hasInitial = aMap.has("initial");
                        Value acc = hasInitial ? aMap.get("initial") : Value();

                        if (!arr || arr->empty()) {
                            if (hasInitial) return acc;
                            return Value(); // nil
                        }

                        arr->lockCount++;
                        size_t startIdx = 0;
                        if (!hasInitial) {
                            acc = (*arr)[0];
                            startIdx = 1;
                        }

                        try {
                            for (size_t i = startIdx; i < arr->size(); ++i) {
                                acc = runCallback(cb, {acc, (*arr)[i], Value(static_cast<int64_t>(i)), Value(arr)}, 4);
                            }
                        } catch (...) {
                            arr->lockCount--;
                            throw;
                        }
                        arr->lockCount--;
                        return acc;
                    })));
                } else {
                    std::cout << "[Member Error]: Unknown array method or property '" << memberName << "'." << std::endl;
                    return false;
                }
                break;
            }

            if (target.isMap()) {
                MapPtr mapObj = target.map;
                if (memberName == "length") {
                    push(Value(static_cast<int64_t>(mapObj ? mapObj->keys.size() : 0)));
                } else if (memberName == "keys") {
                    push(Value(NativeFn([mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "keys()");
                        ArrayPtr arr = std::make_shared<ObjArray>();
                        if (mapObj) {
                            for (const Value& k : mapObj->keys) arr->push_back(k);
                        }
                        return Value(arr);
                    })));
                } else if (memberName == "values") {
                    push(Value(NativeFn([mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "values()");
                        ArrayPtr arr = std::make_shared<ObjArray>();
                        if (mapObj) {
                            for (const Value& k : mapObj->keys) arr->push_back(mapObj->get(k));
                        }
                        return Value(arr);
                    })));
                } else if (memberName == "contains") {
                    push(Value(NativeFn([mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"key"}}, "contains()");
                        if (!aMap.has("key")) throw std::runtime_error("[Runtime Error]: contains() expects 1 key argument.");
                        Value key = aMap.get("key");
                        if (!mapObj || !ObjMap::isSupportedKey(key)) return Value(false);
                        return Value(mapObj->contains(key));
                    })));
                } else if (memberName == "remove") {
                    push(Value(NativeFn([mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"key"}}, "remove()");
                        if (!aMap.has("key")) throw std::runtime_error("[Runtime Error]: remove() expects 1 key argument.");
                        Value key = aMap.get("key");
                        if (!mapObj || !ObjMap::isSupportedKey(key)) return Value();
                        if (mapObj->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const map.");
                        Value removed;
                        if (mapObj->remove(key, &removed)) return removed;
                        return Value(); // nil
                    })));
                } else if (memberName == "put") {
                    push(Value(NativeFn([mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        if (!mapObj) throw std::runtime_error("[Runtime Error]: Invalid map target.");
                        if (mapObj->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const map.");
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"key"}, {"val"}}, "put()");
                        if (!aMap.has("key") || !aMap.has("val")) {
                            throw std::runtime_error("[Runtime Error]: put() requires 'key' and 'val' arguments.");
                        }
                        Value key = aMap.get("key");
                        Value val = aMap.get("val");
                        if (!ObjMap::isSupportedKey(key)) {
                            throw std::runtime_error("[Runtime Error]: Map key must be a supported scalar type (int, float, string, char, bool).");
                        }
                        if (mapObj->typeSpec.keyKind != TypeKind::ANY && mapObj->typeSpec.keyKind != TypeKind::UNTYPED) {
                            TypeSpec expectedKeySpec = mapObj->typeSpec.keyType ? *mapObj->typeSpec.keyType : TypeSpec{mapObj->typeSpec.keyKind};
                            if (!checkAndCoerceValueType(expectedKeySpec, key)) {
                                throw std::runtime_error("[Runtime Error]: Map put key type mismatch: expected " +
                                                         expectedKeySpec.toString() + " but got " +
                                                         key.getTypeSpec().toString() + ".");
                            }
                        }
                        if (mapObj->typeSpec.valueKind != TypeKind::ANY && mapObj->typeSpec.valueKind != TypeKind::UNTYPED) {
                            TypeSpec expectedValSpec = mapObj->typeSpec.valType ? *mapObj->typeSpec.valType : TypeSpec{mapObj->typeSpec.valueKind};
                            if (!checkAndCoerceValueType(expectedValSpec, val)) {
                                throw std::runtime_error("[Runtime Error]: Map put value type mismatch: expected " +
                                                         expectedValSpec.toString() + " but got " +
                                                         val.getTypeSpec().toString() + ".");
                            }
                        }
                        mapObj->set(key, cloneStructValue(val));
                        return Value(mapObj);
                    })));
                } else if (memberName == "get") {
                    push(Value(NativeFn([mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"key"}}, "get()");
                        if (!aMap.has("key")) throw std::runtime_error("[Runtime Error]: get() expects 1 key argument.");
                        Value key = aMap.get("key");
                        if (!mapObj || !ObjMap::isSupportedKey(key)) return Value();
                        return mapObj->get(key);
                    })));
                } else if (memberName == "clear") {
                    push(Value(NativeFn([mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "clear()");
                        if (mapObj) {
                            if (mapObj->isConst) throw std::runtime_error("[Runtime Error]: Cannot call mutating method on const map.");
                            mapObj->checkLock();
                            mapObj->table.clear();
                            mapObj->keys.clear();
                        }
                        return Value(mapObj);
                    })));
                } else if (memberName == "map") {
                    push(Value(NativeFn([this, mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"fn", "cb", "callback"}}, "map()");
                        if (!aMap.has("fn")) throw std::runtime_error("[Runtime Error]: map() expects 1 callback argument.");
                        Value cb = aMap.get("fn");
                        if (!mapObj) return Value(std::make_shared<ObjMap>());

                        mapObj->lockCount++;
                        MapPtr result = std::make_shared<ObjMap>();
                        result->typeSpec = mapObj->typeSpec;
                        try {
                            for (const Value& k : mapObj->keys) {
                                Value v = mapObj->get(k);
                                Value res = runCallback(cb, {v, k, Value(mapObj)}, 3);
                                if (mapObj->typeSpec.valueKind != TypeKind::ANY && mapObj->typeSpec.valueKind != TypeKind::UNTYPED) {
                                    TypeSpec valSpec = mapObj->typeSpec.valType ? *mapObj->typeSpec.valType : TypeSpec{mapObj->typeSpec.valueKind};
                                    if (!checkAndCoerceValueType(valSpec, res)) {
                                        throw std::runtime_error("[Runtime Error]: Callback returned value of type " +
                                                                 res.getTypeSpec().toString() + " incompatible with map value type " +
                                                                 valSpec.toString() + ".");
                                    }
                                }
                                result->set(k, res);
                            }
                        } catch (...) {
                            mapObj->lockCount--;
                            throw;
                        }
                        mapObj->lockCount--;
                        return Value(result);
                    })));
                } else if (memberName == "filter") {
                    push(Value(NativeFn([this, mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"fn", "cb", "callback"}}, "filter()");
                        if (!aMap.has("fn")) throw std::runtime_error("[Runtime Error]: filter() expects 1 callback argument.");
                        Value cb = aMap.get("fn");
                        if (!mapObj) return Value(std::make_shared<ObjMap>());

                        mapObj->lockCount++;
                        MapPtr result = std::make_shared<ObjMap>();
                        result->typeSpec = mapObj->typeSpec;
                        try {
                            for (const Value& k : mapObj->keys) {
                                Value v = mapObj->get(k);
                                Value res = runCallback(cb, {v, k, Value(mapObj)}, 3);
                                if (!res.isFalsey()) {
                                    result->set(k, v);
                                }
                            }
                        } catch (...) {
                            mapObj->lockCount--;
                            throw;
                        }
                        mapObj->lockCount--;
                        return Value(result);
                    })));
                } else if (memberName == "reduce") {
                    push(Value(NativeFn([this, mapObj](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"fn", "cb", "callback"}, {"initial", "acc"}}, "reduce()");
                        if (!aMap.has("fn")) throw std::runtime_error("[Runtime Error]: reduce() expects callback argument.");
                        Value cb = aMap.get("fn");
                        bool hasInitial = aMap.has("initial");
                        Value acc = hasInitial ? aMap.get("initial") : Value();

                        if (!mapObj || mapObj->keys.empty()) {
                            if (hasInitial) return acc;
                            return Value(); // nil
                        }

                        mapObj->lockCount++;
                        size_t startIdx = 0;
                        if (!hasInitial) {
                            acc = mapObj->get(mapObj->keys[0]);
                            startIdx = 1;
                        }

                        try {
                            for (size_t i = startIdx; i < mapObj->keys.size(); ++i) {
                                Value k = mapObj->keys[i];
                                Value v = mapObj->get(k);
                                acc = runCallback(cb, {acc, v, k, Value(mapObj)}, 4);
                            }
                        } catch (...) {
                            mapObj->lockCount--;
                            throw;
                        }
                        mapObj->lockCount--;
                        return acc;
                    })));
                } else {
                    std::cout << "[Member Error]: Unknown map method or property '" << memberName << "'." << std::endl;
                    return false;
                }
                break;
            }

            if (target.isString()) {
                std::string sVal = target.str;
                if (memberName == "length") {
                    push(Value(static_cast<int64_t>(utf8_length(sVal))));
                } else if (memberName == "upper") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "upper()");
                        return Value(utf8_upper(sVal));
                    })));
                } else if (memberName == "lower") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "lower()");
                        return Value(utf8_lower(sVal));
                    })));
                } else if (memberName == "trim") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        (void)args;
                        parseCallArgs(argCount, args, argNames, {}, "trim()");
                        return Value(utf8_trim(sVal));
                    })));
                } else if (memberName == "contains") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"val", "substr", "str"}}, "contains()");
                        if (!aMap.has("val")) throw std::runtime_error("[Runtime Error]: contains() expects 1 string argument.");
                        Value sub = aMap.get("val");
                        if (!sub.isString()) throw std::runtime_error("[Runtime Error]: contains() expects 1 string argument.");
                        return Value(sVal.find(sub.str) != std::string::npos);
                    })));
                } else if (memberName == "starts_with") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"prefix", "val", "str"}}, "starts_with()");
                        if (!aMap.has("prefix")) throw std::runtime_error("[Runtime Error]: starts_with() expects 1 string argument.");
                        Value pre = aMap.get("prefix");
                        if (!pre.isString()) throw std::runtime_error("[Runtime Error]: starts_with() expects 1 string argument.");
                        return Value(sVal.rfind(pre.str, 0) == 0);
                    })));
                } else if (memberName == "ends_with") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"suffix", "val", "str"}}, "ends_with()");
                        if (!aMap.has("suffix")) throw std::runtime_error("[Runtime Error]: ends_with() expects 1 string argument.");
                        Value suf = aMap.get("suffix");
                        if (!suf.isString()) throw std::runtime_error("[Runtime Error]: ends_with() expects 1 string argument.");
                        if (suf.str.length() > sVal.length()) return Value(false);
                        return Value(sVal.compare(sVal.length() - suf.str.length(), suf.str.length(), suf.str) == 0);
                    })));
                } else if (memberName == "split") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        ArgMap aMap = parseCallArgs(argCount, args, argNames, {{"delimiter", "sep", "val"}}, "split()");
                        ArrayPtr arr = std::make_shared<ObjArray>();
                        if (!aMap.has("delimiter")) {
                            std::vector<std::string> chars = utf8_to_chars(sVal);
                            std::string currentToken = "";
                            for (const auto& cStr : chars) {
                                if (is_unicode_space(utf8_code_point(cStr))) {
                                    if (!currentToken.empty()) {
                                        arr->push_back(Value(currentToken));
                                        currentToken = "";
                                    }
                                } else {
                                    currentToken += cStr;
                                }
                            }
                            if (!currentToken.empty()) {
                                arr->push_back(Value(currentToken));
                            }
                            return Value(arr);
                        }
                        Value delimVal = aMap.get("delimiter");
                        if (!delimVal.isString()) throw std::runtime_error("[Runtime Error]: split() expects string delimiter.");
                        std::string delim = delimVal.str;
                        if (delim.empty()) {
                            for (const auto& cStr : utf8_to_chars(sVal)) {
                                arr->push_back(Value(cStr));
                            }
                        } else {
                            size_t start = 0;
                            size_t end = sVal.find(delim);
                            while (end != std::string::npos) {
                                arr->push_back(Value(sVal.substr(start, end - start)));
                                start = end + delim.length();
                                end = sVal.find(delim, start);
                            }
                            arr->push_back(Value(sVal.substr(start)));
                        }
                        return Value(arr);
                    })));
                } else if (memberName == "replace") {
                    push(Value(NativeFn([sVal](int argCount, Value* args, const std::vector<std::string>& argNames) -> Value {
                        int namedCount = static_cast<int>(argNames.size());
                        int posCount = argCount - namedCount;

                        Value valVal, indVal, replacementVal;
                        bool hasVal = false, hasInd = false, hasReplacement = false;

                        for (int i = 0; i < namedCount; ++i) {
                            std::string name = argNames[i];
                            Value val = args[posCount + i];
                            if (name == "val") {
                                if (hasVal) throw std::runtime_error("[Runtime Error]: Duplicate argument 'val'.");
                                hasVal = true; valVal = val;
                            } else if (name == "ind") {
                                if (hasInd) throw std::runtime_error("[Runtime Error]: Duplicate argument 'ind'.");
                                hasInd = true; indVal = val;
                            } else if (name == "replacement") {
                                if (hasReplacement) throw std::runtime_error("[Runtime Error]: Duplicate argument 'replacement'.");
                                hasReplacement = true; replacementVal = val;
                            } else {
                                throw std::runtime_error("[Runtime Error]: Unexpected argument name '" + name + "' for replace().");
                            }
                        }

                        if (hasVal && hasInd) {
                            throw std::runtime_error("[Runtime Error]: replace() cannot specify both 'val' and 'ind'.");
                        }

                        int posIdx = 0;
                        if (!hasVal && !hasInd) {
                            if (posIdx < posCount) {
                                Value firstPos = args[posIdx++];
                                if (firstPos.isSlice() || firstPos.isInt()) {
                                    hasInd = true; indVal = firstPos;
                                } else {
                                    hasVal = true; valVal = firstPos;
                                }
                            }
                        }

                        if (!hasReplacement) {
                            if (posIdx < posCount) {
                                hasReplacement = true;
                                replacementVal = args[posIdx++];
                            }
                        }

                        if (posIdx < posCount) {
                            throw std::runtime_error("[Runtime Error]: Too many arguments provided for replace().");
                        }

                        if ((!hasVal && !hasInd) || !hasReplacement) {
                            throw std::runtime_error("[Runtime Error]: replace() missing required arguments.");
                        }

                        if (hasVal) {
                            if (!valVal.isString() || !replacementVal.isString()) {
                                throw std::runtime_error("[Runtime Error]: replace() val mode expects string arguments.");
                            }
                            std::string from = valVal.str;
                            std::string to = replacementVal.str;
                            if (from.empty()) {
                                throw std::runtime_error("[Runtime Error]: replace() val cannot be empty.");
                            }
                            std::string res = sVal;
                            size_t pos = res.find(from);
                            if (pos != std::string::npos) {
                                res.replace(pos, from.length(), to);
                            }
                            return Value(res);
                        }

                        if (hasInd) {
                            if (!replacementVal.isString()) throw std::runtime_error("[Runtime Error]: replace() replacement must be a string.");
                            std::string to = replacementVal.str;
                            std::vector<std::string> chars = utf8_to_chars(sVal);
                            int64_t len = static_cast<int64_t>(chars.size());

                            if (indVal.isSlice()) {
                                SlicePtr slice = indVal.slice;
                                int64_t start = slice->hasStart ? (slice->start.isInt() ? slice->start.intVal : 0) : 0;
                                int64_t end = slice->hasEnd ? (slice->end.isInt() ? slice->end.intVal : len) : len;

                                if (start < 0) start = len + start;
                                if (end < 0) end = len + end;

                                start = std::max(int64_t(0), std::min(start, len));
                                end = std::max(int64_t(0), std::min(end, len));

                                std::string res = "";
                                for (int64_t i = 0; i < start; ++i) res += chars[i];
                                res += to;
                                for (int64_t i = end; i < len; ++i) res += chars[i];
                                return Value(res);
                            } else if (indVal.isInt()) {
                                int64_t idx = indVal.intVal;
                                if (idx < 0) idx = len + idx;
                                idx = std::max(int64_t(0), std::min(idx, len));

                                std::string res = "";
                                for (int64_t i = 0; i < idx; ++i) res += chars[i];
                                res += to;
                                return Value(res);
                            } else {
                                throw std::runtime_error("[Runtime Error]: replace() ind must be integer or slice.");
                            }
                        }

                        throw std::runtime_error("[Runtime Error]: replace() requires either 'val' or 'ind'.");
                    })));
                } else {
                    std::cout << "[Member Error]: Unknown string method or property '" << memberName << "'." << std::endl;
                    return false;
                }
                break;
            }

            std::cout << "[Member Error]: Cannot access member '" << memberName << "' on non-object value." << std::endl;
            return false;
        }
        case OpCode::OP_MAKE_CONST: {
            Value val = pop();
            push(makeConst(val));
            break;
        }
        case OpCode::OP_SET_MEMBER: {
            uint16_t nameIdx = read16(frame.ip);
            std::string memberName = frame.closure->function->chunk.constants[nameIdx].str;
            Value val = pop();
            Value target = pop();

            if (target.isStruct()) {
                StructInstancePtr inst = target.structInstance;
                if (!inst || !inst->def) {
                    std::cout << "[Runtime Error]: Setting field on invalid struct instance." << std::endl;
                    return false;
                }
                if (inst->isConst) {
                    std::cout << "[Runtime Error]: Cannot mutate const struct instance." << std::endl;
                    return false;
                }
                int fIdx = inst->def->findField(memberName);
                if (fIdx == -1) {
                    std::cout << "[Member Error]: Unknown field '" << memberName << "' in struct '" << inst->def->name << "'." << std::endl;
                    return false;
                }
                const auto& fInfo = inst->def->fields[fIdx];
                if (fInfo.isConst) {
                    std::cout << "[Runtime Error]: Cannot assign to constant field '" << memberName << "'." << std::endl;
                    return false;
                }
                if (!fInfo.isPublic) {
                    bool allowed = false;
                    if (frame.closure->function && frame.closure->function->isMethod && stack.size() > frame.slotsOffset + 1) {
                        const Value& rec = stack[frame.slotsOffset + 1];
                        if (rec.isStruct() && rec.structInstance && rec.structInstance->def && rec.structInstance->def == inst->def) {
                            allowed = true;
                        }
                    }
                    if (!allowed) {
                        std::cout << "[Module Error]: Cannot access private field '" << memberName << "' of struct '" << inst->def->name << "'." << std::endl;
                        return false;
                    }
                }
                if (!val.isNil()) {
                    if (!checkAndCoerceValueType(fInfo.typeSpec, val, inst->def->module ? inst->def->module : frame.closure->module)) {
                        std::cout << "[Runtime Error]: Type mismatch for field '" << memberName << "': expected " << fInfo.typeSpec.toString() << " but got " << val.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                }
                inst->fields[memberName] = cloneValue(val);
                push(val);
                break;
            }

            if (!target.isModule() || !target.module) {
                std::cout << "[Member Error]: Cannot set member '" << memberName << "' on non-module value." << std::endl;
                return false;
            }

            ModulePtr mod = target.module;
            auto symIt = mod->symbols.find(memberName);
            if (symIt == mod->symbols.end()) {
                std::cout << "[Member Error]: Member '" << memberName << "' does not exist in module '" << mod->name << "'." << std::endl;
                return false;
            }

            if (!symIt->second.isPublic && frame.closure->module != mod) {
                std::cout << "[Module Error]: '" << memberName << "' is private in module '" << mod->name << "'." << std::endl;
                return false;
            }

            if (symIt->second.isConst) {
                std::cout << "[Runtime Error]: Cannot reassign constant variable '" << memberName << "'." << std::endl;
                return false;
            }

            if (symIt->second.typeSpec.kind != TypeKind::ANY && symIt->second.typeSpec.kind != TypeKind::UNTYPED) {
                if (!checkAndCoerceValueType(symIt->second.typeSpec, val)) {
                    std::cout << "[Runtime Error]: Type mismatch for member '" << memberName << "'." << std::endl;
                    return false;
                }
            }

            mod->globals[memberName] = val;
            push(val);
            break;
        }
        case OpCode::OP_SET_MEMBER_POST: {
            uint16_t nameIdx = read16(frame.ip);
            std::string memberName = frame.closure->function->chunk.constants[nameIdx].str;
            Value val = pop();
            Value target = pop();

            if (!target.isModule() || !target.module) {
                std::cout << "[Member Error]: Cannot set member '" << memberName << "' on non-module value." << std::endl;
                return false;
            }

            ModulePtr mod = target.module;
            auto symIt = mod->symbols.find(memberName);
            if (symIt == mod->symbols.end()) {
                std::cout << "[Member Error]: Member '" << memberName << "' does not exist in module '" << mod->name << "'." << std::endl;
                return false;
            }

            if (!symIt->second.isPublic && frame.closure->module != mod) {
                std::cout << "[Module Error]: '" << memberName << "' is private in module '" << mod->name << "'." << std::endl;
                return false;
            }

            if (symIt->second.isConst) {
                std::cout << "[Runtime Error]: Cannot reassign constant variable '" << memberName << "'." << std::endl;
                return false;
            }

            Value oldVal = mod->globals[memberName];

            if (symIt->second.typeSpec.kind != TypeKind::ANY && symIt->second.typeSpec.kind != TypeKind::UNTYPED) {
                if (!checkAndCoerceValueType(symIt->second.typeSpec, val)) {
                    std::cout << "[Runtime Error]: Type mismatch for member '" << memberName << "'." << std::endl;
                    return false;
                }
            }

            mod->globals[memberName] = val;
            push(oldVal);
            break;
        }
        case OpCode::OP_SET_INDEX_POST: {
            Value val = pop();
            Value indexVal = pop();
            Value target = pop();

            if (target.isMap()) {
                if (!ObjMap::isSupportedKey(indexVal)) {
                    std::cout << "[Runtime Error]: Map key must be a supported scalar type (int, float, string, char, bool)." << std::endl;
                    return false;
                }
                if (!target.map) {
                    std::cout << "[Runtime Error]: Invalid map target." << std::endl;
                    return false;
                }
                if (target.map->isConst) {
                    std::cout << "[Runtime Error]: Cannot mutate const map." << std::endl;
                    return false;
                }
                Value oldVal = target.map->contains(indexVal) ? target.map->get(indexVal) : Value();
                if (target.map->typeSpec.keyKind != TypeKind::ANY && target.map->typeSpec.keyKind != TypeKind::UNTYPED) {
                    TypeSpec expectedKeySpec = target.map->typeSpec.keyType ? *target.map->typeSpec.keyType : TypeSpec{target.map->typeSpec.keyKind};
                    if (!checkAndCoerceValueType(expectedKeySpec, indexVal)) {
                        std::cout << "[Runtime Error]: Type mismatch for map key assignment: expected "
                                  << expectedKeySpec.toString() << " but got "
                                  << indexVal.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                }
                if (target.map->typeSpec.valueKind != TypeKind::ANY && target.map->typeSpec.valueKind != TypeKind::UNTYPED) {
                    TypeSpec expectedValSpec = target.map->typeSpec.valType ? *target.map->typeSpec.valType : TypeSpec{target.map->typeSpec.valueKind};
                    if (!checkAndCoerceValueType(expectedValSpec, val)) {
                        std::cout << "[Runtime Error]: Type mismatch for map assignment: expected "
                                  << expectedValSpec.toString() << " but got "
                                  << val.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                }
                target.map->set(indexVal, cloneStructValue(val));
                push(oldVal);
            } else if (target.isArray()) {
                if (!indexVal.isInt()) {
                    std::cout << "[Runtime Error]: Array index must be an integer." << std::endl;
                    return false;
                }
                int64_t index = indexVal.intVal;
                if (index < 0) index = static_cast<int64_t>(target.array ? target.array->size() : 0) + index;
                if (!target.array || index < 0 || index >= static_cast<int64_t>(target.array->size())) {
                    std::cout << "[Runtime Error]: Array index " << index << " out of bounds." << std::endl;
                    return false;
                }
                if (target.array->isConst) {
                    std::cout << "[Runtime Error]: Cannot mutate const array." << std::endl;
                    return false;
                }
                Value oldVal = (*target.array)[index];
                if (target.array->typeSpec.elementKind != TypeKind::ANY && target.array->typeSpec.elementKind != TypeKind::UNTYPED) {
                    TypeSpec expectedElemSpec = target.array->typeSpec.elemType ? *target.array->typeSpec.elemType : TypeSpec{target.array->typeSpec.elementKind};
                    if (!checkAndCoerceValueType(expectedElemSpec, val)) {
                        std::cout << "[Runtime Error]: Type mismatch for array assignment: expected "
                                  << expectedElemSpec.toString() << " but got "
                                  << val.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                }
                (*target.array)[index] = cloneStructValue(val);
                push(oldVal);
            } else {
                std::cout << "[Runtime Error]: Only arrays and maps support index assignment." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_BUILD_ARRAY: {
            uint16_t elementCount = read16(frame.ip);
            ArrayPtr arr = std::make_shared<ObjArray>();
            arr->resize(elementCount);
            for (int i = elementCount - 1; i >= 0; --i) {
                (*arr)[i] = cloneStructValue(pop());
            }
            push(Value(arr));
            break;
        }
        case OpCode::OP_BUILD_MAP: {
            uint16_t entryCount = read16(frame.ip);
            MapPtr mapObj = std::make_shared<ObjMap>();
            std::vector<std::pair<Value, Value>> entries(entryCount);
            for (int i = entryCount - 1; i >= 0; --i) {
                Value val = pop();
                Value keyVal = pop();
                if (!ObjMap::isSupportedKey(keyVal)) {
                    std::cout << "[Runtime Error]: Map key must be a supported scalar type." << std::endl;
                    return false;
                }
                entries[i] = {keyVal, cloneStructValue(val)};
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

            if (indexVal.isSlice()) {
                try {
                    push(performSlice(target, indexVal.slice));
                } catch (const std::exception& ex) {
                    std::cout << ex.what() << std::endl;
                    return false;
                }
                break;
            }

            if (target.isMap()) {
                if (!ObjMap::isSupportedKey(indexVal)) {
                    std::cout << "[Runtime Error]: Map key must be a supported scalar type." << std::endl;
                    return false;
                }
                if (target.map && target.map->contains(indexVal)) {
                    push(target.map->get(indexVal));
                } else {
                    push(Value()); // nil if key not found
                }
            } else if (target.isArray()) {
                if (!indexVal.isInt()) {
                    std::cout << "[Runtime Error]: Array index must be an integer." << std::endl;
                    return false;
                }
                int64_t index = indexVal.intVal;
                if (index < 0) index = static_cast<int64_t>(target.array ? target.array->size() : 0) + index;
                if (!target.array || index < 0 || index >= static_cast<int64_t>(target.array->size())) {
                    std::cout << "[Runtime Error]: Array index " << index << " out of bounds." << std::endl;
                    return false;
                }
                push((*target.array)[index]);
            } else if (target.isString()) {
                if (!indexVal.isInt()) {
                    std::cout << "[Runtime Error]: String index must be an integer." << std::endl;
                    return false;
                }
                std::vector<std::string> chars = utf8_to_chars(target.str);
                int64_t index = indexVal.intVal;
                if (index < 0) index = static_cast<int64_t>(chars.size()) + index;
                if (index < 0 || index >= static_cast<int64_t>(chars.size())) {
                    std::cout << "[Runtime Error]: String index " << index << " out of bounds." << std::endl;
                    return false;
                }
                push(Value(utf8_code_point(chars[index]), true));
            } else {
                std::cout << "[Runtime Error]: Only arrays, maps, and strings can be indexed." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_SET_INDEX: {
            Value val = pop();
            Value indexVal = pop();
            Value target = pop();

            if (target.isMap()) {
                if (!ObjMap::isSupportedKey(indexVal)) {
                    std::cout << "[Runtime Error]: Map key must be a supported scalar type." << std::endl;
                    return false;
                }
                if (!target.map) {
                    std::cout << "[Runtime Error]: Invalid map target." << std::endl;
                    return false;
                }
                if (target.map->isConst) {
                    std::cout << "[Runtime Error]: Cannot mutate const map." << std::endl;
                    return false;
                }
                if (target.map->typeSpec.keyKind != TypeKind::ANY && target.map->typeSpec.keyKind != TypeKind::UNTYPED) {
                    TypeSpec expectedKeySpec = target.map->typeSpec.keyType ? *target.map->typeSpec.keyType : TypeSpec{target.map->typeSpec.keyKind};
                    if (!checkAndCoerceValueType(expectedKeySpec, indexVal)) {
                        std::cout << "[Runtime Error]: Type mismatch for map key assignment: expected "
                                  << expectedKeySpec.toString() << " but got "
                                  << indexVal.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                }
                if (target.map->typeSpec.valueKind != TypeKind::ANY && target.map->typeSpec.valueKind != TypeKind::UNTYPED) {
                    TypeSpec expectedValSpec = target.map->typeSpec.valType ? *target.map->typeSpec.valType : TypeSpec{target.map->typeSpec.valueKind};
                    if (!checkAndCoerceValueType(expectedValSpec, val)) {
                        std::cout << "[Runtime Error]: Type mismatch for map assignment: expected "
                                  << expectedValSpec.toString() << " but got "
                                  << val.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                }
                target.map->set(indexVal, cloneStructValue(val));
                push(val);
            } else if (target.isArray()) {
                if (!indexVal.isInt()) {
                    std::cout << "[Runtime Error]: Array index must be an integer." << std::endl;
                    return false;
                }
                int64_t index = indexVal.intVal;
                if (index < 0) index = static_cast<int64_t>(target.array ? target.array->size() : 0) + index;
                if (!target.array || index < 0 || index >= static_cast<int64_t>(target.array->size())) {
                    std::cout << "[Runtime Error]: Array index " << index << " out of bounds." << std::endl;
                    return false;
                }
                if (target.array->isConst) {
                    std::cout << "[Runtime Error]: Cannot mutate const array." << std::endl;
                    return false;
                }
                if (target.array->typeSpec.elementKind != TypeKind::ANY && target.array->typeSpec.elementKind != TypeKind::UNTYPED) {
                    TypeSpec expectedElemSpec = target.array->typeSpec.elemType ? *target.array->typeSpec.elemType : TypeSpec{target.array->typeSpec.elementKind};
                    if (!checkAndCoerceValueType(expectedElemSpec, val)) {
                        std::cout << "[Runtime Error]: Type mismatch for array assignment: expected "
                                  << expectedElemSpec.toString() << " but got "
                                  << val.getTypeSpec().toString() << "." << std::endl;
                        return false;
                    }
                }
                (*target.array)[index] = cloneStructValue(val);
                push(val);
            } else {
                std::cout << "[Runtime Error]: Only arrays and maps support index assignment." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_GRAB: {
            uint16_t pathIdx = read16(frame.ip);
            uint16_t aliasIdx = read16(frame.ip);
            std::string subModPath = frame.closure->function->chunk.constants[pathIdx].str;
            std::string subAlias = frame.closure->function->chunk.constants[aliasIdx].str;

            ClosurePtr subClosure = nullptr;
            bool isNew = false;
            ModulePtr grabbedMod = loadModule(subModPath, frame.closure->module->path, subClosure, isNew);
            if (!grabbedMod) {
                return false;
            }

            auto attachOrMergeModule = [](ModulePtr parent, const std::string& name, ModulePtr newMod, bool isPublic, bool isConst) {
                auto it = parent->globals.find(name);
                if (it != parent->globals.end() && it->second.isModule() && it->second.module) {
                    ModulePtr existing = it->second.module;
                    if (existing != newMod) {
                        for (const auto& pair : existing->globals) {
                            if (newMod->globals.find(pair.first) == newMod->globals.end()) {
                                newMod->globals[pair.first] = pair.second;
                            }
                        }
                        for (const auto& pair : existing->symbols) {
                            if (newMod->symbols.find(pair.first) == newMod->symbols.end()) {
                                newMod->symbols[pair.first] = pair.second;
                            }
                        }
                    }
                }
                parent->globals[name] = Value(newMod);
                parent->symbols[name] = SymbolInfo{isPublic, isConst, TypeSpec{TypeKind::ANY}};
            };

            if (!subAlias.empty()) {
                auto existingSymIt = frame.closure->module->symbols.find(subAlias);
                if (existingSymIt != frame.closure->module->symbols.end() && existingSymIt->second.isConst) {
                    std::cout << "[Runtime Error]: Cannot reassign module alias '" << subAlias << "'." << std::endl;
                    return false;
                }

                attachOrMergeModule(frame.closure->module, subAlias, grabbedMod, false, true);
            } else {
                std::vector<std::string> parts;
                std::string token;
                std::stringstream ss(subModPath);
                while (std::getline(ss, token, '.')) {
                    parts.push_back(token);
                }

                if (parts.size() == 1) {
                    std::string name = parts[0];
                    attachOrMergeModule(frame.closure->module, name, grabbedMod, false, true);
                } else if (!parts.empty()) {
                    std::string rootName = parts[0];
                    ModulePtr parentMod = nullptr;
                    auto rootIt = frame.closure->module->globals.find(rootName);
                    if (rootIt != frame.closure->module->globals.end() && rootIt->second.isModule() && rootIt->second.module) {
                        parentMod = rootIt->second.module;
                    } else {
                        parentMod = std::make_shared<ObjModule>();
                        parentMod->name = rootName;
                        frame.closure->module->globals[rootName] = Value(parentMod);
                        frame.closure->module->symbols[rootName] = SymbolInfo{false, true, TypeSpec{TypeKind::ANY}};
                    }

                    for (size_t i = 1; i < parts.size() - 1; ++i) {
                        std::string partName = parts[i];
                        auto pIt = parentMod->globals.find(partName);
                        if (pIt != parentMod->globals.end() && pIt->second.isModule() && pIt->second.module) {
                            parentMod = pIt->second.module;
                        } else {
                            ModulePtr nextMod = std::make_shared<ObjModule>();
                            nextMod->name = partName;
                            parentMod->globals[partName] = Value(nextMod);
                            parentMod->symbols[partName] = SymbolInfo{true, false, TypeSpec{TypeKind::ANY}};
                            parentMod = nextMod;
                        }
                    }

                    std::string lastName = parts.back();
                    attachOrMergeModule(parentMod, lastName, grabbedMod, true, false);
                }
            }

            if (isNew && subClosure) {
                push(Value(subClosure));
                std::vector<std::string> emptyNames;
                if (!call(subClosure, 0, emptyNames, true)) {
                    // Initialization failed, roll back module cache
                    moduleCache.erase(grabbedMod->path);
                    auto itP = std::find(loadingStackPaths.begin(), loadingStackPaths.end(), grabbedMod->path);
                    if (itP != loadingStackPaths.end()) {
                        size_t idx = std::distance(loadingStackPaths.begin(), itP);
                        loadingStackPaths.erase(itP);
                        if (idx < loadingStackNames.size()) {
                            loadingStackNames.erase(loadingStackNames.begin() + idx);
                        }
                    }
                    return false;
                }
            }
            break;
        }
        case OpCode::OP_EQUAL: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct() && hasOperatorOverload("==", a, false)) {
                if (!callOperatorOverload("==", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '==' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            push(Value(a.isEqual(b)));
            break;
        }
        case OpCode::OP_NOT_EQUAL: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct() && hasOperatorOverload("!=", a, false)) {
                if (!callOperatorOverload("!=", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '!=' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            push(Value(!a.isEqual(b)));
            break;
        }
        case OpCode::OP_GREATER: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload(">", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '>' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '>' only supports numbers!" << std::endl;
                return false;
            }
            if (a.isInt() && b.isInt()) {
                push(Value(a.intVal > b.intVal));
            } else {
                push(Value(a.asFloat() > b.asFloat()));
            }
            break;
        }
        case OpCode::OP_GREATER_EQUAL: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload(">=", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '>=' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '>=' only supports numbers!" << std::endl;
                return false;
            }
            if (a.isInt() && b.isInt()) {
                push(Value(a.intVal >= b.intVal));
            } else {
                push(Value(a.asFloat() >= b.asFloat()));
            }
            break;
        }
        case OpCode::OP_LESS: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload("<", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '<' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '<' only supports numbers!" << std::endl;
                return false;
            }
            if (a.isInt() && b.isInt()) {
                push(Value(a.intVal < b.intVal));
            } else {
                push(Value(a.asFloat() < b.asFloat()));
            }
            break;
        }
        case OpCode::OP_LESS_EQUAL: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload("<=", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '<=' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '<=' only supports numbers!" << std::endl;
                return false;
            }
            if (a.isInt() && b.isInt()) {
                push(Value(a.intVal <= b.intVal));
            } else {
                push(Value(a.asFloat() <= b.asFloat()));
            }
            break;
        }
        case OpCode::OP_UNARY_MINUS: {
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload("-", a, Value(), true)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '-' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (a.isInt()) {
                if (a.intVal == std::numeric_limits<int64_t>::min()) {
                    std::cout << "[Runtime Error]: 64-bit integer negation overflow." << std::endl;
                    return false;
                }
                push(Value(-a.intVal));
            } else if (a.isFloat()) {
                push(Value(-a.floatVal));
            } else {
                std::cout << "[Runtime Error]: '-' operand must be a number." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_UNARY_PLUS: {
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload("+", a, Value(), true)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '+' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (a.isNumber()) {
                push(a);
            } else {
                std::cout << "[Runtime Error]: '+' operand must be a number." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_ADD: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload("+", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '+' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (a.isString() || b.isString() || a.isChar() || b.isChar()) {
                push(Value(a.toString() + b.toString()));
            } else if (a.isInt() && b.isInt()) {
                int64_t res;
                if (__builtin_add_overflow(a.intVal, b.intVal, &res)) {
                    std::cout << "[Runtime Error]: 64-bit integer addition overflow." << std::endl;
                    return false;
                }
                push(Value(res));
            } else if (a.isNumber() && b.isNumber()) {
                push(Value(a.asFloat() + b.asFloat()));
            } else {
                std::cout << "[Runtime Error]: '+' operands must be numbers, strings, or chars." << std::endl;
                return false;
            }
            break;
        }
        case OpCode::OP_SUBTRACT: {
            Value b = pop();
            Value a = pop();
            if (a.isStruct()) {
                if (!callOperatorOverload("-", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '-' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '-' only supports numbers!" << std::endl;
                return false;
            }
            if (a.isInt() && b.isInt()) {
                int64_t res;
                if (__builtin_sub_overflow(a.intVal, b.intVal, &res)) {
                    std::cout << "[Runtime Error]: 64-bit integer subtraction overflow." << std::endl;
                    return false;
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
            if (a.isStruct()) {
                if (!callOperatorOverload("*", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '*' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '*' only supports numbers!" << std::endl;
                return false;
            }
            if (a.isInt() && b.isInt()) {
                int64_t res;
                if (__builtin_mul_overflow(a.intVal, b.intVal, &res)) {
                    std::cout << "[Runtime Error]: 64-bit integer multiplication overflow." << std::endl;
                    return false;
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
            if (a.isStruct()) {
                if (!callOperatorOverload("/", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '/' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                runtimeError("'/' only supports numbers!");
                return false;
            }
            if (b.asFloat() == 0.0) {
                runtimeError("Division by zero!");
                return false;
            }
            if (a.isInt() && b.isInt()) {
                if (a.intVal == std::numeric_limits<int64_t>::min() && b.intVal == -1) {
                    std::cout << "[Runtime Error]: 64-bit integer division overflow." << std::endl;
                    return false;
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
            if (a.isStruct()) {
                if (!callOperatorOverload("%", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '%' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '%' only supports numbers!" << std::endl;
                return false;
            }
            if (b.asFloat() == 0.0) {
                std::cout << "[Runtime Error]: Modulo by zero!" << std::endl;
                return false;
            }
            if (a.isInt() && b.isInt()) {
                if (a.intVal == std::numeric_limits<int64_t>::min() && b.intVal == -1) {
                    std::cout << "[Runtime Error]: 64-bit integer modulo overflow." << std::endl;
                    return false;
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
            if (a.isStruct()) {
                if (!callOperatorOverload("^", a, b, false)) {
                    std::string aName = (a.structInstance && a.structInstance->def) ? a.structInstance->def->name : "struct";
                    std::cout << "[Runtime Error]: No matching operator overload '^' found for struct '" << aName << "'." << std::endl;
                    return false;
                }
                break;
            }
            if (!a.isNumber() || !b.isNumber()) {
                std::cout << "[Runtime Error]: '^' only supports numbers!" << std::endl;
                return false;
            }
            if (a.asFloat() == 0.0 && b.asFloat() < 0.0) {
                std::cout << "[Runtime Error]: Zero cannot be raised to a negative power." << std::endl;
                return false;
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
                    return false;
                }
                push(Value(result));
            } else {
                double powRes = std::pow(a.asFloat(), b.asFloat());
                if (std::isinf(powRes) || std::isnan(powRes)) {
                    std::cout << "[Runtime Error]: Floating point power overflow or invalid result." << std::endl;
                    return false;
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
        case OpCode::OP_INC_ARG_COUNT: {
            break;
        }
        case OpCode::OP_JUMP: {
            uint16_t offset = (static_cast<uint16_t>(frame.ip[0]) << 8) | frame.ip[1];
            frame.ip += 2 + offset;
            break;
        }
        case OpCode::OP_JUMP_IF_FALSE: {
            uint16_t offset = (static_cast<uint16_t>(frame.ip[0]) << 8) | frame.ip[1];
            frame.ip += 2;
            if (peek(0).isFalsey()) {
                frame.ip += offset;
            }
            break;
        }
        case OpCode::OP_LOOP: {
            uint16_t offset = (static_cast<uint16_t>(frame.ip[0]) << 8) | frame.ip[1];
            frame.ip += 2;
            frame.ip -= offset;
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
            Value result = cloneStructValue(pop());
            closeUpvalues(frame.slotsOffset);
            size_t slotsOffset = frame.slotsOffset;
            bool wasGrab = frame.isGrab;
            ModulePtr frameMod = frame.closure->module;
            frames.pop_back();
            if (frames.empty()) {
                pop(); // pop main function
                if (!loadingStackPaths.empty()) {
                    loadingStackPaths.pop_back();
                    loadingStackNames.pop_back();
                }
                if (frameMod) frameMod->isInitialized = true;
                return true;
            }
            stack.resize(slotsOffset);
            if (wasGrab) {
                if (!loadingStackPaths.empty()) {
                    loadingStackPaths.pop_back();
                    loadingStackNames.pop_back();
                }
                if (frameMod) frameMod->isInitialized = true;
            } else {
                push(result);
            }

            break;
        }
    }
    return true;
}

void VM::runtimeError(const std::string& message) {
    std::cout << "[Runtime Error]: " << message << std::endl;
    for (int i = static_cast<int>(frames.size()) - 1; i >= 0; i--) {
        CallFrame& frame = frames[i];
        FunctionPtr function = frame.closure->function;
        size_t instruction = frame.ip - function->chunk.code.data() - 1;
        int line = function->chunk.getLine(instruction);
        int column = function->chunk.getColumn(instruction);
        std::string fnName = function->name.empty() ? "<script>" : function->name;

        std::cout << "  at " << fnName << "() [line " << line << ", col " << column << "]" << std::endl;

        if (!function->chunk.source.empty()) {
            std::string lineStr = Lexer(function->chunk.source).getLineString(line);
            if (!lineStr.empty()) {
                std::cout << "    " << lineStr << std::endl;
                std::cout << "    ";
                int col = column > 1 ? column - 1 : 0;
                for (int c = 0; c < col; c++) {
                    if (c < static_cast<int>(lineStr.size()) && lineStr[c] == '\t') {
                        std::cout << "\t";
                    } else {
                        std::cout << " ";
                    }
                }
                std::cout << "^" << std::endl;
            }
        }
    }
}

bool VM::run(Chunk& mainChunk, const std::string& scriptPath) {
    resetStack();

    rootModule = std::make_shared<ObjModule>();
    rootModule->name = scriptPath.empty() ? "main" : scriptPath;
    try {
        if (!scriptPath.empty() && fs::exists(scriptPath)) {
            rootModule->path = fs::canonical(scriptPath).string();
        } else if (!scriptPath.empty()) {
            rootModule->path = fs::absolute(scriptPath).string();
        } else {
            rootModule->path = fs::current_path().string() + "/main.kek";
        }
    } catch (...) {
        rootModule->path = scriptPath;
    }

    for (const auto& pair : builtins) {
        rootModule->globals[pair.first] = pair.second;
        rootModule->symbols[pair.first] = SymbolInfo{true, false, TypeSpec{TypeKind::ANY}};
    }

    if (!rootModule->path.empty()) {
        moduleCache[rootModule->path] = rootModule;
        loadingStackPaths.push_back(rootModule->path);
        loadingStackNames.push_back(rootModule->name);
    }

    FunctionPtr mainFn = std::make_shared<ObjFunction>();
    mainFn->chunk = mainChunk;
    mainFn->localTypes = mainChunk.localTypes;
    mainFn->name = "main";
    mainFn->arity = 0;
    mainFn->module = rootModule;

    ClosurePtr mainClosure = std::make_shared<ObjClosure>();
    mainClosure->function = mainFn;
    mainClosure->module = rootModule;

    push(Value(mainClosure));
    std::vector<std::string> emptyNames;
    if (!call(mainClosure, 0, emptyNames)) {
        return false;
    }

    while (!frames.empty()) {
        CallFrame& frame = frames.back();
        OpCode instruction = static_cast<OpCode>(*frame.ip++);
        if (!executeInstruction(instruction, frame)) {
            return false;
        }
    }

    return true;
}
