# The first real Firmir application: two generated views selected at runtime.
# Path-based generation: views/red.fml -> firml_generated/views/red.h.
firml_add_target(red_window_codegen
    ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES views/red.fml views/blue.fml)

add_executable(red-window main.cpp)
target_link_libraries(red-window PRIVATE red_window_codegen firmir-gui)
