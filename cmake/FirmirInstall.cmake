# Firmir installation helpers.
#
# Layout (prefix defaults to /usr):
#   libraries        -> <libdir>/
#   executables      -> bin/
#   public headers   -> include/firmir/<module>/ (include/ trees merge)
#   backend plugins  -> <libdir>/firmir/plugins/{gui,render}/
#   pkg-config files -> <libdir>/pkgconfig/firmir-<module>.pc
#
# <libdir> is the platform's library directory: lib64/ on a 64-bit RPM system,
# lib/<triplet>/ on a Debian one, lib/ on a 32-bit system or when the packager
# asks for it (-DCMAKE_INSTALL_LIBDIR/-DLIB_INSTALL_DIR). It is not a detail:
# on a 64-bit system the loader, the linker and pkg-config all look in lib64/,
# so a firmir installed into lib/ is invisible to every consumer of it — the
# same recipe then builds on armv7 and breaks on x86_64/aarch64.

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND NOT WIN32)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()
include(GNUInstallDirs)

set(FIRMIR_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
set(FIRMIR_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
set(FIRMIR_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
# The .pc files stay relocatable: their prefix is derived from the directory
# they are installed into, so it has to step out of pkgconfig/ and out of every
# level of <libdir> (".." for lib/, "../../.." for lib/<triplet>/).
set(FIRMIR_PC_PREFIX_UP "..")
string(REPLACE "/" ";" _firmir_libdir_levels "${FIRMIR_INSTALL_LIBDIR}")
foreach(_firmir_level IN LISTS _firmir_libdir_levels)
    string(APPEND FIRMIR_PC_PREFIX_UP "/..")
endforeach()
unset(_firmir_libdir_levels)
unset(_firmir_level)
# Absolute path baked into firmir-gui / firmir-render as the fallback plugin
# search directory (used by installed binaries).
set(FIRMIR_SYSTEM_PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/${FIRMIR_INSTALL_LIBDIR}/firmir/plugins)
if(WIN32)
    # Windows has no lib/ share/ split: keep plugins next to the binaries.
    set(FIRMIR_SYSTEM_PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/plugins)
endif()

# firmir_install_library(<target> MODULE <module>
#                        [REQUIRES <pkg-config name>...] [HEADERS <dir>])
#
# Installs the library into <libdir>/, its public header tree (default
# ${CMAKE_CURRENT_SOURCE_DIR}/include, must contain the firmir/ directory)
# into include/, and generates + installs firmir-<module>.pc into
# <libdir>/pkgconfig/.
function(firmir_install_library target)
    cmake_parse_arguments(ARG "" "MODULE;HEADERS" "REQUIRES" ${ARGN})
    if(NOT ARG_MODULE)
        message(FATAL_ERROR "firmir_install_library(${target}): MODULE is required")
    endif()

    install(TARGETS ${target}
            LIBRARY DESTINATION ${FIRMIR_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${FIRMIR_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${FIRMIR_INSTALL_BINDIR})

    if(NOT ARG_HEADERS)
        set(ARG_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include)
    endif()
    if(IS_DIRECTORY ${ARG_HEADERS})
        install(DIRECTORY ${ARG_HEADERS}/
                DESTINATION ${FIRMIR_INSTALL_INCLUDEDIR})
    endif()

    string(REPLACE ";" " " FIRMIR_PC_REQUIRES "${ARG_REQUIRES}")
    set(FIRMIR_PC_MODULE ${ARG_MODULE})
    set(FIRMIR_PC_TARGET ${target})
    configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/firmir.pc.in
                   ${CMAKE_BINARY_DIR}/pkgconfig/firmir-${ARG_MODULE}.pc @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/pkgconfig/firmir-${ARG_MODULE}.pc
            DESTINATION ${FIRMIR_INSTALL_LIBDIR}/pkgconfig)
endfunction()

# firmir_install_example(<target>): example binaries install into bin/.
function(firmir_install_example target)
    install(TARGETS ${target} RUNTIME DESTINATION ${FIRMIR_INSTALL_BINDIR})
endfunction()

# firmir_install_plugin(<target> <gui|render> [SUBDIR <name>]): backend
# plugins install into <libdir>/firmir/plugins/<kind>/[<name>/].
function(firmir_install_plugin target kind)
    cmake_parse_arguments(ARG "" "SUBDIR" "" ${ARGN})
    set(destination ${FIRMIR_INSTALL_LIBDIR}/firmir/plugins/${kind})
    if(ARG_SUBDIR)
        set(destination ${destination}/${ARG_SUBDIR})
    endif()
    install(TARGETS ${target}
            LIBRARY DESTINATION ${destination})
endfunction()
