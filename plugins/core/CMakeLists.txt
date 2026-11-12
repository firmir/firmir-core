# Backend plugins for firmir-core services. Each plugin builds into
# ${CMAKE_BINARY_DIR}/plugins/core as firmir-<kind>-<name>.so (no "lib"
# prefix) so the base library finds it via FIRMIR_CORE_PLUGIN_PATH /
# the exe-relative search paths.
set(FIRMIR_CORE_PLUGIN_DIR ${CMAKE_BINARY_DIR}/plugins/core)

# ----------------------------- locale/test -------------------------------
# Test locale backend: reports the system language as Russian and switches
# it at runtime when its dynamic "simulateLanguage" property is set (used
# by tests/core/test_locale.cpp; also handy for demos).
add_library(firmir-locale-test SHARED locale/testlocalebackend.cpp)
set_target_properties(firmir-locale-test PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_CORE_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_CORE_PLUGIN_DIR})
if(WIN32)
    # tests.exe links plugins directly; Windows resolves link-time DLLs from
    # the executable's directory (bin/), while runtime loading uses plugins/.
    add_custom_command(TARGET firmir-locale-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:firmir-locale-test> ${CMAKE_BINARY_DIR}/bin)
endif()
target_link_libraries(firmir-locale-test PRIVATE firmir-core)
firmir_install_plugin(firmir-locale-test core)
