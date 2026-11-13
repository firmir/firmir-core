find_package(PkgConfig REQUIRED)
enable_language(C)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_CORE_DIR wayland-client pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

if(NOT WAYLAND_CORE_DIR)
    set(WAYLAND_CORE_DIR /usr/share/wayland)
endif()
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(WAYLAND_XML ${WAYLAND_CORE_DIR}/wayland.xml)
set(XDG_SHELL_XML ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
set(CORE_CLIENT_HPP ${GENERATED_DIR}/core_client.hpp)
set(XDG_CLIENT_HPP ${GENERATED_DIR}/xdg_shell_client.hpp)
set(XDG_CLIENT_H ${GENERATED_DIR}/xdg-shell-client-protocol.h)
set(XDG_PROTOCOL_C ${GENERATED_DIR}/xdg-shell-protocol.c)
# Under ThreadSanitizer instrumented host tools need ASLR disabled
# ("setarch -R"), like the firmlc build-time codegen.
separate_arguments(FIRML_TOOL_WRAPPER_LIST UNIX_COMMAND "${FIRML_TOOL_WRAPPER}")
add_custom_command(
    OUTPUT ${CORE_CLIENT_HPP} ${XDG_CLIENT_HPP} ${XDG_CLIENT_H} ${XDG_PROTOCOL_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${CMAKE_COMMAND} -E env
            LD_LIBRARY_PATH=$<TARGET_FILE_DIR:firmir-core>:$ENV{LD_LIBRARY_PATH}
            ${FIRML_TOOL_WRAPPER_LIST} $<TARGET_FILE:firmir-wayland-generator>
            --mode client
            --protocol-header wayland-client-protocol.h ${WAYLAND_XML} ${CORE_CLIENT_HPP}
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${XDG_CLIENT_H}
    COMMAND ${WAYLAND_SCANNER} public-code ${XDG_SHELL_XML} ${XDG_PROTOCOL_C}
    COMMAND ${CMAKE_COMMAND} -E env
            LD_LIBRARY_PATH=$<TARGET_FILE_DIR:firmir-core>:$ENV{LD_LIBRARY_PATH}
            ${FIRML_TOOL_WRAPPER_LIST} $<TARGET_FILE:firmir-wayland-generator>
            --mode client
            --protocol-header xdg-shell-client-protocol.h ${XDG_SHELL_XML} ${XDG_CLIENT_HPP}
    DEPENDS firmir-wayland-generator ${WAYLAND_XML} ${XDG_SHELL_XML}
    VERBATIM)

add_library(firmir-wayland-protocols SHARED ${XDG_PROTOCOL_C})
add_dependencies(firmir-wayland-protocols firmir-wayland-client-protocols)
target_include_directories(firmir-wayland-protocols PUBLIC ${GENERATED_DIR}
    ${WAYLAND_CLIENT_INCLUDE_DIRS})
target_link_libraries(firmir-wayland-protocols PUBLIC ${WAYLAND_CLIENT_LIBRARIES})

add_library(firmir-wayland-client SHARED client/src/client.cpp
    ${CORE_CLIENT_HPP} ${XDG_CLIENT_HPP} ${XDG_CLIENT_H})
add_custom_target(firmir-wayland-client-protocols
    DEPENDS ${CORE_CLIENT_HPP} ${XDG_CLIENT_HPP} ${XDG_CLIENT_H} ${XDG_PROTOCOL_C})
add_dependencies(firmir-wayland-client firmir-wayland-client-protocols)
target_include_directories(firmir-wayland-client PUBLIC client/include ${GENERATED_DIR}
    ${WAYLAND_CLIENT_INCLUDE_DIRS})
target_link_directories(firmir-wayland-client PUBLIC ${WAYLAND_CLIENT_LIBRARY_DIRS})
target_link_libraries(firmir-wayland-client PUBLIC ${WAYLAND_CLIENT_LIBRARIES}
    firmir-wayland-protocols)

add_library(firmir-wayland-server SHARED server/src/server.cpp)
target_include_directories(firmir-wayland-server PUBLIC server/include
    ${WAYLAND_SERVER_INCLUDE_DIRS})
target_link_directories(firmir-wayland-server PUBLIC ${WAYLAND_SERVER_LIBRARY_DIRS})
target_link_libraries(firmir-wayland-server PUBLIC ${WAYLAND_SERVER_LIBRARIES})

add_library(firmir-wayland INTERFACE)
target_link_libraries(firmir-wayland INTERFACE firmir-wayland-client firmir-wayland-server)

firmir_install_library(firmir-wayland-protocols MODULE wayland-protocols
    HEADERS "" REQUIRES wayland-client)
firmir_install_library(firmir-wayland-client MODULE wayland-client
    HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/client/include
    REQUIRES firmir-wayland-protocols wayland-client)
firmir_install_library(firmir-wayland-server MODULE wayland-server
    HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    REQUIRES wayland-server)

if(TARGET firmir-gui)
    add_subdirectory(compositor)
    target_link_libraries(firmir-wayland INTERFACE firmir-wayland-compositor)
endif()

set_source_files_properties(${XDG_PROTOCOL_C} PROPERTIES COMPILE_OPTIONS "-w")
