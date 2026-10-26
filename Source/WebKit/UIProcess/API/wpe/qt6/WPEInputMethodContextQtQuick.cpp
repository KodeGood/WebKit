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
#include "WPEInputMethodContextQtQuick.h"

#include "WPEQtView.h"
#include "WPEViewQtQuick.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <wtf/glib/WTFGType.h>

/**
 * WPEInputMethodContextQtQuick:
 *
 * A #WPEInputMethodContext that connects the editable element focused in a WPEQtView to the
 * input method of Qt. While the context is focused, the WPEQtView item answers the input method
 * queries of Qt and passes its input method events here.
 */
struct _WPEInputMethodContextQtQuickPrivate {
    QString preedit;
    int preeditCursor { 0 };

    QString surroundingText;
    int cursorPosition { 0 };
    int anchorPosition { 0 };

    QRectF cursorRectangle;
};
WEBKIT_DEFINE_FINAL_TYPE(WPEInputMethodContextQtQuick, wpe_input_method_context_qtquick, WPE_TYPE_INPUT_METHOD_CONTEXT, WPEInputMethodContext)

static WPEViewQtQuick* viewQtQuick(WPEInputMethodContextQtQuick* context)
{
    auto* view = wpe_input_method_context_get_view(WPE_INPUT_METHOD_CONTEXT(context));
    return view ? WPE_VIEW_QTQUICK(view) : nullptr;
}

// The Qt input method talks to the item with active focus only. Without it, the item lost the
// focus to another one, which now owns the input method.
static WPEQtView* focusedItem(WPEInputMethodContextQtQuick* context)
{
    auto* view = viewQtQuick(context);
    if (!view || wpe_view_qtquick_get_input_method_context(view) != context)
        return nullptr;

    auto* item = wpe_view_qtquick_get_qt_view(view);
    return item && item->hasActiveFocus() ? item : nullptr;
}

static void updateInputMethod(WPEInputMethodContextQtQuick* context, Qt::InputMethodQueries queries)
{
    if (focusedItem(context))
        QGuiApplication::inputMethod()->update(queries);
}

static bool onScreenKeyboardInhibited(WPEInputMethodContext* context)
{
    return wpe_input_method_context_get_input_hints(context) & WPE_INPUT_HINT_INHIBIT_OSK;
}

static Qt::InputMethodHints inputMethodHints(WPEInputMethodContext* context)
{
    Qt::InputMethodHints qtHints;
    switch (wpe_input_method_context_get_input_purpose(context)) {
    case WPE_INPUT_PURPOSE_DIGITS:
        qtHints |= Qt::ImhDigitsOnly;
        break;
    case WPE_INPUT_PURPOSE_NUMBER:
        qtHints |= Qt::ImhFormattedNumbersOnly;
        break;
    case WPE_INPUT_PURPOSE_PHONE:
        qtHints |= Qt::ImhDialableCharactersOnly;
        break;
    case WPE_INPUT_PURPOSE_URL:
        qtHints |= Qt::ImhUrlCharactersOnly;
        break;
    case WPE_INPUT_PURPOSE_EMAIL:
        qtHints |= Qt::ImhEmailCharactersOnly;
        break;
    case WPE_INPUT_PURPOSE_PASSWORD:
        qtHints |= Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase;
        break;
    case WPE_INPUT_PURPOSE_PIN:
        qtHints |= Qt::ImhDigitsOnly | Qt::ImhHiddenText | Qt::ImhSensitiveData;
        break;
    default:
        break;
    }

    auto hints = wpe_input_method_context_get_input_hints(context);
    if (hints & WPE_INPUT_HINT_LOWERCASE)
        qtHints |= Qt::ImhPreferLowercase;
    if (hints & WPE_INPUT_HINT_UPPERCASE_CHARS)
        qtHints |= Qt::ImhPreferUppercase;
    if (hints & WPE_INPUT_HINT_PRIVATE)
        qtHints |= Qt::ImhSensitiveData;
    return qtHints;
}

static void inputPurposeOrHintsChanged(WPEInputMethodContextQtQuick* context)
{
    updateInputMethod(context, Qt::ImEnabled | Qt::ImHints | Qt::ImEnterKeyType);
}

