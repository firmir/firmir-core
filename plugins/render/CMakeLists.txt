# Backend plugins for firmir-render. Each plugin builds into
# ${CMAKE_BINARY_DIR}/plugins/render as firmir-render-<name>.so (no "lib"
# prefix) so that the base library finds it via FIRMIR_RENDER_PLUGIN_PATH.
set(FIRMIR_RENDER_PLUGIN_DIR ${CMAKE_BINARY_DIR}/plugins/render)

if(FIRMIR_BUILD_PLUGIN_RENDER_NULL)
    add_subdirectory(null)
endif()
if(FIRMIR_BUILD_PLUGIN_RENDER_SOFT)
    add_subdirectory(soft)
endif()
if(FIRMIR_BUILD_PLUGIN_RENDER_OPENGL)
    find_path(EGL_INCLUDE_DIR EGL/egl.h)
    find_path(GLES2_INCLUDE_DIR GLES2/gl2.h)
    find_library(EGL_LIBRARY EGL)
    find_library(GLES2_LIBRARY GLESv2)
    if(EGL_INCLUDE_DIR AND GLES2_INCLUDE_DIR AND EGL_LIBRARY AND GLES2_LIBRARY)
        add_subdirectory(opengl)
    else()
        message(STATUS "EGL/GLES2 development files not found; skipping the OpenGL render plugin")
    endif()
endif()

# Real platform backends (opengl/vulkan/directx) belong here as
# plugins/render/<api>/ and must export firmir_render_create_backend().
