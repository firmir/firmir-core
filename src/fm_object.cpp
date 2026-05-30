/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "fm_object_p.h"

namespace Firmir {

ObjectPrivate::ObjectPrivate(Object &current, Object *parent)
    : current(current)
    , parent(parent)
{
    if (parent)
        parent->m_impl.children.insert(&current);
}

ObjectPrivate::~ObjectPrivate()
{
    auto toRemove = std::move(children);
    for (auto *child : toRemove)
        delete child;
}

Object::Object(Object *parent)
    : m_impl(*new ObjectPrivate(*this, parent))
{
}

Object::~Object()
{
    delete &m_impl;
}

} // namespace Firmir
