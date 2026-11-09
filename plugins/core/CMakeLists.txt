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
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_CORE_PLUGIN_DIR})
target_link_libraries(firmir-locale-test PRIVATE firmir-core)
firmir_install_plugin(firmir-locale-test core)
