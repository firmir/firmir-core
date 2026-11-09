add_executable(firmir-example-json-document main.cpp)
target_link_libraries(firmir-example-json-document PRIVATE firmir-core)
firmir_install_example(firmir-example-json-document)
