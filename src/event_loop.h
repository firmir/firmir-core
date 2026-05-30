/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <systemd/sd-event.h>

namespace Firmir {

struct EventLoop
{
    EventLoop();

    static EventLoop &instance();
    sd_event *getHandle();
    int exec();
    void quit(int code = 0);

private:
    sd_event *m_handle = nullptr;
};

} // namespace Firmir
