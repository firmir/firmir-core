# OpenGL ES 2 render backend: EGL pbuffer/surfaceless context, GLSL shaders,
# VBOs and FBO render targets. CPU readback implements CpuRenderTarget so GUI
# display backends can present GPU-rendered frames without native GL windows.
add_library(firmir-render-opengl SHARED opengl.cpp)
set_target_properties(firmir-render-opengl PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR})
target_include_directories(firmir-render-opengl PRIVATE ${EGL_INCLUDE_DIR} ${GLES2_INCLUDE_DIR})
target_link_libraries(firmir-render-opengl PRIVATE firmir-render ${EGL_LIBRARY} ${GLES2_LIBRARY})
# Optional: gbm platform display for DRM nodes on a bare console (see initDisplay).
if(FIRMIR_RENDER_GBM_FOUND)
    target_compile_definitions(firmir-render-opengl PRIVATE FIRMIR_RENDER_HAVE_GBM)
    target_link_libraries(firmir-render-opengl PRIVATE PkgConfig::FIRMIR_RENDER_GBM)
endif()
