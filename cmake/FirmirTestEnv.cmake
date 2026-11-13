# Test-run environment helpers.
#
# Test executables, the libraries they link and the plugins they dlopen() all
# carry an absolute RUNPATH pointing into the build tree. Sandboxed build
# environments (scratchbox2, qemu based package builds) do not honour that
# RUNPATH, so the loader is pointed at the build-tree directories explicitly
# through LD_LIBRARY_PATH for every ctest test.
#
# Usage:
#   firmir_test_environment(<out_var> [target ...])
#
# <out_var> receives a single "LD_LIBRARY_PATH=dir:dir:..." string suitable for
# the ENVIRONMENT test property. Unknown targets, imported targets (names with
# "::") and interface libraries are ignored, so a list of optional targets can
# be passed unconditionally.
function(firmir_test_environment out_var)
    set(dirs)
    foreach(target IN LISTS ARGN)
        if(NOT TARGET "${target}" OR "${target}" MATCHES "::")
            continue()
        endif()
        get_target_property(target_type "${target}" TYPE)
        if(target_type STREQUAL "SHARED_LIBRARY" OR
           target_type STREQUAL "MODULE_LIBRARY" OR
           target_type STREQUAL "EXECUTABLE")
            list(APPEND dirs "$<TARGET_FILE_DIR:${target}>")
        endif()
    endforeach()
    if(NOT dirs)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    # Deduplicate after the generator expressions are evaluated: several
    # targets (e.g. the wayland libraries and their plugins) share a directory.
    set(${out_var}
        "LD_LIBRARY_PATH=$<JOIN:$<REMOVE_DUPLICATES:${dirs}>,:>:$ENV{LD_LIBRARY_PATH}"
        PARENT_SCOPE)
endfunction()

# Appends the build-tree LD_LIBRARY_PATH to every test defined in the current
# directory, keeping any environment entries the tests already carry.
function(firmir_add_test_library_path)
    firmir_test_environment(env ${ARGN})
    if(NOT env)
        return()
    endif()
    get_property(dir_tests DIRECTORY PROPERTY TESTS)
    foreach(test IN LISTS dir_tests)
        set_property(TEST "${test}" APPEND PROPERTY ENVIRONMENT "${env}")
    endforeach()
endfunction()

# Absolute build-tree directory the given library/plugin target is written to.
function(firmir_target_directory out_var target)
    get_target_property(dir "${target}" LIBRARY_OUTPUT_DIRECTORY)
    if(NOT dir)
        get_target_property(dir "${target}" BINARY_DIR)
    endif()
    set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()

# Bakes the build-tree directories of the given libraries/plugins into the
# RUNPATH/RPATH of <target> so that the binary is launchable on its own:
#
#   ./build/tests/tests
#
# without LD_LIBRARY_PATH or a ctest-provided environment. What makes it work
# where the plain CMake default does not:
#  - the entries are $ORIGIN-relative (BUILD_RPATH_USE_ORIGIN), so the binary
#    keeps working when the build tree lives under a different absolute path at
#    run time than at configure time (containers, scratchbox2 sandboxes,
#    relocated/copied build trees);
#  - the binaries are linked with --disable-new-dtags, i.e. with DT_RPATH
#    instead of DT_RUNPATH. DT_RPATH is inherited by the dependencies of
#    dlopen()ed plugins and is honoured by the older loaders used inside build
#    sandboxes, which ignore DT_RUNPATH;
#  - the RPATH lists every firmir library and plugin directory, not only the
#    direct dependencies of the target.
function(firmir_bake_library_path target)
    if(NOT TARGET "${target}")
        return()
    endif()
    set(dirs)
    foreach(lib IN LISTS ARGN)
        if(NOT TARGET "${lib}" OR "${lib}" MATCHES "::")
            continue()
        endif()
        get_target_property(type "${lib}" TYPE)
        if(type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "MODULE_LIBRARY")
            firmir_target_directory(dir "${lib}")
            if(dir)
                list(APPEND dirs "${dir}")
            endif()
        endif()
    endforeach()
    if(NOT dirs)
        return()
    endif()
    list(REMOVE_DUPLICATES dirs)
    set_property(TARGET "${target}" APPEND PROPERTY BUILD_RPATH "${dirs}")
    # Let CMake rewrite the absolute build paths above into $ORIGIN/... entries.
    set_property(TARGET "${target}" PROPERTY BUILD_RPATH_USE_ORIGIN ON)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options("${target}" PRIVATE "LINKER:--disable-new-dtags")
    endif()
endfunction()
