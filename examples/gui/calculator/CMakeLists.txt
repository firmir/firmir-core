# The UI and the calculator logic are declared in calculator.fml; firmlc
# compiles them to C++ at build time (the generated library links firmir-gui
# publicly).
if(NOT TARGET firmlc)
    message(FATAL_ERROR
        "calculator requires FIRMIR_BUILD_TOOL_FIRML (the UI is written in Firml)")
endif()
include(${CMAKE_SOURCE_DIR}/tools/firml/cmake/FirmlCodegen.cmake)
firml_add_target(calculator_codegen SOURCES calculator.fml)

add_executable(firmir-example-gui-calculator main.cpp)
target_link_libraries(firmir-example-gui-calculator PRIVATE calculator_codegen)
firmir_install_example(firmir-example-gui-calculator)
