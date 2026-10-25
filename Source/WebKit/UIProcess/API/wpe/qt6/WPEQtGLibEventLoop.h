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

#pragma once

#include <QObject>
#include <QTimer>
#include <glib.h>
#include <memory>
#include <unordered_map>
#include <vector>

class QAbstractEventDispatcher;
class QSocketNotifier;

// Iterates the default GMainContext, where WebKit's main RunLoop lives, from the Qt event loop of
// the main thread when Qt's event dispatcher is not GLib based (e.g. Qt built without GLib, or the
// OpenHarmony QPA, whose dispatcher is a plain poll() loop).
class WPEQtGLibEventLoop final : public QObject {
    Q_OBJECT
public:
    // Must be called on the Qt main thread before the first WebKit call.
    static void ensureIntegrated();

    ~WPEQtGLibEventLoop();

private:
    explicit WPEQtGLibEventLoop(QAbstractEventDispatcher*);

    void iterate();
    void prepare();
    void updateNotifiers(bool pollSetIsComplete);

    // A notifier can go away while it emits activated(), so it is deleted later.
    struct NotifierDeleter {
        void operator()(QSocketNotifier*) const;
    };
    using NotifierPtr = std::unique_ptr<QSocketNotifier, NotifierDeleter>;
    struct Notifiers {
        NotifierPtr read;
        NotifierPtr write;
        NotifierPtr exception;
    };

    GMainContext* m_context { nullptr };
    gint m_maxPriority { 0 };
    std::vector<GPollFD> m_pollFDs;
    std::unordered_map<int, Notifiers> m_notifiers;
    QTimer m_timer;
    bool m_isPrepared { false };
};
