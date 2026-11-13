# KMS device integration for the eglfs backend: output enumeration through
# raw DRM ioctls, GBM surfaces and an EGL/GLES2 context per window (no
# libdrm, no compositor). Built into plugins/eglfs/ so the backend finds it
# next to the GUI plugins.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FIRMIR_EGLFS_KMS QUIET IMPORTED_TARGET gbm egl glesv2)
endif()
if(NOT FIRMIR_EGLFS_KMS_FOUND)
    message(STATUS "gbm/egl/glesv2 not found; skipping the eglfs kms integration")
    return()
endif()

add_library(firmir-gui-eglfs-kms SHARED kms.cpp)
set_target_properties(firmir-gui-eglfs-kms PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR}/eglfs
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_GUI_PLUGIN_DIR}/eglfs)
target_link_libraries(firmir-gui-eglfs-kms PRIVATE
    firmir-gui-backend PkgConfig::FIRMIR_EGLFS_KMS)