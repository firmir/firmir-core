# drm_egl client backend: GPU buffers handed over as dma-bufs. The plugin
# probes EGL and the DRM render node; the dma-buf transport (linux_dmabuf_v1)
# lands later, until then the probe declines and the loader uses shm.
add_library(firmir-wayland-client-drm-egl SHARED drm_egl.cpp)
set_target_properties(firmir-wayland-client-drm-egl PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_CLIENT_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_CLIENT_PLUGIN_DIR})
target_include_directories(firmir-wayland-client-drm-egl
    PRIVATE ${FIRMIR_WAYLAND_CLIENT_EGL_INCLUDE_DIR})
target_link_libraries(firmir-wayland-client-drm-egl
    PRIVATE firmir-wayland-client ${FIRMIR_WAYLAND_CLIENT_EGL_LIBRARY})

# GBM allocates the buffers from a DRM render node; without it the probe can
# still report why the backend is unavailable, so the dependency is optional.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FIRMIR_WAYLAND_CLIENT_GBM QUIET IMPORTED_TARGET gbm)
endif()
if(FIRMIR_WAYLAND_CLIENT_GBM_FOUND)
    target_compile_definitions(firmir-wayland-client-drm-egl
        PRIVATE FIRMIR_WAYLAND_CLIENT_HAVE_GBM)
    target_link_libraries(firmir-wayland-client-drm-egl
        PRIVATE PkgConfig::FIRMIR_WAYLAND_CLIENT_GBM)
endif()
