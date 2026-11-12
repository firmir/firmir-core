# Base GUI library: scene graph, rendering through firmir-render (the "soft"
# CPU backend by default), backend plugin loading. Deliberately does NOT link
# any display/input backend — those are dlopen()ed at runtime from
# FIRMIR_GUI_PLUGIN_PATH; render backends come from FIRMIR_RENDER_PLUGIN_PATH.
add_library(firmir-gui SHARED
    src/animation.cpp
    src/app.cpp
    src/button.cpp
    src/font.cpp
    src/flickable.cpp
    src/image.cpp
    src/item.cpp
    src/listmodel.cpp
    src/editabletext.cpp
    src/mousearea.cpp
    src/painter.cpp
    src/platform.cpp
    src/positioners.cpp
    src/repeater.cpp
    src/rectangle.cpp
    src/screen.cpp
    src/shadereffect.cpp
    src/text.cpp
    src/theme.cpp
    src/window.cpp
)
target_include_directories(firmir-gui PUBLIC include)
# firmir-render is PUBLIC: painter.hpp exposes Render::DmaBufTextureDesc
# (zero-copy video frames) in its public API.
target_link_libraries(firmir-gui PUBLIC firmir-core firmir-render
    PRIVATE ${CMAKE_DL_LIBS})
if(FIRMIR_BUILD_SYSTEM STREQUAL "windows")
    # No pkg-config/fontconfig on Windows: vcpkg packages with CMake configs.
    # Font enumeration is done by scanning the Windows font directories.
    find_package(Freetype REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
    target_link_libraries(firmir-gui PRIVATE Freetype::Freetype harfbuzz::harfbuzz)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FIRMIR_FONT REQUIRED freetype2 fontconfig harfbuzz)
    target_include_directories(firmir-gui PRIVATE ${FIRMIR_FONT_INCLUDE_DIRS})
    target_link_directories(firmir-gui PRIVATE ${FIRMIR_FONT_LIBRARY_DIRS})
    target_link_libraries(firmir-gui PRIVATE ${FIRMIR_FONT_LIBRARIES})
endif()
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
target_link_libraries(firmir-gui PRIVATE PNG::PNG JPEG::JPEG)
find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.hpp)
find_library(SHADERC_SHARED_LIBRARY shaderc_shared)
if(SHADERC_INCLUDE_DIR AND SHADERC_SHARED_LIBRARY)
    target_include_directories(firmir-gui PRIVATE ${SHADERC_INCLUDE_DIR})
    target_link_libraries(firmir-gui PRIVATE ${SHADERC_SHARED_LIBRARY})
    target_compile_definitions(firmir-gui PRIVATE FIRMIR_HAVE_SHADERC)
endif()

# Header-only interface with the backend plugin ABI, for plugin targets that
# must not link the base library itself.
add_library(firmir-gui-backend INTERFACE)
target_include_directories(firmir-gui-backend INTERFACE include)

# Installed binaries fall back to the system plugin directory when neither
# FIRMIR_GUI_PLUGIN_PATH nor a build-tree layout provides the plugins.
target_compile_definitions(firmir-gui PRIVATE
    FIRMIR_SYSTEM_PLUGIN_DIR="${FIRMIR_SYSTEM_PLUGIN_DIR}/gui")

firmir_install_library(firmir-gui MODULE gui REQUIRES firmir-core firmir-render)
