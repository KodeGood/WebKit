
if (APPLE)
    if (USE_APPLE_INTERNAL_SDK)
        set(ICU_INCLUDE_DIRS ${CMAKE_OSX_SYSROOT}/usr/local/include)
    else ()
        set(ICU_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/ICU/Headers)
    endif ()
    # Apple just has a single tbd/dylib for ICU.
    find_library(ICU_I18N_LIBRARY icucore)
    find_library(ICU_UC_LIBRARY icucore)
    find_library(ICU_DATA_LIBRARY icucore)

    add_library(ICU::data UNKNOWN IMPORTED)
    set_target_properties(ICU::data PROPERTIES
        IMPORTED_LOCATION "${ICU_DATA_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIRS}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    )

    add_library(ICU::i18n UNKNOWN IMPORTED)
    set_target_properties(ICU::i18n PROPERTIES
        IMPORTED_LOCATION "${ICU_I18N_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIRS}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    )

    add_library(ICU::uc UNKNOWN IMPORTED)
    set_target_properties(ICU::uc PROPERTIES
        IMPORTED_LOCATION "${ICU_UC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIRS}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
    )

    set(ICU_LIBRARIES ${ICU_UC_LIBRARY})
    set(ICU_FOUND ON)
    message(STATUS "Found ICU: ${ICU_LIBRARIES}")
else ()
    # Defer to CMake's built-in FindICU. Include it rather than calling
    # find_package() again: a nested find_package() runs package-manager
    # wrappers (e.g. vcpkg's ICU wrapper) twice, and the inner run clobbers
    # the outer one's fixups, leaving ICU::uc without an IMPORTED_LOCATION.
    include(${CMAKE_ROOT}/Modules/FindICU.cmake)
endif ()
