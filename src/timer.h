/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "object.h"

#include <systemd/sd-event.h>

namespace Firmir {

struct Timer : public Object
{
    enum class Event { Timeout };

    ~Timer();

    void setInterval(uint32_t ms);
    void setSingleShot(bool single);
    void start();
    void stop();
    bool isActive() const;
    static void singleShot(uint32_t ms, Object &handler, HandleVoidCallback callback);

private:
    static int staticCallback(sd_event_source *source, uint64_t usec, void *userdata);
    void handleTimeout(uint64_t nowUs);

    sd_event_source *m_source = nullptr;
    uint32_t m_intervalMs = 1000;
    bool m_singleShot = true;
    bool m_active = false;
};

} // namespace Firmir
