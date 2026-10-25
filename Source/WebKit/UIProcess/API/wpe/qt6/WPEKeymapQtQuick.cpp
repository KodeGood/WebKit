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
#include "WPEKeymapQtQuick.h"

#include <QGuiApplication>
#include <wtf/glib/WTFGType.h>

/**
 * WPEKeymapQtQuick:
 *
 * A #WPEKeymap that reports the keyboard modifiers known to Qt, for platforms without XKB data
 * for the default keymap. It does not translate key codes.
 */
struct _WPEKeymapQtQuickPrivate {
};
WEBKIT_DEFINE_FINAL_TYPE(WPEKeymapQtQuick, wpe_keymap_qtquick, WPE_TYPE_KEYMAP, WPEKeymap)

static gboolean wpeKeymapQtQuickGetEntriesForKeyval(WPEKeymap*, guint, WPEKeymapEntry** entries, guint* entriesCount)
{
    *entries = nullptr;
    *entriesCount = 0;
    return FALSE;
}

static gboolean wpeKeymapQtQuickTranslateKeyboardState(WPEKeymap*, guint, WPEModifiers, int, guint* keyval, int* effectiveGroup, int* level, WPEModifiers* consumedModifiers)
{
    if (keyval)
        *keyval = 0;
    if (effectiveGroup)
        *effectiveGroup = 0;
    if (level)
        *level = 0;
    if (consumedModifiers)
        *consumedModifiers = static_cast<WPEModifiers>(0);
    return FALSE;
}

static WPEModifiers wpeKeymapQtQuickGetModifiers(WPEKeymap*)
{
    uint32_t result = 0;
    auto modifiers = QGuiApplication::queryKeyboardModifiers();
    if (modifiers.testFlag(Qt::ShiftModifier))
        result |= WPE_MODIFIER_KEYBOARD_SHIFT;
    if (modifiers.testFlag(Qt::ControlModifier))
        result |= WPE_MODIFIER_KEYBOARD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))
        result |= WPE_MODIFIER_KEYBOARD_ALT;
    if (modifiers.testFlag(Qt::MetaModifier))
        result |= WPE_MODIFIER_KEYBOARD_META;
    return static_cast<WPEModifiers>(result);
}

static void wpe_keymap_qtquick_class_init(WPEKeymapQtQuickClass* keymapQtQuickClass)
{
    WPEKeymapClass* keymapClass = WPE_KEYMAP_CLASS(keymapQtQuickClass);
    keymapClass->get_entries_for_keyval = wpeKeymapQtQuickGetEntriesForKeyval;
    keymapClass->translate_keyboard_state = wpeKeymapQtQuickTranslateKeyboardState;
    keymapClass->get_modifiers = wpeKeymapQtQuickGetModifiers;
}

WPEKeymap* wpe_keymap_qtquick_new(void)
{
    return WPE_KEYMAP(g_object_new(WPE_TYPE_KEYMAP_QTQUICK, nullptr));
}
