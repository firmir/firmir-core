/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "fm_object.h"

#include <unordered_set>

namespace Firmir {

struct FIRMIR_EXPORT_SYMBOL ObjectPrivate {
    ObjectPrivate(Object &current, Object *parent);
    virtual ~ObjectPrivate();

    Object &current;
    Object *parent;
    std::unordered_set<Object *> children;
};

} // namespace Firmir
