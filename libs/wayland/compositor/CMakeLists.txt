if(NOT FIRMIR_BUILD_LIB_WAYLAND_COMPOSITOR)
    return()
endif()
set_source_files_properties(${XDG_PROTOCOL_C} PROPERTIES GENERATED TRUE)

set(VIEWPORTER_XML ${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml)
set(DECORATION_XML ${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
set(XDG_OUTPUT_XML ${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-output/xdg-output-unstable-v1.xml)
set(XDG_SERVER_H ${GENERATED_DIR}/xdg-shell-server-protocol.h)
set(VIEWPORTER_H ${GENERATED_DIR}/viewporter-server-protocol.h)
set(VIEWPORTER_C ${GENERATED_DIR}/viewporter-protocol.c)
set(DECORATION_H ${GENERATED_DIR}/xdg-decoration-unstable-v1-server-protocol.h)
set(DECORATION_C ${GENERATED_DIR}/xdg-decoration-unstable-v1-protocol.c)
set(XDG_OUTPUT_H ${GENERATED_DIR}/xdg-output-unstable-v1-server-protocol.h)
set(XDG_OUTPUT_C ${GENERATED_DIR}/xdg-output-unstable-v1-protocol.c)
set(XDG_SERVER_HPP ${GENERATED_DIR}/xdg_shell_server.hpp)
add_custom_command(
    OUTPUT ${XDG_SERVER_H} ${VIEWPORTER_H} ${VIEWPORTER_C}
           ${DECORATION_H} ${DECORATION_C} ${XDG_OUTPUT_H} ${XDG_OUTPUT_C} ${XDG_SERVER_HPP}
    COMMAND ${WAYLAND_SCANNER} server-header ${XDG_SHELL_XML} ${XDG_SERVER_H}
    COMMAND ${WAYLAND_SCANNER} server-header ${VIEWPORTER_XML} ${VIEWPORTER_H}
    COMMAND ${WAYLAND_SCANNER} private-code ${VIEWPORTER_XML} ${VIEWPORTER_C}
    COMMAND ${WAYLAND_SCANNER} server-header ${DECORATION_XML} ${DECORATION_H}
    COMMAND ${WAYLAND_SCANNER} private-code ${DECORATION_XML} ${DECORATION_C}
    COMMAND ${WAYLAND_SCANNER} server-header ${XDG_OUTPUT_XML} ${XDG_OUTPUT_H}
    COMMAND ${WAYLAND_SCANNER} private-code ${XDG_OUTPUT_XML} ${XDG_OUTPUT_C}
    COMMAND ${Python3_EXECUTABLE} ${GENERATOR} --mode server
            --protocol-header xdg-shell-server-protocol.h ${XDG_SHELL_XML} ${XDG_SERVER_HPP}
    DEPENDS ${XDG_SHELL_XML} ${VIEWPORTER_XML} ${DECORATION_XML} ${XDG_OUTPUT_XML} ${GENERATOR}
    VERBATIM)

add_library(firmir-wayland-compositor SHARED
    src/core.cpp src/surface.cpp src/input.cpp src/shell.cpp
    ${VIEWPORTER_C} ${DECORATION_C} ${XDG_OUTPUT_C}
    ${XDG_SERVER_HPP})
add_dependencies(firmir-wayland-compositor firmir-wayland-client-protocols)
target_include_directories(firmir-wayland-compositor PUBLIC include ${GENERATED_DIR}
    ${WAYLAND_SERVER_INCLUDE_DIRS})
target_link_libraries(firmir-wayland-compositor PUBLIC firmir-wayland-server firmir-gui
    firmir-wayland-protocols)
set_source_files_properties(${VIEWPORTER_C} ${DECORATION_C} ${XDG_OUTPUT_C}
    PROPERTIES COMPILE_OPTIONS "-w")
