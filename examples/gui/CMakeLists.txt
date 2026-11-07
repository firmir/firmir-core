# GUI examples. example_gui, calculator and views-menu declare their UI in
# Firml (.fml), compiled to C++ by firmlc at build time; controls-gallery is a
# hand-written C++ demo of firmir-gui-controls.
if(NOT TARGET firmlc)
    message(FATAL_ERROR
        "the GUI examples require FIRMIR_BUILD_TOOL_FIRML (the UI is written in Firml)")
endif()
include(${CMAKE_SOURCE_DIR}/tools/firml/cmake/FirmlCodegen.cmake)

add_subdirectory(example_gui)
add_subdirectory(calculator)
add_subdirectory(views-menu)
if(TARGET firmir-gui-controls)
    add_subdirectory(controls-gallery)
endif()
