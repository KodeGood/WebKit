/*
 * Copyright (C) 2026 Jani Hautakangas <jani@kodegood.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WPEQtGLibEventLoop.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <utility>

void WPEQtGLibEventLoop::ensureIntegrated()
{
    static WPEQtGLibEventLoop* s_eventLoop;
    if (s_eventLoop)
        return;

    auto* dispatcher = QAbstractEventDispatcher::instance();
    if (!dispatcher || dispatcher->inherits("QEventDispatcherGlib"))
        return;

    s_eventLoop = new WPEQtGLibEventLoop(dispatcher);
    QObject::connect(QCoreApplication::instance(), &QObject::destroyed, [] {
        delete std::exchange(s_eventLoop, nullptr);
    });
}

WPEQtGLibEventLoop::WPEQtGLibEventLoop(QAbstractEventDispatcher* dispatcher)
    : m_context(g_main_context_ref(g_main_context_default()))
{
    if (!g_main_context_acquire(m_context))
        qWarning("WPEQtGLibEventLoop: the default GMainContext is owned by another thread, WebKit will not run");

    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &WPEQtGLibEventLoop::iterate);

    // GLib signals its wakeup fd only for sources attached from other threads, so sources attached
    // while Qt handles events on this thread are picked up by iterating before Qt blocks. Arming the
    // notifiers and the timer here is in time: Qt builds its poll set and deadline afterwards.
    connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, this, &WPEQtGLibEventLoop::iterate);

    prepare();
}

WPEQtGLibEventLoop::~WPEQtGLibEventLoop()
{
    m_notifiers.clear();
    g_main_context_release(m_context);
    g_main_context_unref(m_context);
}

void WPEQtGLibEventLoop::prepare()
{
    gboolean ready = g_main_context_prepare(m_context, &m_maxPriority);

    gint timeout = -1;
    if (m_pollFDs.empty())
        m_pollFDs.resize(1);
    gint count;
    while ((count = g_main_context_query(m_context, m_maxPriority, &timeout, m_pollFDs.data(), m_pollFDs.size())) > static_cast<gint>(m_pollFDs.size()))
        m_pollFDs.resize(count);
    m_pollFDs.resize(count);
    m_isPrepared = true;

    // With a source ready GLib leaves out the fds of lower priority sources, they are not gone.
    updateNotifiers(!ready && timeout);

    if (ready)
        timeout = 0;
    if (timeout >= 0)
        m_timer.start(timeout);
    else
        m_timer.stop();
}

void WPEQtGLibEventLoop::iterate()
{
    if (m_isPrepared) {
        m_isPrepared = false;
        for (auto& pollFD : m_pollFDs)
            pollFD.revents = 0;
        g_poll(m_pollFDs.data(), m_pollFDs.size(), 0);
        if (g_main_context_check(m_context, m_maxPriority, m_pollFDs.data(), m_pollFDs.size()))
            g_main_context_dispatch(m_context);
    }

    // A nested iteration in the dispatch above already prepared again; doing it once more is harmless.
    prepare();
}

void WPEQtGLibEventLoop::NotifierDeleter::operator()(QSocketNotifier* notifier) const
{
    notifier->setEnabled(false);
    notifier->deleteLater();
}

void WPEQtGLibEventLoop::updateNotifiers(bool pollSetIsComplete)
{
    std::unordered_map<int, gushort> events;
    for (const auto& pollFD : m_pollFDs)
        events[pollFD.fd] |= pollFD.events;

    if (pollSetIsComplete)
        std::erase_if(m_notifiers, [&](const auto& entry) { return !events.contains(entry.first); });

    auto update = [this](NotifierPtr& notifier, int fd, QSocketNotifier::Type type, bool wanted) {
        if (!wanted) {
            notifier = nullptr;
            return;
        }
        if (notifier)
            return;
        notifier.reset(new QSocketNotifier(fd, type));
        connect(notifier.get(), &QSocketNotifier::activated, this, &WPEQtGLibEventLoop::iterate);
    };

    for (auto [fd, fdEvents] : events) {
        auto& notifiers = m_notifiers[fd];
        // POLLHUP and POLLERR are reported with any of the requested events.
        update(notifiers.read, fd, QSocketNotifier::Read, fdEvents & (G_IO_IN | G_IO_HUP | G_IO_ERR));
        update(notifiers.write, fd, QSocketNotifier::Write, fdEvents & G_IO_OUT);
        update(notifiers.exception, fd, QSocketNotifier::Exception, fdEvents & G_IO_PRI);
    }
}

#include "moc_WPEQtGLibEventLoop.cpp"
