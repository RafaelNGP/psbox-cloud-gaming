// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Minimal JSON value, parser and serializer.
//
// Self-contained on purpose: the PS5 payload SDK ships a bare libc++, and the
// xCloud protocol only needs plain objects, arrays, strings and numbers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace xc::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : data_(b) {}
    Value(int v) : data_(static_cast<double>(v)) {}
    Value(int64_t v) : data_(static_cast<double>(v)) {}
    Value(double v) : data_(v) {}
    Value(const char* s) : data_(std::in_place_type<std::string>, s) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Array a) : data_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : data_(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return static_cast<Type>(data_.index()); }
    bool isNull() const { return data_.index() == 0; }
    bool isBool() const { return data_.index() == 1; }
    bool isNumber() const { return data_.index() == 2; }
    bool isString() const { return data_.index() == 3; }
    bool isArray() const { return data_.index() == 4; }
    bool isObject() const { return data_.index() == 5; }

    bool asBool(bool def = false) const { return isBool() ? std::get<bool>(data_) : def; }
    double asNumber(double def = 0) const { return isNumber() ? std::get<double>(data_) : def; }
    // `def` too for a number no int64_t holds (1e400 is infinity).
    int64_t asInt(int64_t def = 0) const {
        if (!isNumber()) return def;
        double num = std::get<double>(data_);
        return num > -9.2e18 && num < 9.2e18 ? static_cast<int64_t>(num) : def;
    }
    const std::string& asString() const;
    std::string str(std::string def = {}) const { return isString() ? std::get<std::string>(data_) : def; }

    // Arrays.
    size_t size() const;
    const Value& operator[](size_t i) const;
    const Value& operator[](int i) const { return i < 0 ? (*this)[size_t(-1)] : (*this)[size_t(i)]; }
    void push(Value v);
    const Array& items() const;

    // Objects. Missing keys on a const lookup yield a shared Null value, so
    // chains like v["a"]["b"].str() are safe.
    const Value& operator[](std::string_view key) const;
    const Value& operator[](const char* key) const { return (*this)[std::string_view(key)]; }
    Value& set(const std::string& key, Value v);
    bool has(std::string_view key) const;
    const Object& members() const;

    std::string dump() const;

private:
    void dumpTo(std::string& out) const;

    using Storage = std::variant<std::monostate, bool, double, std::string,
                                 std::shared_ptr<Array>, std::shared_ptr<Object>>;
    Storage data_;
};

// Returns nullopt on malformed input; `error` (if given) receives a message.
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

void appendEscaped(std::string& out, std::string_view s);
std::string escape(std::string_view s);

}  // namespace xc::json
