# Backend plugins for firmir-gui. Each plugin builds into
# ${CMAKE_BINARY_DIR}/plugins as firmir-gui-<name>.so (no "lib" prefix) so
# that the base library finds it via FIRMIR_GUI_PLUGIN_PATH.
set(FIRMIR_GUI_PLUGIN_DIR ${CMAKE_BINARY_DIR}/plugins)

if(FIRMIR_BUILD_PLUGIN_GUI_HEADLESS)
    add_subdirectory(headless)
endif()

include(${PROJECT_SOURCE_DIR}/cmake/FirmirHeaders.cmake)

# Older revisions probed the headers with check_include_file_cxx(), which stores
# the verdict in CMakeCache.txt: drop those entries so a build directory that
# was configured before libdrm-devel was installed picks the headers up.
unset(FIRMIR_HAVE_DRM_H CACHE)
unset(FIRMIR_EGLFS_HAVE_DRM_H CACHE)
unset(FIRMIR_HAVE_LINUX_INPUT_H CACHE)

if(FIRMIR_BUILD_PLUGIN_GUI_EVDEV)
    firmir_find_headers(FIRMIR_HAVE_LINUX_INPUT_H FIRMIR_LINUX_INPUT_INCLUDE_DIR
        HEADERS linux/input.h)
    if(FIRMIR_HAVE_LINUX_INPUT_H)
        if(FIRMIR_LINUX_INPUT_INCLUDE_DIR)
            message(STATUS "linux/input.h: found in ${FIRMIR_LINUX_INPUT_INCLUDE_DIR}")
        else()
            message(STATUS "linux/input.h: found in the default include path")
        endif()
        add_subdirectory(evdev)
    else()
        message(STATUS "linux/input.h not found, skipping the evdev backend")
    endif()
endif()

# <drm/drm.h> + <drm/drm_mode.h> are required by the drm backend and by the
# eglfs KMS integration. FIRMIR_DRM_HEADERS=on forces the verdict for setups
# where the configure step cannot see the target's include tree (some
# scratchbox2/cross layouts), =off skips both backends.
set(FIRMIR_DRM_HEADERS "auto" CACHE STRING
    "DRM/KMS headers: auto (look for <drm/drm.h>), on (assume present), off (skip)")
set_property(CACHE FIRMIR_DRM_HEADERS PROPERTY STRINGS auto on off)
if(FIRMIR_DRM_HEADERS STREQUAL "on")
    set(FIRMIR_HAVE_DRM_H TRUE)
    set(FIRMIR_DRM_INCLUDE_DIR "")
    message(STATUS "drm/drm.h: assumed present (FIRMIR_DRM_HEADERS=on)")
elseif(FIRMIR_DRM_HEADERS STREQUAL "off")
    set(FIRMIR_HAVE_DRM_H FALSE)
    set(FIRMIR_DRM_INCLUDE_DIR "")
    message(STATUS "drm/drm.h: assumed absent (FIRMIR_DRM_HEADERS=off)")
else()
    firmir_find_headers(FIRMIR_HAVE_DRM_H FIRMIR_DRM_INCLUDE_DIR
        HEADERS drm/drm.h drm/drm_mode.h PACKAGES libdrm)
    if(FIRMIR_HAVE_DRM_H)
        if(FIRMIR_DRM_INCLUDE_DIR)
            message(STATUS "drm/drm.h: found in ${FIRMIR_DRM_INCLUDE_DIR}")
        else()
            message(STATUS "drm/drm.h: found in the default include path")
        endif()
    endif()
endif()

if(FIRMIR_BUILD_PLUGIN_GUI_DRM)
    if(FIRMIR_HAVE_DRM_H)
        add_subdirectory(drm)
    else()
        message(STATUS "drm/drm.h not found, skipping the drm backend "
            "(set -DFIRMIR_DRM_HEADERS=on to force it)")
    endif()
endif()
if(FIRMIR_BUILD_PLUGIN_GUI_EGLFS)
    add_subdirectory(eglfs)
endif()
if(FIRMIR_BUILD_PLUGIN_GUI_WAYLAND)
    if(TARGET firmir-wayland-client)
        add_subdirectory(wayland)
    else()
        message(STATUS "firmir-wayland-client is unavailable; skipping the wayland backend")
    endif()
endif()

# Installed GUI backends live in lib/firmir/plugins/gui/.
foreach(backend headless evdev drm wayland eglfs)
    if(TARGET firmir-gui-${backend})
        firmir_install_plugin(firmir-gui-${backend} gui)
    endif()
endforeach()

# eglfs device integrations (sub-plugins) live one level deeper, in
# lib/firmir/plugins/gui/eglfs/.
if(TARGET firmir-gui-eglfs-kms)
    firmir_install_plugin(firmir-gui-eglfs-kms gui SUBDIR eglfs)
endif()
