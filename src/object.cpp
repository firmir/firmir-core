/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "object.h"
#include "logging.h"

namespace Firmir {

Object::~Object()
{
    for (auto *objectIt : m_subscribtions) {
        for (auto &[event, handlers] : objectIt->m_subscribers)
            handlers.erase(this);
    }
}

void Object::connectTo(int event, Object &handler, UniversalCallback callback)
{
    m_subscribers[event][&handler].push_back(callback);
    handler.m_subscribtions.insert(this);
}

void Object::emitEvent(int event, const Args &args)
{
    auto eventIt = m_subscribers.find(event);
    if (eventIt == m_subscribers.end())
        return;

    // To correct produce all events if this object will be removed in one of the callbacks
    // we copy all callbacks before calling them
    HandleCallbacks allCallbacks;
    for (auto &[_, callbacks] : eventIt->second)
        allCallbacks.insert(allCallbacks.end(), callbacks.begin(), callbacks.end());
    for (auto callback : allCallbacks) {
        if (std::holds_alternative<HandleCallback>(callback))
            std::get<HandleCallback>(callback)(args);
        else if (std::holds_alternative<HandleVoidCallback>(callback))
            std::get<HandleVoidCallback>(callback)();
        else
            Warning() << "Emit event with empty callback function";
    }
}

} // namespace Firmir
