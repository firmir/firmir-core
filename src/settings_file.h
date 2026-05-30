/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "types.h"

namespace Firmir {

struct SettingsFilePrivate;
struct SettingsFile {
    SettingsFile(const std::string &path, const std::string &activeGroup = std::string());
    ~SettingsFile();

    Variant getValue(const std::string &key, const Variant &defaultValue = Variant());
    bool setValue(const std::string &key, const Variant &value);
    bool sync();

private:
    SettingsFilePrivate *pImpl;
};

} // namespace Firmir
