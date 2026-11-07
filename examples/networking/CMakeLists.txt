add_executable(firmir-example-networking-fetch main.cpp)
target_link_libraries(firmir-example-networking-fetch PRIVATE firmir-networking)
firmir_install_example(firmir-example-networking-fetch)
