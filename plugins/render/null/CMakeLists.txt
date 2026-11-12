# Null render backend: validates and records render commands without a GPU.
# Used by tests and as the default fallback backend.
add_library(firmir-render-null SHARED null.cpp)
set_target_properties(firmir-render-null PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR})
if(WIN32)
    # tests.exe links plugins directly; Windows resolves link-time DLLs from
    # the executable's directory (bin/), while runtime loading uses plugins/.
    add_custom_command(TARGET firmir-render-null POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:firmir-render-null> ${CMAKE_BINARY_DIR}/bin)
endif()
target_link_libraries(firmir-render-null PRIVATE firmir-render)
target_include_directories(firmir-render-null PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
