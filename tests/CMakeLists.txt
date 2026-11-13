set(TEST_SOURCES main.cpp)
set(TEST_LIBS)

# gui/test_drm_enumeration.cpp exercises the DRM/KMS enumeration protocol
# (firmir/gui/detail/drmenumeration.hpp) against a fake kernel, so it needs the
# DRM uapi headers on its include path — but no DRM device or libdrm.
include(${PROJECT_SOURCE_DIR}/cmake/FirmirHeaders.cmake)
set(FIRMIR_TEST_DRM_HEADERS FALSE)
set(FIRMIR_TEST_DRM_INCLUDE_DIR "")
if(FIRMIR_DRM_HEADERS STREQUAL "on")
    set(FIRMIR_TEST_DRM_HEADERS TRUE)
elseif(NOT FIRMIR_DRM_HEADERS STREQUAL "off")
    firmir_find_headers(FIRMIR_TEST_DRM_HEADERS FIRMIR_TEST_DRM_INCLUDE_DIR
        HEADERS drm/drm.h drm/drm_mode.h PACKAGES libdrm)
endif()

if(FIRMIR_BUILD_LIB_CORE)
    list(APPEND TEST_SOURCES
        core/test_byte_array.cpp
        core/test_future.cpp
        core/test_io.cpp
        core/test_json.cpp
        core/test_locale.cpp
        core/test_log.cpp
        core/test_object.cpp
        core/test_regexp.cpp
        core/test_settings.cpp
        core/test_socket_notifier.cpp
        core/test_string.cpp
        core/test_thread.cpp
        core/test_timer.cpp
        core/test_translator.cpp
        core/test_variant.cpp
        core/test_xml.cpp
    )
    list(APPEND TEST_LIBS firmir-core)
endif()
if(FIRMIR_BUILD_LIB_DBUS)
    list(APPEND TEST_SOURCES dbus/test_dbus.cpp)
    list(APPEND TEST_LIBS firmir-dbus)
endif()
if(FIRMIR_BUILD_LIB_NETWORKING)
    list(APPEND TEST_SOURCES networking/test_networking.cpp)
    list(APPEND TEST_LIBS firmir-networking)
endif()
if(FIRMIR_BUILD_LIB_GUI AND TARGET firmir-gui-headless)
    list(APPEND TEST_SOURCES gui/test_gui.cpp gui/test_abstract_data_list.cpp gui/test_listmodel.cpp gui/test_editabletext.cpp gui/test_mousearea.cpp
        gui/test_positioners.cpp gui/test_backend_selection.cpp)
    if(FIRMIR_TEST_DRM_HEADERS)
        list(APPEND TEST_SOURCES gui/test_drm_enumeration.cpp)
    endif()
    list(APPEND TEST_LIBS firmir-gui firmir-gui-headless)
elseif(FIRMIR_BUILD_LIB_GUI)
    message(STATUS "firmir-gui tests require the headless plugin; skipping them")
endif()
if(TARGET firmir-gui-eglfs)
    list(APPEND TEST_SOURCES gui/test_eglfs.cpp)
endif()
if(FIRMIR_BUILD_LIB_RENDER AND TARGET firmir-render-null)
    list(APPEND TEST_SOURCES render/test_render.cpp)
    list(APPEND TEST_LIBS firmir-render firmir-render-null)
    if(TARGET firmir-render-soft)
        list(APPEND TEST_LIBS firmir-render-soft)
    endif()
elseif(FIRMIR_BUILD_LIB_RENDER)
    message(STATUS "firmir-render tests require the null plugin; skipping them")
endif()

if(TARGET firmir-gui-controls)
    list(APPEND TEST_SOURCES gui-controls/test_gui_extended.cpp gui-controls/test_controls.cpp)
    list(APPEND TEST_LIBS firmir-gui-controls)
endif()
if(TARGET firmir-multimedia)
    list(APPEND TEST_SOURCES multimedia/test_multimedia.cpp)
    list(APPEND TEST_LIBS firmir-multimedia)
endif()
if(TARGET firmir-wayland-compositor)
    list(APPEND TEST_SOURCES wayland/test_wayland.cpp)
    list(APPEND TEST_LIBS firmir-wayland-client firmir-wayland-compositor)
endif()

# The wl_keyboard keycode translation (plugins/gui/wayland/keytranslate.hpp)
# is header-only; test it wherever xkbcommon is available, even without the
# compositor library.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XKBCOMMON QUIET IMPORTED_TARGET xkbcommon)
endif()
if(XKBCOMMON_FOUND)
    list(APPEND TEST_SOURCES wayland/test_wayland_keymap.cpp)
    list(APPEND TEST_LIBS PkgConfig::XKBCOMMON)
endif()

add_executable(tests ${TEST_SOURCES})
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# <drm/drm.h> for gui/test_drm_enumeration.cpp (kernel uapi headers: system,
# so they must not trip -Werror).
firmir_use_headers(tests "${FIRMIR_TEST_DRM_INCLUDE_DIR}")

