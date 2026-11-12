# Software render backend: CPU rasterizer executing the firmir-render command
# stream. Used by firmir-gui for window contents (default FIRMIR_RENDER_BACKEND
# for GUI) and by the render tests.
add_library(firmir-render-soft SHARED soft.cpp)
set_target_properties(firmir-render-soft PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR})
if(WIN32)
    # tests.exe links plugins directly; Windows resolves link-time DLLs from
    # the executable's directory (bin/), while runtime loading uses plugins/.
    add_custom_command(TARGET firmir-render-soft POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:firmir-render-soft> ${CMAKE_BINARY_DIR}/bin)
endif()
target_link_libraries(firmir-render-soft PRIVATE firmir-render)
