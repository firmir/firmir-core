/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "fm_visibility.h"

namespace Firmir {

struct ObjectPrivate;
struct FIRMIR_EXPORT_SYMBOL Object {
    Object(Object *parent = nullptr);
    virtual ~Object();

private:
    friend class ObjectPrivate;
    ObjectPrivate &m_impl;
};

} // namespace Firmir
