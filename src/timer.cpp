/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "timer.h"
#include "event_loop.h"
#include "logging.h"

namespace Firmir {

Timer::~Timer() {
    stop();
}

void Timer::setInterval(uint32_t ms) {
    m_intervalMs = ms;
}

void Timer::setSingleShot(bool single) {
    m_singleShot = single;
}

void Timer::start() {
    stop();
    if (m_intervalMs == 0) {
        emitEvent(Event::Timeout);
        return;
    }

    uint64_t nowUs;
    auto *handle = EventLoop::instance().getHandle();
    int r = sd_event_now(handle, CLOCK_MONOTONIC, &nowUs);
    if (r < 0)
        return;

    uint64_t expireUs = nowUs + m_intervalMs * 1000;
    uint64_t accuracy = m_intervalMs < 2000 ? 1 : 0;
    r = sd_event_add_time(handle, &m_source, CLOCK_MONOTONIC, expireUs, accuracy, &Timer::staticCallback, this);
    if (r < 0) {
        m_source = nullptr;
        return;
    }

    m_active = true;
    sd_event_source_set_enabled(m_source, SD_EVENT_ONESHOT);
}

void Timer::stop() {
    if (m_source) {
        sd_event_source_unref(m_source);
        m_source = nullptr;
    }

    m_active = false;
}

bool Timer::isActive() const {
    return m_active;
}

void Timer::singleShot(uint32_t ms, Object &handler, HandleVoidCallback callback)
{
    auto timer = new Timer();
    timer->setInterval(ms);
    timer->setSingleShot(true);
    timer->connect(Event::Timeout, *timer, [timer] () { delete timer; });
    timer->connect(Event::Timeout, handler, callback);
    timer->start();
}

int Timer::staticCallback(sd_event_source *source, uint64_t usec, void *userdata) {
    Timer *self = static_cast<Timer*>(userdata);
    self->handleTimeout(usec);
    return 0;
}

void Timer::handleTimeout(uint64_t nowUs) {
    if (!m_active)
        return;

    emitEvent(Event::Timeout);

    if (m_singleShot || !m_active) {
        stop();
        return;
    }

    uint64_t nextUs = nowUs + m_intervalMs * 1000;
    int r = sd_event_source_set_time(m_source, nextUs);
    if (r >= 0)
        sd_event_source_set_enabled(m_source, SD_EVENT_ONESHOT);
    else
        stop();
}

} // namespace Firmir
