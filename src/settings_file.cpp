/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "settings_file.h"
#include "logging.h"

#include <gio/gio.h>

namespace Firmir {

struct SettingsFilePrivate
{
    SettingsFilePrivate(const std::string &path, const std::string &activeGroup);
    ~SettingsFilePrivate();

    std::string path;
    std::string activeGroup;
    GKeyFile *file;
};

SettingsFilePrivate::SettingsFilePrivate(const std::string &path, const std::string &activeGroup)
    : path(path)
    , activeGroup(activeGroup)
    , file(g_key_file_new())
{
    GError *error = nullptr;
    if (!g_key_file_load_from_file(file, path.c_str(), G_KEY_FILE_NONE, &error)) {
        Error() << "Failed to read data from file:" << error->message;
        g_error_free(error);
    }
}

SettingsFilePrivate::~SettingsFilePrivate()
{
    g_key_file_free(file);
}

SettingsFile::SettingsFile(const std::string &path, const std::string &activeGroup)
    : pImpl(new SettingsFilePrivate(path, activeGroup))
{
}

SettingsFile::~SettingsFile()
{
    delete pImpl;
}

Variant SettingsFile::getValue(const std::string &key, const Variant &defaultValue)
{
    GError* error = nullptr;
    auto *file = pImpl->file;
    const auto *group = pImpl->activeGroup.c_str();
    gint intValue = g_key_file_get_integer(file, group, key.c_str(), &error);
    if (!error)
        return Variant(static_cast<int64_t>(intValue));
    g_clear_error(&error);

    gboolean boolValue = g_key_file_get_boolean(file, group, key.c_str(), &error);
    if (!error)
        return Variant(static_cast<bool>(boolValue));
    g_clear_error(&error);

    gsize length = 0;
    gchar** strListValue = g_key_file_get_string_list(file, group, key.c_str(), &length, &error);
    if (!error) {
        StringListT res;
        res.reserve(length);
        for (gsize i = 0; i < length; ++i)
            res.push_back(strListValue[i]);
        g_strfreev(strListValue);
        return Variant(res);
    }
    g_clear_error(&error);

    gchar* strValue = g_key_file_get_string(file, group, key.c_str(), &error);
    if (!error) {
        Variant res(std::string{strValue});
        g_free(strValue);
        return res;
    }
    g_clear_error(&error);

    return defaultValue;
}

bool SettingsFile::setValue(const std::string &key, const Variant &value)
{
    bool res = true;
    auto *file = pImpl->file;
    const auto *group = pImpl->activeGroup.c_str();
    switch (value.dataType()) {
    case DataType::Null:
        res = g_key_file_remove_key(file, group, key.c_str(), nullptr);
        break;
    case DataType::Bool:
        g_key_file_set_boolean(file, group, key.c_str(), value.toBool());
        break;
    case DataType::Int:
        g_key_file_set_int64(file, group, key.c_str(), value.toInt());
        break;
    case DataType::String:
        g_key_file_set_string(file, group, key.c_str(), value.toString().c_str());
        break;
    case DataType::StringList: {
        auto strList = value.toStringList();
        std::vector<const gchar *> rawRes;
        rawRes.reserve(strList.size() + 1);
        for (size_t i = 0; i < strList.size(); ++i)
            rawRes.push_back(strList[i].c_str());
        rawRes.push_back(nullptr);
        g_key_file_set_string_list(file, group, key.c_str(), rawRes.data(), strList.size());
        break;
    }
    default:
        Warning() << __func__ << "Unsupported data type:" << static_cast<int>(value.dataType());
        res = false;
        break;
    }

    return res;
}

bool SettingsFile::sync()
{
    GError* error = nullptr;
    g_key_file_save_to_file(pImpl->file, pImpl->path.c_str(), &error);
    if (!error)
        return true;

    Warning() << __func__ << "Failed to save changes to file:" << error->message;
    g_clear_error(&error);
    return false;
}

} // namespace Firmir
