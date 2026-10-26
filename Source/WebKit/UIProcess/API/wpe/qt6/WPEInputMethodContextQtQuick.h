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

#ifndef WPEInputMethodContextQtQuick_h
#define WPEInputMethodContextQtQuick_h

#include <QVariant>
#include <glib-object.h>
#include <wpe/wpe-platform.h>

class QInputMethodEvent;

G_BEGIN_DECLS

#define WPE_TYPE_INPUT_METHOD_CONTEXT_QTQUICK (wpe_input_method_context_qtquick_get_type())
G_DECLARE_FINAL_TYPE (WPEInputMethodContextQtQuick, wpe_input_method_context_qtquick, WPE, INPUT_METHOD_CONTEXT_QTQUICK, WPEInputMethodContext)

WPEInputMethodContext *wpe_input_method_context_qtquick_new (WPEView *view);

G_END_DECLS

// An invalid QVariant for the queries the context does not answer.
QVariant wpe_input_method_context_qtquick_query        (WPEInputMethodContextQtQuick *context, Qt::InputMethodQuery query);
void     wpe_input_method_context_qtquick_handle_event (WPEInputMethodContextQtQuick *context, QInputMethodEvent *event);

#endif /* WPEInputMethodContextQtQuick_h */
