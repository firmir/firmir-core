# firmir-multimedia example: plays a video/audio file in a window through the
# first available VideoSource backend (gstreamer/ffmpeg). The UI is declared
# in player.fml (compiled to C++ by firmlc at build time).
if(NOT TARGET firmlc)
    message(FATAL_ERROR
        "the multimedia example requires FIRMIR_BUILD_TOOL_FIRML "
        "(the UI is written in Firml)")
endif()
include(${CMAKE_SOURCE_DIR}/tools/firml/cmake/FirmlCodegen.cmake)
firml_add_target(player_codegen
    SOURCES player.fml
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/libs/gui/include
                 ${CMAKE_SOURCE_DIR}/libs/multimedia/include)
# The generated C++ includes <firmir/items/VideoOutput.hpp>.
target_link_libraries(player_codegen PUBLIC firmir-multimedia)

add_executable(firmir-example-multimedia-video-player main.cpp)
target_link_libraries(firmir-example-multimedia-video-player
    PRIVATE player_codegen firmir-multimedia)
firmir_install_example(firmir-example-multimedia-video-player)
