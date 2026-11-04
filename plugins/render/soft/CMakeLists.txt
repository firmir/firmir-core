# Software render backend: CPU rasterizer executing the firmir-render command
# stream. Used by firmir-gui for window contents (default FIRMIR_RENDER_BACKEND
# for GUI) and by the render tests.
add_library(firmir-render-soft SHARED soft.cpp)
set_target_properties(firmir-render-soft PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR})
target_link_libraries(firmir-render-soft PRIVATE firmir-render)