static void setPreedit(WPEInputMethodContextQtQuick* context, const QString& preedit, int cursor)
{
    auto* priv = context->priv;
    if (preedit == priv->preedit && cursor == priv->preeditCursor)
        return;

    bool hadPreedit = !priv->preedit.isEmpty();
    priv->preedit = preedit;
    priv->preeditCursor = cursor;

    if (!hadPreedit && !preedit.isEmpty())
        g_signal_emit_by_name(context, "preedit-started");
    g_signal_emit_by_name(context, "preedit-changed");
    if (hadPreedit && preedit.isEmpty())
        g_signal_emit_by_name(context, "preedit-finished");
}

static void wpeInputMethodContextQtQuickGetPreeditString(WPEInputMethodContext* context, char** text, GList** underlines, guint* cursorOffset)
{
    auto* priv = WPE_INPUT_METHOD_CONTEXT_QTQUICK(context)->priv;
    if (text)
        *text = g_strdup(priv->preedit.toUtf8().constData());
    if (underlines) {
        *underlines = nullptr;
        if (!priv->preedit.isEmpty())
            *underlines = g_list_prepend(*underlines, wpe_input_method_underline_new(0, priv->preedit.length()));
    }
    if (cursorOffset)
        *cursorOffset = priv->preeditCursor;
}

static gboolean wpeInputMethodContextQtQuickFilterKeyEvent(WPEInputMethodContext*, WPEEvent*)
{
    // Qt delivers input method text in QInputMethodEvents, not through key events.
    return FALSE;
}

static void wpeInputMethodContextQtQuickFocusIn(WPEInputMethodContext* context)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_QTQUICK(context);
    auto* view = viewQtQuick(self);
    auto* item = view ? wpe_view_qtquick_get_qt_view(view) : nullptr;
    if (!item)
        return;

    wpe_view_qtquick_set_input_method_context(view, self);
    item->setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    if (!item->hasActiveFocus())
        return;

    // Qt's OHOS input context shows its keyboard on updates other than Qt::ImQueryAll.
    QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImHints | Qt::ImEnterKeyType | Qt::ImQueryInput);
    if (!onScreenKeyboardInhibited(context))
        QGuiApplication::inputMethod()->show();
}

static void wpeInputMethodContextQtQuickFocusOut(WPEInputMethodContext* context)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_QTQUICK(context);
    auto* view = viewQtQuick(self);
    if (!view || wpe_view_qtquick_get_input_method_context(view) != self)
        return;

    auto* item = focusedItem(self);
    wpe_view_qtquick_set_input_method_context(view, nullptr);
    if (auto* qtView = wpe_view_qtquick_get_qt_view(view))
        qtView->setFlag(QQuickItem::ItemAcceptsInputMethod, false);

    self->priv->preedit = { };
    self->priv->preeditCursor = 0;

    // The element lost the focus within the page: there is nothing left to type into.
    if (item) {
        QGuiApplication::inputMethod()->update(Qt::ImEnabled);
        QGuiApplication::inputMethod()->hide();
    }
}

static void wpeInputMethodContextQtQuickSetCursorArea(WPEInputMethodContext* context, int x, int y, int width, int height)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_QTQUICK(context);
    // In view coordinates, which are those of the item.
    self->priv->cursorRectangle = QRectF(x, y, width, height);
    updateInputMethod(self, Qt::ImCursorRectangle | Qt::ImAnchorRectangle);
}

static void wpeInputMethodContextQtQuickSetSurrounding(WPEInputMethodContext* context, const char* text, guint length, guint cursorIndex, guint selectionIndex)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_QTQUICK(context);
    auto* priv = self->priv;

    // WebKit reports UTF-8 byte offsets, Qt expects UTF-16 positions.
    priv->surroundingText = QString::fromUtf8(text, length);
    priv->cursorPosition = QString::fromUtf8(text, std::min(cursorIndex, length)).length();
    priv->anchorPosition = QString::fromUtf8(text, std::min(selectionIndex, length)).length();
    updateInputMethod(self, Qt::ImSurroundingText | Qt::ImCursorPosition | Qt::ImAnchorPosition | Qt::ImCurrentSelection
        | Qt::ImTextBeforeCursor | Qt::ImTextAfterCursor);
}

static void wpeInputMethodContextQtQuickReset(WPEInputMethodContext* context)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_QTQUICK(context);
    setPreedit(self, { }, 0);
    if (focusedItem(self))
        QGuiApplication::inputMethod()->reset();
}

static void wpeInputMethodContextQtQuickConstructed(GObject* object)
{
    G_OBJECT_CLASS(wpe_input_method_context_qtquick_parent_class)->constructed(object);

    g_signal_connect(object, "notify::input-purpose", G_CALLBACK(inputPurposeOrHintsChanged), nullptr);
    g_signal_connect(object, "notify::input-hints", G_CALLBACK(inputPurposeOrHintsChanged), nullptr);
}

