#pragma once
#include <string>
#include <sstream>

enum class ValueType { NUMBER, STRING };

struct Value {
    ValueType type;
    double num;
    std::string str;

    Value() : type(ValueType::NUMBER), num(0.0), str("") {}
    Value(double n) : type(ValueType::NUMBER), num(n), str("") {}
    Value(std::string s) : type(ValueType::STRING), num(0.0), str(s) {}

    bool isNumber() const { return type == ValueType::NUMBER; }
    bool isString() const { return type == ValueType::STRING; }

    std::string toString() const {
        if (isString()) return str;
        std::ostringstream ss;
        ss << num;
        return ss.str();
    }
};
