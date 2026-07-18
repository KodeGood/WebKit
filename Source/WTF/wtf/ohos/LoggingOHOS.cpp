/*
 * Copyright (C) 2025 Jani Hautakangas <jani@kodegood.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if OS(OHOS)
#include "Logging.h"

#if !LOG_DISABLED || !RELEASE_LOG_DISABLED

#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

namespace WTF {

String logLevelString()
{
    const char* logEnv = getenv("WEBKIT_DEBUG");

    // Disable all log channels if WEBKIT_DEBUG is unset or empty.
    if (!logEnv || !*logEnv)
        return makeString("-all"_s);

    return String::fromLatin1(logEnv);
}

} // namespace WTF

#endif // !LOG_DISABLED || !RELEASE_LOG_DISABLED

#endif // OS(ANDROID)