static void wpeInputMethodContextQtQuickDispose(GObject* object)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_QTQUICK(object);
    if (auto* view = viewQtQuick(self)) {
        if (wpe_view_qtquick_get_input_method_context(view) == self)
            wpe_view_qtquick_set_input_method_context(view, nullptr);
    }

    G_OBJECT_CLASS(wpe_input_method_context_qtquick_parent_class)->dispose(object);
}

static void wpe_input_method_context_qtquick_class_init(WPEInputMethodContextQtQuickClass* klass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(klass);
    objectClass->constructed = wpeInputMethodContextQtQuickConstructed;
    objectClass->dispose = wpeInputMethodContextQtQuickDispose;

    WPEInputMethodContextClass* imClass = WPE_INPUT_METHOD_CONTEXT_CLASS(klass);
    imClass->get_preedit_string = wpeInputMethodContextQtQuickGetPreeditString;
    imClass->filter_key_event = wpeInputMethodContextQtQuickFilterKeyEvent;
    imClass->focus_in = wpeInputMethodContextQtQuickFocusIn;
    imClass->focus_out = wpeInputMethodContextQtQuickFocusOut;
    imClass->set_cursor_area = wpeInputMethodContextQtQuickSetCursorArea;
    imClass->set_surrounding = wpeInputMethodContextQtQuickSetSurrounding;
    imClass->reset = wpeInputMethodContextQtQuickReset;
}

WPEInputMethodContext* wpe_input_method_context_qtquick_new(WPEView* view)
{
    g_return_val_if_fail(WPE_IS_VIEW_QTQUICK(view), nullptr);
    return WPE_INPUT_METHOD_CONTEXT(g_object_new(WPE_TYPE_INPUT_METHOD_CONTEXT_QTQUICK, "view", view, nullptr));
}

QVariant wpe_input_method_context_qtquick_query(WPEInputMethodContextQtQuick* context, Qt::InputMethodQuery query)
{
    auto* imContext = WPE_INPUT_METHOD_CONTEXT(context);
    auto* priv = context->priv;
    switch (query) {
    case Qt::ImEnabled:
        return !onScreenKeyboardInhibited(imContext);
    case Qt::ImHints:
        return static_cast<int>(inputMethodHints(imContext));
    case Qt::ImEnterKeyType:
        return static_cast<int>(wpe_input_method_context_get_input_purpose(imContext) == WPE_INPUT_PURPOSE_SEARCH ? Qt::EnterKeySearch : Qt::EnterKeyDefault);
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle:
        return priv->cursorRectangle;
    case Qt::ImSurroundingText:
        return priv->surroundingText;
    case Qt::ImCursorPosition:
        return priv->cursorPosition;
    case Qt::ImAnchorPosition:
        return priv->anchorPosition;
    case Qt::ImCurrentSelection:
        return priv->surroundingText.mid(std::min(priv->cursorPosition, priv->anchorPosition), std::abs(priv->cursorPosition - priv->anchorPosition));
    case Qt::ImTextBeforeCursor:
        return priv->surroundingText.left(priv->cursorPosition);
    case Qt::ImTextAfterCursor:
        return priv->surroundingText.mid(priv->cursorPosition);
    default:
        return { };
    }
}

void wpe_input_method_context_qtquick_handle_event(WPEInputMethodContextQtQuick* context, QInputMethodEvent* event)
{
    if (event->replacementLength())
        g_signal_emit_by_name(context, "delete-surrounding", event->replacementStart(), static_cast<guint>(event->replacementLength()));

    if (!event->commitString().isEmpty()) {
        // The commit replaces the composition, then a new one may start.
        bool hadPreedit = !context->priv->preedit.isEmpty();
        context->priv->preedit = { };
        context->priv->preeditCursor = 0;
        g_signal_emit_by_name(context, "committed", event->commitString().toUtf8().constData());
        if (hadPreedit)
            g_signal_emit_by_name(context, "preedit-finished");
    }

    auto preedit = event->preeditString();
    int cursor = preedit.length();
    for (const auto& attribute : event->attributes()) {
        if (attribute.type == QInputMethodEvent::Cursor)
            cursor = std::clamp(attribute.start, 0, static_cast<int>(preedit.length()));
    }
    setPreedit(context, preedit, cursor);
}
