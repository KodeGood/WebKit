# Copyright (C) 2025 Jani Hautakangas <jani@kodegood.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND ITS CONTRIBUTORS ``AS
# IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR ITS
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#[=======================================================================[.rst:
FindOHOS
-----------

Find OHOS SDK headers and libraries.

This module supports checking for the following components:

``NativeBuffer``
  The OH_NativeBuffer ``libnative_buffer`` library.
``NativeWindow``
  The OH_NativeWindow_* ``libnative_window`` library.
``Log``
  The logging ``libhilog_ndk.z`` library.
``IPC``
  The OpenHarmony binder IPCKit ``libipc_capi`` library.
``ChildProcess``
  The AbilityKit native child process ``libchild_process`` library.
``AbilityRuntime``
  The AbilityKit application-context ``libability_runtime`` library.

Imported Targets
^^^^^^^^^^^^^^^^

For each supported component, the corresponding `OHOS::<name>` target
will be defined, for example `OHOS::NativeBuffer`, if found.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables in your project, for each
component given its `<name>`:

``OHOS_<name>_FOUND``
  True if the component `<name>` is available.

#]=======================================================================]

if (NOT OHOS)
    return()
endif ()

include(CMakePushCheckState)
include(CheckFunctionExists)
include(CheckIncludeFileCXX)

function(_OHOSHandleComponent target)
    if (TARGET OHOS::${target})
        message(DEBUG "OHOS::${target} already checked, skipping")
        return()
    endif ()

    if (NOT OHOS_COMPONENT_${target}_LIBRARY)
        message(CHECK_FAIL "Invalid component name")
        return()
    endif ()

    set(libname "${OHOS_COMPONENT_${target}_LIBRARY}")
    set(header "${OHOS_COMPONENT_${target}_HEADER}")
    set(symbol "${OHOS_COMPONENT_${target}_SYMBOL}")
    set(deps "${OHOS_COMPONENT_${target}_DEPS}")
    set(deplibs "${libname}")

    foreach (dep IN LISTS deps)
        if (OHOS_FIND_REQUIRED_${target} AND NOT OHOS_FIND_REQUIRED_${dep})
            set(OHOS_FIND_REQUIRED_${dep} TRUE PARENT_SCOPE)
        endif ()
        _OHOSHandleComponent(${dep})
        list(APPEND deplibs OHOS::${dep})
    endforeach ()

    check_include_file_cxx("${header}" OHOS_COMPONENT_${target}_HAS_HEADER)
    if (NOT OHOS_COMPONENT_${target}_HAS_HEADER)
        message(STATUS "Missing header ${header}")
        return()
    endif ()

    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES "${libname}")
    check_function_exists("${symbol}" OHOS_COMPONENT_${target}_HAS_SYMBOL)
    cmake_pop_check_state()
    if (NOT OHOS_COMPONENT_${target}_HAS_SYMBOL)
        message(STATUS "Missing symbol ${symbol}")
        return()
    endif ()

    add_library(OHOS::${target} INTERFACE IMPORTED)
    set_target_properties(OHOS::${target} PROPERTIES
        INTERFACE_LINK_LIBRARIES "${deplibs}")
    set(OHOS_${target}_FOUND TRUE PARENT_SCOPE)
endfunction()

function(_OHOSDefineComponent target libname header symbol)
    if (OHOS_COMPONENT_${target}_LIBRARY)
        message(FATAL_ERROR "OHOS::${target} component already defined")
    endif ()
    cmake_parse_arguments(PARSE_ARGV 4 opt "" "" "")
    set(OHOS_COMPONENT_${target}_LIBRARY "${libname}" PARENT_SCOPE)
    set(OHOS_COMPONENT_${target}_HEADER "${header}" PARENT_SCOPE)
    set(OHOS_COMPONENT_${target}_SYMBOL "${symbol}" PARENT_SCOPE)
    set(OHOS_COMPONENT_${target}_DEPS "${opt_UNPARSED_ARGUMENTS}" PARENT_SCOPE)
endfunction()

_OHOSDefineComponent(NativeBuffer
    native_buffer
    native_buffer/native_buffer.h
    OH_NativeBuffer_Alloc
)
_OHOSDefineComponent(NativeWindow
    native_window
    native_window/external_window.h
    OH_NativeWindow_CreateNativeWindowBufferFromNativeBuffer
)
_OHOSDefineComponent(Log
    hilog_ndk.z
    hilog/log.h
    OH_LOG_Print
)
_OHOSDefineComponent(IPC
    ipc_capi
    IPCKit/ipc_kit.h
    OH_IPCParcel_Create
)
_OHOSDefineComponent(ChildProcess
    child_process
    AbilityKit/native_child_process.h
    OH_Ability_CreateNativeChildProcess
)
_OHOSDefineComponent(AbilityRuntime
    ability_runtime
    AbilityKit/ability_runtime/application_context.h
    OH_AbilityRuntime_ApplicationContextGetCacheDir
)

foreach (component IN LISTS OHOS_FIND_COMPONENTS)
    message(CHECK_START "Checking OHOS::${component}")
    list(APPEND CMAKE_MESSAGE_INDENT "  ")
    _OHOSHandleComponent(${component})
    list(POP_BACK CMAKE_MESSAGE_INDENT)
    if (OHOS_${component}_FOUND)
        message(CHECK_PASS "OK")
    else ()
        message(CHECK_FAIL "Failed")
    endif ()
endforeach ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OHOS HANDLE_COMPONENTS)
