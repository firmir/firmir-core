# Null render backend: validates and records render commands without a GPU.
# Used by tests and as the default fallback backend.
add_library(firmir-render-null SHARED null.cpp)
set_target_properties(firmir-render-null PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR})
target_link_libraries(firmir-render-null PRIVATE firmir-render)
target_include_directories(firmir-render-null PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
