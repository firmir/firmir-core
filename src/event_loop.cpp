/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "event_loop.h"
#include "logging.h"

#include <cstdlib>
#include <memory>

namespace Firmir {

static std::unique_ptr<EventLoop> singleton;

EventLoop::EventLoop()
{
    int r = sd_event_new(&m_handle);
    if (r < 0) {
        Warning() << "Failed to create event loop";
        exit(EXIT_FAILURE);
    }
}

EventLoop &EventLoop::instance()
{
    if (!singleton)
        singleton = std::make_unique<EventLoop>();
    return *singleton;
}

sd_event *EventLoop::getHandle()
{
    return m_handle;
}

int EventLoop::exec()
{
    return sd_event_loop(m_handle);
}

void EventLoop::quit(int code)
{
    sd_event_exit(m_handle, code);
}

} // namespace Firmir
