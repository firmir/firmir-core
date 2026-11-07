set(TEST_SOURCES main.cpp)
set(TEST_LIBS)

if(FIRMIR_BUILD_LIB_CORE)
    list(APPEND TEST_SOURCES
        test_byte_array.cpp
        test_future.cpp
        test_io.cpp
        test_log.cpp
        test_object.cpp
        test_regexp.cpp
        test_settings.cpp
        test_socket_notifier.cpp
        test_string.cpp
        test_thread.cpp
        test_timer.cpp
        test_variant.cpp
    )
    list(APPEND TEST_LIBS firmir-core)
endif()
if(FIRMIR_BUILD_LIB_JSON)
    list(APPEND TEST_SOURCES test_json.cpp)
    list(APPEND TEST_LIBS firmir-json)
endif()
if(FIRMIR_BUILD_LIB_XML)
    list(APPEND TEST_SOURCES test_xml.cpp)
    list(APPEND TEST_LIBS firmir-xml)
endif()
if(FIRMIR_BUILD_LIB_DBUS)
    list(APPEND TEST_SOURCES test_dbus.cpp)
    list(APPEND TEST_LIBS firmir-dbus)
endif()
if(FIRMIR_BUILD_LIB_NETWORKING)
    list(APPEND TEST_SOURCES test_networking.cpp)
    list(APPEND TEST_LIBS firmir-networking)
endif()
if(FIRMIR_BUILD_LIB_GUI AND TARGET firmir-gui-headless)
    list(APPEND TEST_SOURCES test_gui.cpp test_abstract_data_list.cpp
        test_positioners.cpp)
    list(APPEND TEST_LIBS firmir-gui firmir-gui-headless)
elseif(FIRMIR_BUILD_LIB_GUI)
    message(STATUS "firmir-gui tests require the headless plugin; skipping them")
endif()
if(FIRMIR_BUILD_LIB_RENDER AND TARGET firmir-render-null)
    list(APPEND TEST_SOURCES test_render.cpp)
    list(APPEND TEST_LIBS firmir-render firmir-render-null)
    if(TARGET firmir-render-soft)
        list(APPEND TEST_LIBS firmir-render-soft)
    endif()
elseif(FIRMIR_BUILD_LIB_RENDER)
    message(STATUS "firmir-render tests require the null plugin; skipping them")
endif()

if(TARGET firmir-gui-controls)
    list(APPEND TEST_SOURCES test_gui_extended.cpp)
    list(APPEND TEST_LIBS firmir-gui-controls)
endif()
if(TARGET firmir-wayland-compositor)
    list(APPEND TEST_SOURCES test_wayland.cpp)
    list(APPEND TEST_LIBS firmir-wayland-client firmir-wayland-compositor)
endif()

add_executable(tests ${TEST_SOURCES})
if(TARGET firmir-render-opengl)
    add_dependencies(tests firmir-render-opengl)
endif()
if(TARGET firmir-render-vulkan)
    add_dependencies(tests firmir-render-vulkan)
endif()
target_link_libraries(tests PRIVATE ${TEST_LIBS})

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
    set(DBUS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${DBUS_GENERATED_DIR})
    add_custom_command(
        OUTPUT ${DBUS_GENERATED_DIR}/calculator_proxy.hpp
               ${DBUS_GENERATED_DIR}/calculator_adaptor.hpp
        COMMAND ${DBUS_TOOL_WRAPPER} $<TARGET_FILE:firmir-dbus-tool> --proxy
                ${CMAKE_CURRENT_SOURCE_DIR}/org.firmir.Calculator.xml
                -o ${DBUS_GENERATED_DIR}/calculator_proxy.hpp
        COMMAND ${DBUS_TOOL_WRAPPER} $<TARGET_FILE:firmir-dbus-tool> --adaptor
                ${CMAKE_CURRENT_SOURCE_DIR}/org.firmir.Calculator.xml
                -o ${DBUS_GENERATED_DIR}/calculator_adaptor.hpp
        DEPENDS firmir-dbus-tool
                ${CMAKE_CURRENT_SOURCE_DIR}/org.firmir.Calculator.xml
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
