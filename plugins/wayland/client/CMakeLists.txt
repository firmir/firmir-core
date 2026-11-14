# Client side of the Wayland buffer backends: what a firmir-wayland-client
# process allocates its frames from. The loader (Client::Backends) tries
# drm_egl first and falls back to shm.
if(FIRMIR_BUILD_PLUGIN_WAYLAND_CLIENT_SHM)
    add_subdirectory(shm)
endif()

if(FIRMIR_BUILD_PLUGIN_WAYLAND_CLIENT_DRM_EGL)
    find_path(FIRMIR_WAYLAND_CLIENT_EGL_INCLUDE_DIR EGL/egl.h)
    find_library(FIRMIR_WAYLAND_CLIENT_EGL_LIBRARY EGL)
    if(FIRMIR_WAYLAND_CLIENT_EGL_INCLUDE_DIR AND FIRMIR_WAYLAND_CLIENT_EGL_LIBRARY)
        add_subdirectory(drm-egl)
    else()
        message(STATUS "EGL development files not found; skipping the drm_egl "
            "Wayland client backend")
    endif()
endif()

# Installed client backends live in lib/firmir/plugins/wayland/client/.
foreach(backend shm drm-egl)
    if(TARGET firmir-wayland-client-${backend})
        firmir_install_plugin(firmir-wayland-client-${backend} wayland SUBDIR client)
    endif()
endforeach()
