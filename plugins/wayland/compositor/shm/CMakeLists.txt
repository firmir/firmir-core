# wl_shm buffer reader: maps the client's shared memory and copies the frame.
add_library(firmir-wayland-compositor-shm SHARED shm.cpp)
set_target_properties(firmir-wayland-compositor-shm PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_DIR})
target_link_libraries(firmir-wayland-compositor-shm
    PRIVATE firmir-wayland-compositor)
