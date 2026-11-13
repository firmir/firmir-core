add_library(firmir-gui-evdev SHARED evdev.cpp)
firmir_use_headers(firmir-gui-evdev "${FIRMIR_LINUX_INPUT_INCLUDE_DIR}")
set_target_properties(firmir-gui-evdev PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR})
target_link_libraries(firmir-gui-evdev PRIVATE firmir-gui-backend)
find_package(Threads REQUIRED)
target_link_libraries(firmir-gui-evdev PRIVATE Threads::Threads)
