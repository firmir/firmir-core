# Headless display+input backend (also the test backend; the test binary
# links it directly while Firmir::Gui::Platform dlopen()s the same file).
add_library(firmir-gui-headless SHARED headless.cpp)
set_target_properties(firmir-gui-headless PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR})
target_link_libraries(firmir-gui-headless PRIVATE firmir-gui-backend)
target_include_directories(firmir-gui-headless PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
