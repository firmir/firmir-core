/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <variant>
#include <string>
#include <vector>
#include <ostream>
#include <unordered_map>

namespace Firmir {

enum class DataType {
    Null,
    Bool,
    Int,
    String,
    StringList,
};

std::string dataTypeToString(DataType type);

using StringListT = std::vector<std::string>;

struct Variant {
    Variant() = default;
    Variant(bool value);
    Variant(int64_t value);
    Variant(const std::string &value);
    Variant(const StringListT &value);

    DataType dataType() const;
    bool isNull() const;
    bool toBool(bool *valid = nullptr, bool defaultValue = false) const;
    int64_t toInt(bool *valid = nullptr, int64_t defaultValue = 0) const;
    std::string toString(bool *valid = nullptr, std::string defaultValue = std::string()) const;
    StringListT toStringList(bool *valid = nullptr, StringListT defaultValue = StringListT()) const;

private:
    DataType m_dataType = DataType::Null;
    std::variant<bool, int64_t, std::string, StringListT> m_value;
};

struct VariantMapT : public std::unordered_map<std::string, Variant>
{
    Variant get(const std::string &key, const Variant &defaultValue = Variant());
};

} // namespace Firmir

std::ostream &operator<<(std::ostream &out, const Firmir::Variant &value);
std::ostream &operator<<(std::ostream &out, const Firmir::StringListT &value);
std::ostream &operator<<(std::ostream &out, const Firmir::VariantMapT &value);
