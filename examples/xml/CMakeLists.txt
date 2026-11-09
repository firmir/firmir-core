add_executable(firmir-example-xml-document main.cpp)
target_link_libraries(firmir-example-xml-document PRIVATE firmir-core)
firmir_install_example(firmir-example-xml-document)