# Multimedia backend e2e: synthesizes a rawvideo AVI with libavformat and
# plays it back through the ffmpeg backend plugin (real decode, pts pacing,
# seek). Lives here because tests are configured after plugins/multimedia.
if(FIRMIR_MM_FFMPEG_FOUND AND TARGET firmir-mm-ffmpeg)
    add_executable(firmir-multimedia-mediatest multimedia/mediatest.cpp)
    target_link_libraries(firmir-multimedia-mediatest
        PRIVATE firmir-multimedia PkgConfig::FIRMIR_MM_FFMPEG)
    target_compile_definitions(firmir-multimedia-mediatest PRIVATE
        FIRMIR_MM_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:firmir-mm-ffmpeg>")
    add_dependencies(firmir-multimedia-mediatest firmir-mm-ffmpeg)
    add_test(NAME firmir.multimedia.mediatest
             COMMAND firmir-multimedia-mediatest)
endif()
if(TARGET firmir-render-opengl)
    add_dependencies(tests firmir-render-opengl)
endif()
if(TARGET firmir-render-vulkan)
    add_dependencies(tests firmir-render-vulkan)
endif()
if(TARGET firmir-gui-eglfs)
    # gui/test_eglfs.cpp dlopen()s the backend and its kms integration.
    target_compile_definitions(tests PRIVATE
        FIRMIR_EGLFS_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:firmir-gui-eglfs>")
    add_dependencies(tests firmir-gui-eglfs)
    if(TARGET firmir-gui-eglfs-kms)
        target_compile_definitions(tests PRIVATE FIRMIR_EGLFS_TEST_KMS_PLUGIN)
        add_dependencies(tests firmir-gui-eglfs-kms)
    endif()
endif()
target_link_libraries(tests PRIVATE ${TEST_LIBS})
if(WIN32)
    target_link_libraries(tests PRIVATE ws2_32)
endif()

# <firmir/gui/detail/drmuapi.hpp> must keep working with pre-4.0 DRM uapi
# headers, where struct drm_buf_map has a member named "virtual" and including
# <drm/drm.h> from C++ therefore fails ("expected unqualified-id before
# 'virtual'"). Built against the fixture header in
# tests/fixtures/old_drm_header/ (put first on the include path), so the
# regression is caught on hosts whose own headers are already fixed.
if(FIRMIR_BUILD_LIB_GUI AND TARGET firmir-gui-backend)
    add_executable(firmir-gui-drmuapi-test gui/drmuapi_old_header.cpp)
    target_include_directories(firmir-gui-drmuapi-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/old_drm_header)
    target_link_libraries(firmir-gui-drmuapi-test PRIVATE firmir-gui-backend)
    add_test(NAME firmir.gui.drmuapi_old_header COMMAND firmir-gui-drmuapi-test)
endif()

if(TARGET firmir-mm-ffmpeg)
    target_compile_definitions(tests PRIVATE
        FIRMIR_MM_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:firmir-mm-ffmpeg>")
    add_dependencies(tests firmir-mm-ffmpeg)
elseif(TARGET firmir-mm-gstreamer)
    target_compile_definitions(tests PRIVATE
        FIRMIR_MM_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:firmir-mm-gstreamer>")
    add_dependencies(tests firmir-mm-gstreamer)
endif()

if(FIRMIR_BUILD_LIB_GUI AND TARGET firmir-gui-headless)
    # The test binary links the headless plugin directly (for its control API)
    # while Firmir::Gui::Platform dlopen()s the same file; point the search path
    # at the plugin directory.
    target_compile_definitions(tests PRIVATE
        FIRMIR_GUI_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:firmir-gui-headless>")
endif()
if(FIRMIR_BUILD_LIB_RENDER AND TARGET firmir-render-null)
    # Same pattern for the null render backend.
    target_compile_definitions(tests PRIVATE
        FIRMIR_RENDER_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:firmir-render-null>")
endif()
if(TARGET firmir-render-soft)
    target_compile_definitions(tests PRIVATE FIRMIR_RENDER_TEST_SOFT)
endif()
if(TARGET firmir-render-opengl)
    target_compile_definitions(tests PRIVATE FIRMIR_RENDER_TEST_OPENGL)
endif()
if(TARGET firmir-render-vulkan)
    target_compile_definitions(tests PRIVATE FIRMIR_RENDER_TEST_VULKAN)
endif()

if(FIRMIR_BUILD_LIB_DBUS)
    # Generate D-Bus proxy/adaptor headers with firmir-dbus-tool, exercising the
    # code generator end to end.
    #
    # Under ThreadSanitizer the tool itself is instrumented and refuses the
    # default ASLR memory layout on some kernels ("unexpected memory mapping"),
    # so run it with ASLR disabled just like the test binary below.
    if(FIRMIR_SANITIZER STREQUAL "thread")
        set(DBUS_TOOL_WRAPPER setarch -R)
    else()
        set(DBUS_TOOL_WRAPPER)
    endif()
    # firmir-dbus-tool needs libfirmir-dbus.so, which in turn needs
    # libfirmir-core.so. The absolute RUNPATH baked in at link time is not
    # usable in sandboxed build environments (scratchbox2 rewrites paths), so
    # both library directories are exported explicitly.
    set(DBUS_TOOL_ENV
        LD_LIBRARY_PATH=$<TARGET_FILE_DIR:firmir-dbus>:$<TARGET_FILE_DIR:firmir-core>:$ENV{LD_LIBRARY_PATH})
    set(DBUS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${DBUS_GENERATED_DIR})
    add_custom_command(
        OUTPUT ${DBUS_GENERATED_DIR}/calculator_proxy.hpp
               ${DBUS_GENERATED_DIR}/calculator_adaptor.hpp
        COMMAND ${CMAKE_COMMAND} -E env ${DBUS_TOOL_ENV}
                ${DBUS_TOOL_WRAPPER} $<TARGET_FILE:firmir-dbus-tool> --proxy
                ${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.firmir.Calculator.xml
                -o ${DBUS_GENERATED_DIR}/calculator_proxy.hpp
        COMMAND ${CMAKE_COMMAND} -E env ${DBUS_TOOL_ENV}
                ${DBUS_TOOL_WRAPPER} $<TARGET_FILE:firmir-dbus-tool> --adaptor
                ${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.firmir.Calculator.xml
                -o ${DBUS_GENERATED_DIR}/calculator_adaptor.hpp
        DEPENDS firmir-dbus-tool
                ${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.firmir.Calculator.xml
        VERBATIM
    )
    add_custom_target(dbus_generated_headers
        DEPENDS ${DBUS_GENERATED_DIR}/calculator_proxy.hpp
                ${DBUS_GENERATED_DIR}/calculator_adaptor.hpp)
    add_dependencies(tests dbus_generated_headers)
    target_include_directories(tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

# ThreadSanitizer cannot handle the default ASLR memory layout on some
# kernels ("unexpected memory mapping"); run it with ASLR disabled.
if(FIRMIR_SANITIZER STREQUAL "thread")
    add_test(NAME firmir.tests COMMAND setarch -R $<TARGET_FILE:tests>)
else()
    add_test(NAME firmir.tests COMMAND $<TARGET_FILE:tests>)
endif()

# ffmpeg's transitive dependencies (libavutil/libbluray/libopenmpt/...)
# keep global init state allocated at exit; LSan suppressions keep the suite
# green without hiding leaks in Firmir's own code.
if(FIRMIR_SANITIZER STREQUAL "address")
    set_tests_properties(firmir.tests PROPERTIES
        ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan-thirdparty.supp")
endif()

# Mesa's Vulkan worker cache survives the loader itself (two native allocations
# outside Firmir ownership). Keep LSan for the full suite and isolate only the
# Vulkan pixel test with leak detection disabled; ASan/UBSan remain active.
if(FIRMIR_SANITIZER STREQUAL "address" AND TARGET firmir-render-vulkan)
    set_tests_properties(firmir.tests PROPERTIES
        ENVIRONMENT "FIRMIR_TEST_SKIP_FILTER=render_vulkan")
    add_test(NAME firmir.vulkan COMMAND $<TARGET_FILE:tests>)
    set_tests_properties(firmir.vulkan PROPERTIES
        ENVIRONMENT "FIRMIR_TEST_FILTER=render_vulkan;ASAN_OPTIONS=detect_leaks=0")
endif()
if(FIRMIR_SANITIZER STREQUAL "thread" AND TARGET firmir-render-vulkan)
    set_tests_properties(firmir.tests PROPERTIES
        ENVIRONMENT "FIRMIR_TEST_SKIP_FILTER=render_vulkan")
    add_test(NAME firmir.vulkan COMMAND setarch -R $<TARGET_FILE:tests>)
    set_tests_properties(firmir.vulkan PROPERTIES
        ENVIRONMENT "FIRMIR_TEST_FILTER=render_vulkan;TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan-lavapipe.supp")
endif()

# The test binary, the libraries it links and the plugins it loads carry an
# absolute RUNPATH into the build tree, which sandboxed build environments
# (scratchbox2) ignore. Point the loader at the build-tree libraries instead.
firmir_add_test_library_path(${FIRMIR_TEST_RUNTIME_TARGETS})

# Self-contained launch: the test binaries carry the absolute build-tree
# library directories in their DT_RPATH, so `./build/tests/tests` starts
# without LD_LIBRARY_PATH or any other environment setup.
firmir_bake_library_path(tests ${FIRMIR_TEST_RUNTIME_TARGETS})
firmir_bake_library_path(firmir-multimedia-mediatest ${FIRMIR_TEST_RUNTIME_TARGETS})
