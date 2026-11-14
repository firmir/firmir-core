# Buffer backend plugins for the Wayland modules. Each plugin builds into
# ${CMAKE_BINARY_DIR}/plugins/wayland/{client,compositor} as
# firmir-wayland-<side>-<name>.so (no "lib" prefix) so the modules find it via
# FIRMIR_WAYLAND_<SIDE>_PLUGIN_PATH and the executable-relative lookup.
set(FIRMIR_WAYLAND_CLIENT_PLUGIN_DIR ${CMAKE_BINARY_DIR}/plugins/wayland/client)
set(FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_DIR
    ${CMAKE_BINARY_DIR}/plugins/wayland/compositor)

if(TARGET firmir-wayland-client)
    add_subdirectory(client)
else()
    message(STATUS "firmir-wayland-client is unavailable; skipping its "
        "buffer backend plugins")
endif()

if(TARGET firmir-wayland-compositor)
    add_subdirectory(compositor)
else()
    message(STATUS "firmir-wayland-compositor is unavailable; skipping its "
        "buffer backend plugins")
endif()
