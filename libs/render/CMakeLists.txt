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

# Installed binaries fall back to the system plugin directory when neither
# FIRMIR_RENDER_PLUGIN_PATH nor a build-tree layout provides the plugins.
target_compile_definitions(firmir-render PRIVATE
    FIRMIR_SYSTEM_PLUGIN_DIR="${FIRMIR_SYSTEM_PLUGIN_DIR}/render")

firmir_install_library(firmir-render MODULE render REQUIRES firmir-core)
