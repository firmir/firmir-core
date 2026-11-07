# The Firmir Controls gallery: a hand-written C++ demo walking through the
# firmir-gui-controls widgets (menu + one page per control).
add_executable(firmir-example-gui-controls-gallery gallery.cpp)
target_link_libraries(firmir-example-gui-controls-gallery PRIVATE firmir-gui-controls)
if(TARGET firmir-gui-headless)
    target_link_libraries(firmir-example-gui-controls-gallery PRIVATE firmir-gui-headless)
    target_compile_definitions(firmir-example-gui-controls-gallery PRIVATE FIRMIR_DEMO_HEADLESS)
endif()
firmir_install_example(firmir-example-gui-controls-gallery)
