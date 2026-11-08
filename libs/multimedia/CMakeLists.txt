# firmir-multimedia: VideoOutput item + VideoSource with pluggable decode
# backends (plugins/multimedia/, firmir-mm-*.so, loaded at runtime).
add_library(firmir-multimedia SHARED
    src/gpuframe.cpp
    src/videotexture.cpp
    src/videosource.cpp
    src/videooutput.cpp
)

target_include_directories(firmir-multimedia
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmir-multimedia PUBLIC firmir-gui ${CMAKE_DL_LIBS})

# Installed binaries fall back to the system plugin directory when neither
# FIRMIR_MULTIMEDIA_PLUGIN_PATH nor a build-tree layout provides the plugins.
target_compile_definitions(firmir-multimedia PRIVATE
    FIRMIR_SYSTEM_PLUGIN_DIR="${FIRMIR_SYSTEM_PLUGIN_DIR}")

firmir_install_library(firmir-multimedia MODULE multimedia REQUIRES firmir-gui)

# The ffmpeg backend e2e (mediatest) is registered from
# plugins/multimedia/ once the ffmpeg plugin target exists.
