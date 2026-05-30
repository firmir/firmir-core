/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "types.h"

#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <variant>

namespace Firmir {

struct Object
{
    const int AllEvents = -1;
    using Args = std::vector<Variant>;
    using HandleCallback = std::function<void (const Args &)>;
    using HandleVoidCallback = std::function<void ()>;
    using UniversalCallback = std::variant<HandleCallback, HandleVoidCallback>;

    ~Object();
    
    void connectTo(int event, Object &handler, UniversalCallback callback);

    template <typename EventT>
    void connect(EventT event, Object &handler, UniversalCallback callback)
    {
        connectTo(static_cast<int>(event), handler, callback);
    }

    template <typename EventT>
    void emitEvent(EventT event, const Args &args = Args())
    {
        emitEvent(static_cast<int>(event), args);
    }

    void emitEvent(int event, const Args &args = Args());

private:
    using HandleCallbacks = std::vector<UniversalCallback>;

    std::unordered_map<int, std::unordered_map<Object *, HandleCallbacks>> m_subscribers;
    std::unordered_set<Object *> m_subscribtions;
};

} // namespace Firmir
