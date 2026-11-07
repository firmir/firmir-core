add_executable(firmir-example-core-basics main.cpp)
target_link_libraries(firmir-example-core-basics PRIVATE firmir-core)
firmir_install_example(firmir-example-core-basics)
