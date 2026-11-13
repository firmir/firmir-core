# Detection of the optional system headers the GUI backends need:
# <drm/drm.h> + <drm/drm_mode.h> (drm, eglfs-kms) and <linux/input.h> (evdev).
#
# check_include_file_cxx() is deliberately not used here:
#  * it stores its verdict in CMakeCache.txt, so a header installed after the
#    first configure keeps being reported as missing until that cache entry is
#    deleted — "the header is right there but cmake does not find it";
#  * its probe compiles and links a test executable, which in scratchbox2 and
#    other cross/sysroot setups can fail for reasons that have nothing to do
#    with the header (target libc, sb2 path mapping, try_compile flags),
#    silently disabling a backend whose headers are present.
#
# The headers are looked up on disk instead, so the result is re-evaluated on
# every configure. The usual layouts are covered:
#   <prefix>/include/drm/drm.h         kernel uapi (Ubuntu, Sailfish, ...)
#   <prefix>/include/libdrm/drm/drm.h  libdrm includedir with a drm/ subdir
# pkg-config (libdrm.pc) include dirs are searched first, then the include
# prefixes of the build environment (including CMAKE_SYSROOT for cross
# builds). A caller that needs extra roots can preset
# FIRMIR_HEADER_SEARCH_ROOTS.

# firmir_find_headers(<found_var> <include_dir_var>
#                     HEADERS <header>... [PACKAGES <pkg-config module>...])
#
# Sets <found_var> to TRUE when every header of HEADERS is present in one of the
# searched roots; <include_dir_var> then holds the root to add to the include
# path (empty when the compiler searches it anyway, i.e. /usr/include).
function(firmir_find_headers found_var include_dir_var)
    cmake_parse_arguments(ARG "" "" "HEADERS;PACKAGES" ${ARGN})
    if(NOT ARG_HEADERS)
        message(FATAL_ERROR "firmir_find_headers(): HEADERS is required")
    endif()
    set(_roots "")
    if(ARG_PACKAGES)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            foreach(_package IN LISTS ARG_PACKAGES)
                pkg_check_modules(FIRMIR_PC_${_package} QUIET ${_package})
                if(FIRMIR_PC_${_package}_FOUND)
                    list(APPEND _roots ${FIRMIR_PC_${_package}_INCLUDE_DIRS})
                endif()
            endforeach()
        endif()
    endif()
    foreach(_prefix /usr /usr/local ${CMAKE_SYSROOT}/usr)
        list(APPEND _roots ${_prefix}/include ${_prefix}/include/libdrm)
        if(CMAKE_LIBRARY_ARCHITECTURE)
            list(APPEND _roots ${_prefix}/include/${CMAKE_LIBRARY_ARCHITECTURE})
        endif()
    endforeach()
    if(DEFINED FIRMIR_HEADER_SEARCH_ROOTS)
        list(APPEND _roots ${FIRMIR_HEADER_SEARCH_ROOTS})
    endif()
    list(REMOVE_DUPLICATES _roots)

    set(${found_var} FALSE PARENT_SCOPE)
    set(${include_dir_var} "" PARENT_SCOPE)
    foreach(_root IN LISTS _roots)
        set(_complete TRUE)
        foreach(_header IN LISTS ARG_HEADERS)
            if(NOT EXISTS "${_root}/${_header}")
                set(_complete FALSE)
                break()
            endif()
        endforeach()
        if(_complete)
            set(${found_var} TRUE PARENT_SCOPE)
            # /usr/include is part of the compiler's default search path: adding
            # it explicitly can break #include_next handling in glibc headers,
            # so it is reported as "nothing to add".
            if(NOT _root STREQUAL "/usr/include")
                set(${include_dir_var} "${_root}" PARENT_SCOPE)
            endif()
            return()
        endif()
    endforeach()
endfunction()

# firmir_use_headers(<target> <include_dir>)
#
# Forwards the root reported by firmir_find_headers() to @p target (as a system
# include directory: these are kernel uapi headers and must not trip -Werror).
function(firmir_use_headers target include_dir)
    if(NOT include_dir)
        return()
    endif()
    target_include_directories(${target} SYSTEM PRIVATE ${include_dir})
endfunction()
