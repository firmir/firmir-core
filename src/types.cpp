/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "types.h"

#include <exception>
#include <sstream>

namespace Firmir {

std::string dataTypeToString(DataType type)
{
    switch (type) {
    case DataType::Null: return "Null";
    case DataType::Bool: return "Bool";
    case DataType::Int: return "Int";
    case DataType::String: return "String";
    case DataType::StringList: return "StringList";
    default: return "Unknown";
    }
}

Variant::Variant(bool value)
    : m_dataType(DataType::Bool)
    , m_value(value)
{
}

Variant::Variant(int64_t value)
    : m_dataType(DataType::Int)
    , m_value(value)
{
}

Variant::Variant(const std::string &value)
    : m_dataType(DataType::String)
    , m_value(value)
{
}

Variant::Variant(const StringListT &value)
    : m_dataType(DataType::StringList)
    , m_value(value)
{
}

DataType Variant::dataType() const
{
    return m_dataType;
}

bool Variant::isNull() const
{
    return m_dataType == DataType::Null;
}

bool Variant::toBool(bool *valid, bool defaultValue) const
{
    if (valid)
        *valid = true;
    if (std::holds_alternative<bool>(m_value))
        return std::get<bool>(m_value);
    if (std::holds_alternative<std::string>(m_value)) {
        auto value = std::get<std::string>(m_value);
        if (value == "true")
            return true;
        else if (value == "false")
            return false;
    }
    if (valid)
        *valid = false;
    return defaultValue;
}

int64_t Variant::toInt(bool *valid, int64_t defaultValue) const
{
    if (valid)
        *valid = true;
    if (std::holds_alternative<int64_t>(m_value))
        return std::get<int64_t>(m_value);
    if (std::holds_alternative<std::string>(m_value)) {
        auto value = std::get<std::string>(m_value);
        try {
            return static_cast<int64_t>(std::stoll(value));
        } catch (const std::exception &) {
        }
    }

    if (valid)
        *valid = false;
    return defaultValue;
}

std::string Variant::toString(bool *valid, std::string defaultValue) const
{
    if (valid)
        *valid = true;
    if (std::holds_alternative<std::string>(m_value))
        return std::get<std::string>(m_value);

    if (std::holds_alternative<StringListT>(m_value)) {
        auto value = std::get<StringListT>(m_value);
        if (value.size() == 1)
            return value[0];
    }

    if (valid)
        *valid = false;
    return defaultValue;
}

StringListT Variant::toStringList(bool *valid, StringListT defaultValue) const
{
    if (valid)
        *valid = true;
    if (std::holds_alternative<StringListT>(m_value))
        return std::get<StringListT>(m_value);
    if (valid)
        *valid = false;
    return defaultValue;
}

Variant VariantMapT::get(const std::string &key, const Variant &defaultValue)
{
    if (contains(key)) {
        auto res = (*this)[key];
        if (!res.isNull())
            return res;
    }

    return defaultValue;
}

} // namespace Firmir

std::ostream &operator<<(std::ostream &out, const Firmir::Variant &value)
{
    std::stringstream ss;
    ss << "Variant(dataType=" << dataTypeToString(value.dataType()) << ", value=";
    switch (value.dataType()) {
    case Firmir::DataType::Null: ss << "Null"; break;
    case Firmir::DataType::Bool: ss << value.toBool(); break;
    case Firmir::DataType::Int: ss << value.toInt(); break;
    case Firmir::DataType::String: ss << value.toString(); break;
    case Firmir::DataType::StringList: ss << value.toStringList(); break;
    default: break;
    }
    ss << ")";
    return out << ss.str();
}

std::ostream &operator<<(std::ostream &out, const Firmir::StringListT &value)
{
    std::stringstream ss;
    ss << "StringList(";
    for (size_t i = 0; i < value.size(); ++i)
        ss << "\"" << value[i] << "\"" << (i + 1 == value.size() ? "" : ", ");
    ss << ")";
    return out << ss.str();
}

std::ostream &operator<<(std::ostream &out, const Firmir::VariantMapT &map)
{
    std::stringstream ss;
    ss << "VariantMap(";
    size_t i = 0;
    for (const auto &[key, value] : map)
        ss << "{\"" << key << "\": " << value << "}" << ((++i) == map.size() ? "" : ", ");
    ss << ")";
    return out << ss.str();
}
