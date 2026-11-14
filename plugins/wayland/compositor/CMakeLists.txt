# Compositor side of the Wayland buffer backends: how a client buffer becomes
# pixels the painter can draw. The loader (BufferBackends) tries drm_egl first
# and falls back to shm.
if(FIRMIR_BUILD_PLUGIN_WAYLAND_COMPOSITOR_SHM)
    add_subdirectory(shm)
endif()

if(FIRMIR_BUILD_PLUGIN_WAYLAND_COMPOSITOR_DRM_EGL)
    find_path(FIRMIR_WAYLAND_COMPOSITOR_EGL_INCLUDE_DIR EGL/egl.h)
    find_path(FIRMIR_WAYLAND_COMPOSITOR_GLES2_INCLUDE_DIR GLES2/gl2.h)
    find_library(FIRMIR_WAYLAND_COMPOSITOR_EGL_LIBRARY EGL)
    find_library(FIRMIR_WAYLAND_COMPOSITOR_GLES2_LIBRARY GLESv2)
    set(FIRMIR_WAYLAND_COMPOSITOR_GBM_FOUND FALSE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FIRMIR_WAYLAND_COMPOSITOR_GBM QUIET IMPORTED_TARGET gbm)
    endif()
    if(FIRMIR_WAYLAND_COMPOSITOR_EGL_INCLUDE_DIR
       AND FIRMIR_WAYLAND_COMPOSITOR_GLES2_INCLUDE_DIR
       AND FIRMIR_WAYLAND_COMPOSITOR_EGL_LIBRARY
       AND FIRMIR_WAYLAND_COMPOSITOR_GLES2_LIBRARY
       AND FIRMIR_WAYLAND_COMPOSITOR_GBM_FOUND)
        add_subdirectory(drm-egl)
    else()
        # EGL is initialised from a DRM render node (GBM), so gbm is required:
        # the platform cannot be initialised from the compositor's own display.
        message(STATUS "EGL/GLES2/gbm development files not found; skipping the "
            "drm_egl Wayland compositor backend")
    endif()
endif()

# Installed compositor backends live in lib/firmir/plugins/wayland/compositor/.
foreach(backend shm drm-egl)
    if(TARGET firmir-wayland-compositor-${backend})
        firmir_install_plugin(firmir-wayland-compositor-${backend} wayland SUBDIR compositor)
    endif()
endforeach()
