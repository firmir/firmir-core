add_library(firmir-gui-drm SHARED drm.cpp)
firmir_use_headers(firmir-gui-drm "${FIRMIR_DRM_INCLUDE_DIR}")
set_target_properties(firmir-gui-drm PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR})
target_link_libraries(firmir-gui-drm PRIVATE firmir-gui-backend)
find_package(Threads REQUIRED)
target_link_libraries(firmir-gui-drm PRIVATE Threads::Threads)
