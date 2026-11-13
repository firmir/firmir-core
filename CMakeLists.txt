cmake_minimum_required(VERSION 3.16)
project(firmir VERSION 1.0.0 LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FirmirInstall.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FirmirTestEnv.cmake)

option(FIRMIR_BUILD_TESTS "Build the test suite" ON)
option(FIRMIR_BUILD_EXAMPLES "Build examples" ON)
option(FIRMIR_BUILD_DOC_TOOL "Build the firmir-doc tool" ON)
option(FIRMIR_BUILD_TOOL_FIRML "Build the firmlc transpiler (Firml -> C++)" ON)
option(FIRMIR_BUILD_LIB_CORE "Build firmir-core" ON)
option(FIRMIR_BUILD_LIB_DBUS "Build firmir-dbus" ON)
option(FIRMIR_BUILD_LIB_NETWORKING "Build firmir-networking" ON)
option(FIRMIR_BUILD_LIB_GUI "Build firmir-gui" ON)
option(FIRMIR_BUILD_LIB_GUI_CONTROLS "Build firmir-gui-controls" ON)
option(FIRMIR_BUILD_LIB_MULTIMEDIA "Build firmir-multimedia" ON)
option(FIRMIR_BUILD_LIB_RENDER "Build firmir-render" ON)
option(FIRMIR_BUILD_LIB_WAYLAND "Build firmir-wayland client/server modules" ON)
option(FIRMIR_BUILD_LIB_WAYLAND_COMPOSITOR "Build firmir-wayland-compositor" ON)
option(FIRMIR_BUILD_PLUGIN_GUI_DRM "Build the DRM GUI plugin" ON)
option(FIRMIR_BUILD_PLUGIN_GUI_EGLFS "Build the eglfs GUI plugin" ON)
option(FIRMIR_BUILD_PLUGIN_GUI_EVDEV "Build the evdev GUI plugin" ON)
option(FIRMIR_BUILD_PLUGIN_GUI_HEADLESS "Build the headless GUI plugin" ON)
option(FIRMIR_BUILD_PLUGIN_GUI_WAYLAND "Build the Wayland GUI plugin" ON)
option(FIRMIR_BUILD_APPS "Build the demo applications (none at the moment)" OFF)
option(FIRMIR_BUILD_PLUGIN_RENDER_NULL "Build the null render plugin" ON)
option(FIRMIR_BUILD_PLUGIN_RENDER_SOFT "Build the software render plugin" ON)
option(FIRMIR_BUILD_PLUGIN_RENDER_OPENGL "Build the OpenGL ES render plugin" ON)
option(FIRMIR_BUILD_PLUGIN_RENDER_VULKAN "Build the Vulkan render plugin" ON)

# Target system the build runs on: "linux" (default) or "windows". Modules
# that cannot work on the selected system are skipped automatically.
set(FIRMIR_BUILD_SYSTEM "linux" CACHE STRING "Target system: linux or windows")
set_property(CACHE FIRMIR_BUILD_SYSTEM PROPERTY STRINGS linux windows)
if(NOT FIRMIR_BUILD_SYSTEM MATCHES "^(linux|windows)$")
    message(FATAL_ERROR
        "FIRMIR_BUILD_SYSTEM='${FIRMIR_BUILD_SYSTEM}' is not supported; "
        "expected 'linux' or 'windows'")
endif()
if(FIRMIR_BUILD_SYSTEM STREQUAL "windows")
    # Wayland, D-Bus (unix sockets) and the evdev/DRM backends have no
    # Windows counterpart — do not build them for this system.
    set(FIRMIR_BUILD_LIB_DBUS OFF)
    set(FIRMIR_BUILD_LIB_WAYLAND OFF)
    set(FIRMIR_BUILD_LIB_WAYLAND_COMPOSITOR OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_DRM OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_EGLFS OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_EVDEV OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_WAYLAND OFF)
    if(NOT FIRMIR_SANITIZER STREQUAL "")
        message(WARNING "FIRMIR_SANITIZER is not supported on windows; ignoring")
        set(FIRMIR_SANITIZER "")
    endif()
    # No dllexport annotations in the sources: let CMake export every symbol
    # from the shared libraries (the public API carries no global data, and
    # plugin entry points are plain extern \"C\" functions).
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    # Keep all DLLs/EXEs in one directory so executables find the libraries
    # at runtime (Windows has no RPATH) and the exe-relative plugin lookup
    # (<exe>/../plugins) works the same as on Linux.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "Firmir target system: windows "
        "(dbus/wayland/evdev/drm/eglfs are Linux-only and are skipped)")
