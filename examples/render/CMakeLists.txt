add_executable(firmir-example-render-triangle main.cpp)
target_link_libraries(firmir-example-render-triangle PRIVATE firmir-render)
firmir_install_example(firmir-example-render-triangle)
