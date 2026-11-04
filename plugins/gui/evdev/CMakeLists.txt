add_library(firmir-gui-evdev SHARED evdev.cpp)
set_target_properties(firmir-gui-evdev PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR})
target_link_libraries(firmir-gui-evdev PRIVATE firmir-gui-backend)
find_package(Threads REQUIRED)
target_link_libraries(firmir-gui-evdev PRIVATE Threads::Threads)
