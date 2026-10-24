include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/vcpkg_make.cmake")

function(vcpkg_make_configure)
    cmake_parse_arguments(PARSE_ARGV 0 arg
        "AUTORECONF;COPY_SOURCE;DISABLE_MSVC_WRAPPERS;DISABLE_CPPFLAGS;DISABLE_DEFAULT_OPTIONS;DISABLE_MSVC_TRANSFORMATIONS"
        "SOURCE_PATH;DEFAULT_OPTIONS_EXCLUDE"
        "OPTIONS;OPTIONS_DEBUG;OPTIONS_RELEASE;PRE_CONFIGURE_CMAKE_COMMANDS;LANGUAGES"
    )

    z_vcpkg_unparsed_args(FATAL_ERROR)

    if(arg_DISABLE_DEFAULT_OPTIONS AND arg_DEFAULT_OPTIONS_EXCLUDE)
        message(FATAL_ERROR "DISABLE_DEFAULT_OPTIONS cannot be used together with DEFAULT_OPTIONS_EXCLUDE.")
    endif()

    # Can be set in the triplet to append options for configure
    if(DEFINED VCPKG_MAKE_CONFIGURE_OPTIONS)
        list(APPEND arg_OPTIONS ${VCPKG_MAKE_CONFIGURE_OPTIONS})
    endif()
    if(DEFINED VCPKG_MAKE_CONFIGURE_OPTIONS_RELEASE)
        list(APPEND arg_OPTIONS_RELEASE ${VCPKG_MAKE_CONFIGURE_OPTIONS_RELEASE})
    endif()
    if(DEFINED VCPKG_MAKE_CONFIGURE_OPTIONS_DEBUG)
        list(APPEND arg_OPTIONS_DEBUG ${VCPKG_MAKE_CONFIGURE_OPTIONS_DEBUG})
    endif()

    set(src_dir "${arg_SOURCE_PATH}")

    # OHOS overlay: overwrite every bundled config.sub/config.guess in this port's extracted
    # source with the recent copies shipped by the vcpkg-make port (they recognise the `ohos`
    # OS), so cross configure with --host=aarch64-unknown-linux-ohos is accepted. We scan the
    # whole per-port buildtree (CURRENT_BUILDTREES_DIR/src) rather than just src_dir, because some
    # ports (e.g. gettext-libintl) configure a subdir whose config.sub lives above SOURCE_PATH.
    # Scoped to this port's buildtree, so it never touches unrelated sources. No-op if absent.
    set(_ohos_gnuconfig "${CURRENT_HOST_INSTALLED_DIR}/share/vcpkg-make/gnuconfig")
    set(_ohos_scan_root "${src_dir}")
    if(DEFINED CURRENT_BUILDTREES_DIR AND EXISTS "${CURRENT_BUILDTREES_DIR}/src")
        set(_ohos_scan_root "${CURRENT_BUILDTREES_DIR}/src")
    endif()
    if(EXISTS "${_ohos_gnuconfig}/config.sub")
        file(GLOB_RECURSE _ohos_config_subs "${_ohos_scan_root}/config.sub")
        foreach(_cs IN LISTS _ohos_config_subs)
            get_filename_component(_cs_dir "${_cs}" DIRECTORY)
            file(COPY "${_ohos_gnuconfig}/config.sub" DESTINATION "${_cs_dir}")
        endforeach()
        file(GLOB_RECURSE _ohos_config_guesses "${_ohos_scan_root}/config.guess")
        foreach(_cg IN LISTS _ohos_config_guesses)
            get_filename_component(_cg_dir "${_cg}" DIRECTORY)
            file(COPY "${_ohos_gnuconfig}/config.guess" DESTINATION "${_cg_dir}")
        endforeach()
    endif()

    z_vcpkg_warn_path_with_spaces()

    set(prepare_flags_opts "")
    if(arg_DISABLE_MSVC_WRAPPERS)
        list(APPEND prepare_flags_opts "DISABLE_MSVC_WRAPPERS")        
    endif()
    if(arg_DISABLE_CPPFLAGS)
        list(APPEND prepare_flags_opts "DISABLE_CPPFLAGS")
    endif()
    if(DEFINED arg_LANGUAGES)
        list(APPEND prepare_flags_opts "LANGUAGES" ${arg_LANGUAGES})
    endif()

    # Cache this invocation's desired cmake vars configuration.
    set(Z_VCPKG_MAKE_GET_CMAKE_VARS_OPTS "ADDITIONAL_LANGUAGES;${arg_LANGUAGES}" CACHE INTERNAL "")
    z_vcpkg_make_get_cmake_vars()

    set(escaping "")
    if(arg_DISABLE_MSVC_TRANSFORMATIONS)
      set(escaping NO_FLAG_ESCAPING)
    endif()

    z_vcpkg_set_global_property(make_prepare_flags_opts "${prepare_flags_opts}")
    z_vcpkg_make_prepare_flags(${prepare_flags_opts} ${escaping} C_COMPILER_NAME ccname FRONTEND_VARIANT_OUT frontend)

    z_vcpkg_make_get_configure_triplets(BUILD_TRIPLET COMPILER_NAME "${ccname}")

    if(NOT arg_DISABLE_MSVC_WRAPPERS AND "${frontend}" STREQUAL "MSVC" )
        # Lets assume that wrappers are only required for MSVC like frontends.
        vcpkg_add_to_path(PREPEND "${CURRENT_HOST_INSTALLED_DIR}/share/vcpkg-make/wrappers")
    endif()

    vcpkg_make_get_shell(shell_var)
    set(shell_cmd "${shell_var}")

    if(arg_AUTORECONF)
      vcpkg_run_autoreconf("${shell_cmd}" "${src_dir}")
    endif()

    # Backup environment variables
    set(cm_FLAGS AR AS CC C CCAS CPP CXX FC FF GC LD LF LIBTOOL OBJC OBJXX R RC UPC Y)

    list(TRANSFORM cm_FLAGS APPEND "FLAGS")
    vcpkg_backup_env_variables(VARS 
        ${cm_FLAGS}
    # General backup
        PATH
    # Used by gcc/linux
        C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH LD_LIBRARY_PATH
    # Used by cl
        INCLUDE LIB LIBPATH _CL_ _LINK_
    # Used by emscripten
        EMMAKEN_JUST_CONFIGURE
    )
    z_vcpkg_make_set_common_vars()
    if(VCPKG_TARGET_IS_EMSCRIPTEN)
        set(EMMAKEN_JUST_CONFIGURE 1) # as in emconfigure
    endif()

    foreach(config IN LISTS buildtypes)
        string(TOUPPER "${config}" configup)
        set(target_dir "${workdir_${configup}}")
        file(REMOVE_RECURSE "${target_dir}")
        file(MAKE_DIRECTORY "${target_dir}")
        file(RELATIVE_PATH relative_build_path "${target_dir}" "${src_dir}")
        if(arg_COPY_SOURCE)
            file(COPY "${src_dir}/" DESTINATION "${target_dir}")
            set(relative_build_path ".")
        endif()

        z_vcpkg_make_prepare_programs(configure_env ${prepare_flags_opts} CONFIG "${configup}" BUILD_TRIPLET "${BUILD_TRIPLET}")

        set(opts "")
        if(NOT arg_DISABLE_DEFAULT_OPTIONS)
            z_vcpkg_make_default_path_and_configure_options(opts CONFIG "${configup}"
                EXCLUDE_FILTER "${arg_DEFAULT_OPTIONS_EXCLUDE}"
            )
        endif()

        set(configure_path_from_wd "./${relative_build_path}/configure")

        foreach(cmd IN LISTS arg_PRE_CONFIGURE_CMAKE_COMMANDS)
            cmake_language(CALL ${cmd} ${configup})
        endforeach()

        vcpkg_make_run_configure(SHELL
                                    "${shell_cmd}"
                                 CONFIG
                                    "${configup}"
                                 CONFIGURE_ENV
                                    "${configure_env}"
                                 CONFIGURE_PATH
                                    "${configure_path_from_wd}"
                                 OPTIONS 
                                    ${BUILD_TRIPLET}
                                    ${arg_OPTIONS}
                                    ${opts}
                                    ${arg_OPTIONS_${configup}}
                                 WORKING_DIRECTORY
                                    "${target_dir}"
                                 ${extra_configure_opts}
                                )

        # OHOS overlay: OpenHarmony (like Android) does not support versioned sonames, and the
        # OHOS HAP packager only accepts plain lib*.so files. The libtool macros bundled in
        # autotools tarballs have no OHOS case, so configure falls into the generic linux* branch
        # and bakes versioned specs (libfoo.so.MAJOR + symlinks) into the generated `libtool`.
        if(VCPKG_TARGET_IS_OHOS)
            file(GLOB_RECURSE _ohos_libtool_scripts "${target_dir}/libtool")
            foreach(_lt IN LISTS _ohos_libtool_scripts)
                file(READ "${_lt}" _lt_contents)
                string(REPLACE
                    "library_names_spec=\"\\$libname\\$release\\$shared_ext\\$versuffix \\$libname\\$release\\$shared_ext\\$major \\$libname\\$shared_ext\""
                    "library_names_spec=\"\\$libname\\$release\\$shared_ext\""
                    _lt_contents "${_lt_contents}")
                string(REPLACE
                    "soname_spec=\"\\$libname\\$release\\$shared_ext\\$major\""
                    "soname_spec=\"\\$libname\\$release\\$shared_ext\""
                    _lt_contents "${_lt_contents}")
                string(REPLACE "\nversion_type=linux\n" "\nversion_type=none\n" _lt_contents "${_lt_contents}")
                file(WRITE "${_lt}" "${_lt_contents}")
            endforeach()
        endif()
    endforeach()

    # Restore environment
    vcpkg_restore_env_variables(VARS 
        ${cm_FLAGS} 
        C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH LD_LIBRARY_PATH
        INCLUDE LIB LIBPATH _CL_ _LINK_
        EMMAKEN_JUST_CONFIGURE
    )

    if(VCPKG_HOST_IS_BSD)
        find_program(Z_VCPKG_MAKE gmake REQUIRED)
    else()
        find_program(Z_VCPKG_MAKE NAMES make gmake NAMES_PER_DIR REQUIRED)
    endif()
endfunction()
