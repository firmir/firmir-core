# The Firmir Controls gallery: the UI is declared in Firml (gallery.fml,
# compiled by firmlc at build time); the demo logic lives in GalleryApp.
if(NOT TARGET firmlc)
    message(FATAL_ERROR
        "controls-gallery requires FIRMIR_BUILD_TOOL_FIRML (the UI is written in Firml)")
endif()
include(${CMAKE_SOURCE_DIR}/tools/firml/cmake/FirmlCodegen.cmake)
firml_add_target(gallery_codegen
    SOURCES gallery.fml
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_SOURCE_DIR}/libs/gui/include
                 ${CMAKE_SOURCE_DIR}/libs/gui-controls/include)
# The generated C++ includes the control wrappers and GalleryApp.hpp.
target_link_libraries(gallery_codegen PUBLIC firmir-gui-controls)
target_include_directories(gallery_codegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(firmir-example-gui-controls-gallery main.cpp GalleryApp.cpp)
target_link_libraries(firmir-example-gui-controls-gallery PRIVATE gallery_codegen)
if(TARGET firmir-gui-headless)
    target_link_libraries(firmir-example-gui-controls-gallery PRIVATE firmir-gui-headless)
    target_compile_definitions(firmir-example-gui-controls-gallery PRIVATE FIRMIR_DEMO_HEADLESS)
endif()
firmir_install_example(firmir-example-gui-controls-gallery)
