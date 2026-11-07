# The UI is declared in example_gui.fml; firmlc compiles it to C++ at build
# time (the generated library links firmir-gui publicly).
if(NOT TARGET firmlc)
    message(FATAL_ERROR
        "the animation example requires FIRMIR_BUILD_TOOL_FIRML (the UI is written in Firml)")
endif()
include(${CMAKE_SOURCE_DIR}/tools/firml/cmake/FirmlCodegen.cmake)
firml_add_target(example_gui_codegen SOURCES example_gui.fml)

add_executable(firmir-example-gui-animation main.cpp)
target_link_libraries(firmir-example-gui-animation PRIVATE example_gui_codegen)
firmir_install_example(firmir-example-gui-animation)

add_subdirectory(calculator)
