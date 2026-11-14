# drm_egl buffer reader: binds the compositor's wl_display to EGL
# (eglBindWaylandDisplayWL) and imports client buffers as EGLImages
# (eglCreateImageKHR); wl_shm frames are read the same way the shm plugin does.
add_library(firmir-wayland-compositor-drm-egl SHARED drm_egl.cpp)
set_target_properties(firmir-wayland-compositor-drm-egl PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_DIR})
target_include_directories(firmir-wayland-compositor-drm-egl
    PRIVATE ${FIRMIR_WAYLAND_COMPOSITOR_EGL_INCLUDE_DIR}
            ${FIRMIR_WAYLAND_COMPOSITOR_GLES2_INCLUDE_DIR})
target_link_libraries(firmir-wayland-compositor-drm-egl
    PRIVATE firmir-wayland-compositor
            ${FIRMIR_WAYLAND_COMPOSITOR_EGL_LIBRARY}
            ${FIRMIR_WAYLAND_COMPOSITOR_GLES2_LIBRARY})
# The EGL display is created from a DRM render node through GBM (see the
# plugin's createDisplay()).
if(FIRMIR_WAYLAND_COMPOSITOR_GBM_FOUND)
    target_link_libraries(firmir-wayland-compositor-drm-egl
        PRIVATE PkgConfig::FIRMIR_WAYLAND_COMPOSITOR_GBM)
endif()
