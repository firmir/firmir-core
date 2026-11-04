# Base GUI library: scene graph, rendering through firmir-render (the "soft"
# CPU backend by default), backend plugin loading. Deliberately does NOT link
# any display/input backend — those are dlopen()ed at runtime from
# FIRMIR_GUI_PLUGIN_PATH; render backends come from FIRMIR_RENDER_PLUGIN_PATH.
add_library(firmir-gui SHARED
    src/animation.cpp
    src/app.cpp
    src/button.cpp
    src/item.cpp
    src/painter.cpp
    src/platform.cpp
    src/rectangle.cpp
    src/screen.cpp
    src/text.cpp
    src/window.cpp
)
target_include_directories(firmir-gui PUBLIC include)
target_link_libraries(firmir-gui PUBLIC firmir-core PRIVATE firmir-render ${CMAKE_DL_LIBS})

# Header-only interface with the backend plugin ABI, for plugin targets that
# must not link the base library itself.
add_library(firmir-gui-backend INTERFACE)
target_include_directories(firmir-gui-backend INTERFACE include)
