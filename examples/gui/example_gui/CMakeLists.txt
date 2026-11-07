# The UI is declared in example_gui.fml; firmlc compiles it to C++ at build
# time (the generated library links firmir-gui publicly).
firml_add_target(example_gui_codegen SOURCES example_gui.fml)

add_executable(firmir-example-gui-animation main.cpp)
target_link_libraries(firmir-example-gui-animation PRIVATE example_gui_codegen)
firmir_install_example(firmir-example-gui-animation)
