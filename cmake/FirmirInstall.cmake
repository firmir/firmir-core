# Firmir installation helpers.
#
# Layout (prefix defaults to /usr):
#   libraries        -> lib/
#   executables      -> bin/
#   public headers   -> include/firmir/<module>/ (include/ trees merge)
#   backend plugins  -> lib/firmir/plugins/{gui,render}/
#   pkg-config files -> lib/pkgconfig/firmir-<module>.pc

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND NOT WIN32)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

set(FIRMIR_INSTALL_LIBDIR lib)
set(FIRMIR_INSTALL_BINDIR bin)
set(FIRMIR_INSTALL_INCLUDEDIR include)
# Absolute path baked into firmir-gui / firmir-render as the fallback plugin
# search directory (used by installed binaries).
set(FIRMIR_SYSTEM_PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/lib/firmir/plugins)
if(WIN32)
    # Windows has no lib/ share/ split: keep plugins next to the binaries.
    set(FIRMIR_SYSTEM_PLUGIN_DIR ${CMAKE_INSTALL_PREFIX}/plugins)
endif()

# firmir_install_library(<target> MODULE <module>
#                        [REQUIRES <pkg-config name>...] [HEADERS <dir>])
#
# Installs the library into lib/, its public header tree (default
# ${CMAKE_CURRENT_SOURCE_DIR}/include, must contain the firmir/ directory)
# into include/, and generates + installs firmir-<module>.pc into
# lib/pkgconfig/.
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
# plugins install into lib/firmir/plugins/<kind>/[<name>/].
function(firmir_install_plugin target kind)
    cmake_parse_arguments(ARG "" "SUBDIR" "" ${ARGN})
    set(destination ${FIRMIR_INSTALL_LIBDIR}/firmir/plugins/${kind})
    if(ARG_SUBDIR)
        set(destination ${destination}/${ARG_SUBDIR})
    endif()
    install(TARGETS ${target}
            LIBRARY DESTINATION ${destination})
endfunction()