else()
    message(STATUS "Firmir target system: linux")
endif()

if(FIRMIR_BUILD_TESTS)
    enable_testing()
endif()
set(FIRMIR_SANITIZER "" CACHE STRING "Sanitizer to enable: '', 'address' or 'thread'")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /Wall on MSVC is unusable (noise in system headers); /W4 is the
    # /W4 /WX is the -Wall -Wextra -Werror equivalent. Suppressed classes
    # match warnings GCC does not raise with -Wall -Wextra (i.e. the project
    # never enforced them): C4244/C4267 are -Wconversion territory (numeric
    # narrowing in pixel/math code), C4458/C4456 are member/local shadowing
    # (the GUI code names locals `d` next to the PIMPL member `d` on purpose;
    # GCC only warns with -Wshadow, which the project does not use),
    # C4324 is "structure padded due to alignas" (intended padding) and
    # C4611 is "setjmp + C++ destructors not portable" (libjpeg's documented
    # error-handling idiom).
    add_compile_options(/W4 /WX /permissive- /utf-8 /Zc:__cplusplus /EHsc /MP
                        /wd4244 /wd4267 /wd4456 /wd4458 /wd4324 /wd4611)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

if(FIRMIR_SANITIZER STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(FIRMIR_SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
elseif(NOT FIRMIR_SANITIZER STREQUAL "")
    message(FATAL_ERROR "Unknown FIRMIR_SANITIZER='${FIRMIR_SANITIZER}'")
endif()

# GCC 13 emits false-positive maybe-uninitialized warnings inside libstdc++
# regex/function headers in sanitizer builds; keep the warning, but do not
# let it fail the -Werror build.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT FIRMIR_SANITIZER STREQUAL "")
    add_compile_options(-Wno-error=maybe-uninitialized)
endif()

# Under ThreadSanitizer instrumented binaries refuse the default ASLR memory
# layout on some kernels ("unexpected memory mapping"), so firmlc and the
# tests run with ASLR disabled (used by the firml snapshot tests and by
# firml_add_target() build-time codegen).
if(FIRMIR_SANITIZER STREQUAL "thread")
    set(FIRML_TOOL_WRAPPER "setarch -R")
else()
    set(FIRML_TOOL_WRAPPER "")
endif()

if(FIRMIR_BUILD_LIB_NETWORKING AND NOT FIRMIR_BUILD_LIB_CORE)
    message(FATAL_ERROR "firmir-networking requires firmir-core")
endif()
if(FIRMIR_BUILD_LIB_GUI AND NOT FIRMIR_BUILD_LIB_CORE)
    message(FATAL_ERROR "firmir-gui requires firmir-core")
endif()
if(FIRMIR_BUILD_LIB_GUI AND NOT FIRMIR_BUILD_LIB_RENDER)
    message(FATAL_ERROR "firmir-gui requires firmir-render (scene rendering goes through firmir-render)")
endif()
if(FIRMIR_BUILD_LIB_RENDER AND NOT FIRMIR_BUILD_LIB_CORE)
    message(FATAL_ERROR "firmir-render requires firmir-core")
endif()
if(FIRMIR_BUILD_LIB_DBUS AND NOT FIRMIR_BUILD_LIB_CORE)
    message(FATAL_ERROR "firmir-dbus requires firmir-core")
endif()
if(FIRMIR_BUILD_LIB_WAYLAND AND NOT FIRMIR_BUILD_LIB_CORE)
    message(FATAL_ERROR "firmir-wayland requires firmir-core (the protocol generator uses Firmir XML)")
endif()
if(NOT FIRMIR_BUILD_LIB_GUI)
    set(FIRMIR_BUILD_LIB_GUI_CONTROLS OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_DRM OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_EGLFS OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_EVDEV OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_HEADLESS OFF)
    set(FIRMIR_BUILD_PLUGIN_GUI_WAYLAND OFF)
endif()
if(NOT FIRMIR_BUILD_LIB_RENDER)
    set(FIRMIR_BUILD_PLUGIN_RENDER_NULL OFF)
    set(FIRMIR_BUILD_PLUGIN_RENDER_SOFT OFF)
    set(FIRMIR_BUILD_PLUGIN_RENDER_OPENGL OFF)
    set(FIRMIR_BUILD_PLUGIN_RENDER_VULKAN OFF)
endif()

find_package(Threads REQUIRED)

if(FIRMIR_BUILD_LIB_CORE)
    add_subdirectory(libs/core)
    add_subdirectory(plugins/core)
endif()
if(FIRMIR_BUILD_LIB_WAYLAND)
    # Host tool generating the Wayland protocol wrappers for libs/wayland.
    add_subdirectory(tools/wayland)
endif()
if(FIRMIR_BUILD_LIB_DBUS)
    add_subdirectory(libs/dbus)
endif()
if(FIRMIR_BUILD_LIB_NETWORKING)
    add_subdirectory(libs/networking)
endif()
if(FIRMIR_BUILD_LIB_GUI)
    add_subdirectory(libs/gui)
    if(FIRMIR_BUILD_LIB_GUI_CONTROLS)
        add_subdirectory(libs/gui-controls)
    endif()
    if(FIRMIR_BUILD_LIB_MULTIMEDIA)
        add_subdirectory(libs/multimedia)
        add_subdirectory(plugins/multimedia)
    endif()
    if(FIRMIR_BUILD_LIB_WAYLAND)
        add_subdirectory(libs/wayland)
    endif()
    if(FIRMIR_BUILD_PLUGIN_GUI_DRM OR FIRMIR_BUILD_PLUGIN_GUI_EGLFS OR FIRMIR_BUILD_PLUGIN_GUI_EVDEV OR FIRMIR_BUILD_PLUGIN_GUI_HEADLESS OR FIRMIR_BUILD_PLUGIN_GUI_WAYLAND)
        add_subdirectory(plugins/gui)
    endif()
endif()
if(FIRMIR_BUILD_LIB_RENDER)
    add_subdirectory(libs/render)
    if(FIRMIR_BUILD_PLUGIN_RENDER_NULL OR FIRMIR_BUILD_PLUGIN_RENDER_SOFT OR FIRMIR_BUILD_PLUGIN_RENDER_OPENGL OR FIRMIR_BUILD_PLUGIN_RENDER_VULKAN)
        add_subdirectory(plugins/render)
    endif()
endif()

# Targets a test run may load at runtime: linked libraries plus the plugins
# that are dlopen()ed by them (a plugin's own dependencies must be findable
# too). firmir_test_environment() ignores the targets that were not built.
set(FIRMIR_TEST_RUNTIME_TARGETS
    firmir-core firmir-dbus firmir-networking
    firmir-render firmir-multimedia firmir-gui firmir-gui-controls
    firmir-wayland-protocols firmir-wayland-client firmir-wayland-server
    firmir-wayland-compositor
    firmir-gui-drm firmir-gui-eglfs firmir-gui-eglfs-kms firmir-gui-evdev
    firmir-gui-headless firmir-gui-wayland
    firmir-render-null firmir-render-soft firmir-render-opengl
    firmir-render-vulkan
    firmir-mm-gstreamer firmir-mm-ffmpeg)

if(FIRMIR_BUILD_DOC_TOOL)
    add_subdirectory(tools/doc)
endif()
if(FIRMIR_BUILD_LIB_WAYLAND AND NOT FIRMIR_BUILD_LIB_GUI)
    add_subdirectory(libs/wayland)
endif()
if(FIRMIR_BUILD_TOOL_FIRML)
    add_subdirectory(tools/firml)
endif()
if(FIRMIR_BUILD_EXAMPLES)
    if(FIRMIR_BUILD_LIB_CORE)
        add_subdirectory(examples/core)
        add_subdirectory(examples/json)
        add_subdirectory(examples/xml)
    endif()
    if(FIRMIR_BUILD_LIB_DBUS)
        add_subdirectory(examples/dbus)
    endif()
    if(FIRMIR_BUILD_LIB_NETWORKING)
        add_subdirectory(examples/networking)
    endif()
    if(FIRMIR_BUILD_LIB_GUI)
        add_subdirectory(examples/gui)
    endif()
    if(TARGET firmir-multimedia)
        add_subdirectory(examples/multimedia)
    endif()
    if(FIRMIR_BUILD_LIB_RENDER)
        add_subdirectory(examples/render)
    endif()
    if(TARGET firmir-wayland-compositor)
        add_subdirectory(examples/wayland)
    endif()
endif()

if(FIRMIR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
