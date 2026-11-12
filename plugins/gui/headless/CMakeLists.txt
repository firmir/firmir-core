# Headless display+input backend (also the test backend; the test binary
# links it directly while Firmir::Gui::Platform dlopen()s the same file).
add_library(firmir-gui-headless SHARED headless.cpp)
set_target_properties(firmir-gui-headless PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR})
if(WIN32)
    # tests.exe links plugins directly; Windows resolves link-time DLLs from
    # the executable's directory (bin/), while runtime loading uses plugins/.
    add_custom_command(TARGET firmir-gui-headless POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:firmir-gui-headless> ${CMAKE_BINARY_DIR}/bin)
endif()
target_link_libraries(firmir-gui-headless PRIVATE firmir-gui-backend)
target_include_directories(firmir-gui-headless PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
