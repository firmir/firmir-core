# Base render library: portable drawing-primitive abstraction and backend
# plugin loading. Concrete GPU backends live in plugins/render and are
# dlopen()ed at runtime from FIRMIR_RENDER_PLUGIN_PATH.
add_library(firmir-render SHARED
    src/render.cpp
)

target_include_directories(firmir-render
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmir-render PUBLIC firmir-core PRIVATE ${CMAKE_DL_LIBS})
