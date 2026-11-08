# Decode backend plugins for firmir-multimedia. Each plugin builds into
# ${CMAKE_BINARY_DIR}/plugins/multimedia as firmir-mm-<name>.so (no "lib"
# prefix) so the base library finds it via FIRMIR_MULTIMEDIA_PLUGIN_PATH /
# the exe-relative search paths. A plugin compiles only when its dev
# libraries are present (pkg-config).
set(FIRMIR_MM_PLUGIN_DIR ${CMAKE_BINARY_DIR}/plugins/multimedia)

find_package(PkgConfig QUIET)
if(NOT PkgConfig_FOUND)
    message(STATUS "firmir-multimedia: pkg-config not found; no backend plugins")
    return()
endif()

# ------------------------------- gstreamer -------------------------------
pkg_check_modules(FIRMIR_MM_GST IMPORTED_TARGET
    gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
    gstreamer-allocators-1.0)
if(FIRMIR_MM_GST_FOUND)
    add_library(firmir-mm-gstreamer SHARED gstreamer/gstvideosource.cpp)
    set_target_properties(firmir-mm-gstreamer PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_MM_PLUGIN_DIR})
    target_link_libraries(firmir-mm-gstreamer
        PRIVATE firmir-multimedia PkgConfig::FIRMIR_MM_GST)
    firmir_install_plugin(firmir-mm-gstreamer multimedia)
    set(FIRMIR_MM_BACKENDS "gstreamer")
endif()

# -------------------------------- ffmpeg ---------------------------------
pkg_check_modules(FIRMIR_MM_FFMPEG GLOBAL IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)
if(FIRMIR_MM_FFMPEG_FOUND)
    add_library(firmir-mm-ffmpeg SHARED ffmpeg/ffmpegvideosource.cpp)
    set_target_properties(firmir-mm-ffmpeg PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_MM_PLUGIN_DIR})
    target_link_libraries(firmir-mm-ffmpeg
        PRIVATE firmir-multimedia PkgConfig::FIRMIR_MM_FFMPEG)
    firmir_install_plugin(firmir-mm-ffmpeg multimedia)

    # Zero-copy decode path: VAAPI surfaces exported as dma-buf. Compiles
    # only when libva + the drm variant are available.
    pkg_check_modules(FIRMIR_MM_VAAPI IMPORTED_TARGET libva libva-drm)
    if(FIRMIR_MM_VAAPI_FOUND)
        target_compile_definitions(firmir-mm-ffmpeg
            PRIVATE FIRMIR_MM_HAVE_VAAPI=1)
        target_link_libraries(firmir-mm-ffmpeg
            PRIVATE PkgConfig::FIRMIR_MM_VAAPI)
        string(APPEND FIRMIR_MM_BACKENDS " ffmpeg(+vaapi)")
    else()
        string(APPEND FIRMIR_MM_BACKENDS " ffmpeg")
        message(STATUS "firmir-multimedia: libva not found; the ffmpeg backend "
                       "builds without the VAAPI zero-copy path")
    endif()
endif()

# The backend e2e (mediatest) lives in tests/multimedia/ and is registered
# from tests/CMakeLists.txt (tests are added after this directory).

if(FIRMIR_MM_BACKENDS)
    message(STATUS "firmir-multimedia backends:${FIRMIR_MM_BACKENDS}")
else()
    message(STATUS "firmir-multimedia: no decode backends — install "
        "libgstreamer1.0-dev + libgstreamer-plugins-base1.0-dev and/or "
        "libavformat-dev libavcodec-dev libavutil-dev libswscale-dev")
endif()
