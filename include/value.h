#pragma once
#include <string>
#include <sstream>

enum class ValueType { NUMBER, STRING, BOOL, NIL };

struct Value {
    ValueType type;
    double num;
    std::string str;
    bool boolean;

    Value() : type(ValueType::NIL), num(0.0), str(""), boolean(false) {}
    Value(double n) : type(ValueType::NUMBER), num(n), str(""), boolean(false) {}
    Value(std::string s) : type(ValueType::STRING), num(0.0), str(s), boolean(false) {}
    Value(bool b) : type(ValueType::BOOL), num(0.0), str(""), boolean(b) {}

    bool isNumber() const { return type == ValueType::NUMBER; }
    bool isString() const { return type == ValueType::STRING; }
    bool isBool() const { return type == ValueType::BOOL; }
    bool isNil() const { return type == ValueType::NIL; }

    bool isFalsey() const {
        if (isNil()) return true;
        if (isBool()) return !boolean;
        if (isNumber()) return num == 0.0;
        if (isString()) return str.empty();
        return false;
    }

    bool isEqual(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case ValueType::NIL: return true;
            case ValueType::BOOL: return boolean == other.boolean;
            case ValueType::NUMBER: return num == other.num;
            case ValueType::STRING: return str == other.str;
        }
        return false;
    }

    std::string toString() const {
        if (isNil()) return "nil";
        if (isBool()) return boolean ? "true" : "false";
        if (isString()) return str;
        std::ostringstream ss;
        ss << num;
        return ss.str();
    }
};
