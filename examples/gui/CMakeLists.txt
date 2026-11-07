# The UI is declared in example_gui.fml; firmlc compiles it to C++ at build
# time (the generated library links firmir-gui publicly).
if(NOT TARGET firmlc)
    message(FATAL_ERROR
        "example_gui requires FIRMIR_BUILD_TOOL_FIRML (the UI is written in Firml)")
endif()
include(${CMAKE_SOURCE_DIR}/tools/firml/cmake/FirmlCodegen.cmake)
firml_add_target(example_gui_codegen SOURCES example_gui.fml)

add_executable(example_gui main.cpp)
target_link_libraries(example_gui PRIVATE example_gui_codegen)
