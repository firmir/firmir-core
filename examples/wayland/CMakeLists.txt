add_executable(firmir-example-wayland-compositor compositor.cpp)
target_link_libraries(firmir-example-wayland-compositor PRIVATE firmir-wayland-compositor)
firmir_install_example(firmir-example-wayland-compositor)
