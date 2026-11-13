# eglfs display backend: fullscreen, compositor-less presentation through a
# device integration sub-plugin (firmir-gui-eglfs-<name>.so). The backend
# itself only routes screens/windows to the integration selected with
# FIRMIR_EGLFS_INTEGRATION (see <firmir/gui/eglfsintegration.hpp>).
add_library(firmir-gui-eglfs SHARED eglfs.cpp)
set_target_properties(firmir-gui-eglfs PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR})
target_link_libraries(firmir-gui-eglfs PRIVATE firmir-gui-backend firmir-core)
# Installed-by-prefix fallback directory for the device integrations; the
# plugin joins it to the search path derived from FIRMIR_EGLFS_PLUGIN_PATH.
target_compile_definitions(firmir-gui-eglfs PRIVATE
    FIRMIR_EGLFS_SYSTEM_PLUGIN_DIR="${FIRMIR_SYSTEM_PLUGIN_DIR}/gui/eglfs")

# The DRM/KMS headers were located once for the whole plugins/gui tree (see
# ../CMakeLists.txt): reuse that verdict instead of probing again.
if(FIRMIR_HAVE_DRM_H)
    add_subdirectory(kms)
else()
    message(STATUS "drm/drm.h not found; skipping the eglfs kms integration "
        "(see FIRMIR_DRM_HEADERS)")
endif()