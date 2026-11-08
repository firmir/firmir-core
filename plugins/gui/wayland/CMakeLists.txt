# Wayland GUI adapter. Connection, registry and event dispatch are provided by
# firmir-wayland-client; this file contains only the Firmir platform adapter.
add_library(firmir-gui-wayland SHARED wayland.cpp)
set_target_properties(firmir-gui-wayland PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR})
target_link_libraries(firmir-gui-wayland
    PRIVATE firmir-gui-backend firmir-core firmir-wayland-client)

# Keymap handling (wl_keyboard -> text) goes through xkbcommon.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
target_link_libraries(firmir-gui-wayland PRIVATE PkgConfig::XKBCOMMON)

# shm_open needs -lrt on glibc < 2.34.
find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
    target_link_libraries(firmir-gui-wayland PRIVATE ${RT_LIBRARY})
endif()
