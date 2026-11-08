# Firml code generation for applications built against an *installed* firmir
# tree (the in-tree equivalent is tools/firml/cmake/FirmlCodegen.cmake).
#
#   pkg_get_variable(FIRMIR_PREFIX firmir-gui prefix)
#   include(${FIRMIR_PREFIX}/share/firmir/cmake/FirmirFirml.cmake)
#   firmir_add_fml(my_codegen SOURCES app.fml [INCLUDE_DIRS <dirs...>])
#   target_link_libraries(my_app PRIVATE my_codegen)
#
# Uses ${FIRMIR_PREFIX}/bin/firmlc and the installed headers under
# ${FIRMIR_PREFIX}/include. The generated directory is on the target's public
# include path (consumers write #include "<stem>.hpp").

if(NOT DEFINED FIRMIR_PREFIX)
    # Allow inclusion without pkg-config: derive the prefix from this file's
    # location (<prefix>/share/firmir/cmake/FirmirFirml.cmake).
    get_filename_component(FIRMIR_PREFIX
                           "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
endif()

function(firmir_add_fml target)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDE_DIRS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "firmir_add_fml(${target}): SOURCES is required")
    endif()

    set(firmlc "${FIRMIR_PREFIX}/bin/firmlc")
    if(NOT EXISTS "${firmlc}")
        message(FATAL_ERROR "firmir_add_fml: firmlc not found at ${firmlc}")
    endif()

    set(include_args -I "${FIRMIR_PREFIX}/include")
    foreach(dir IN LISTS ARG_INCLUDE_DIRS)
        list(APPEND include_args -I "${dir}")
    endforeach()

    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/firml_generated")
    set(gen_sources)
    set(gen_headers)
    foreach(fml IN LISTS ARG_SOURCES)
        get_filename_component(fml_abs "${fml}" ABSOLUTE)
        get_filename_component(stem "${fml}" NAME_WE)
        set(hpp "${gen_dir}/${stem}.hpp")
        set(cpp "${gen_dir}/${stem}.cpp")
        add_custom_command(
            OUTPUT "${hpp}" "${cpp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${gen_dir}"
            COMMAND "${firmlc}" "${fml_abs}" -o "${gen_dir}" ${include_args}
            DEPENDS "${fml_abs}" "${firmlc}"
            COMMENT "firmlc ${stem}.fml"
            VERBATIM)
        list(APPEND gen_sources "${cpp}")
        list(APPEND gen_headers "${hpp}")
    endforeach()

    add_library(${target} STATIC ${gen_sources} ${gen_headers})
    target_include_directories(${target} PUBLIC "${gen_dir}")
    # App-local headers referenced from .fml files (opaque types).
    target_include_directories(${target} PUBLIC ${ARG_INCLUDE_DIRS})
    # The generated code includes <firmir/...> headers and links firmir-gui.
    target_include_directories(${target} PUBLIC "${FIRMIR_PREFIX}/include")
    target_link_directories(${target} PUBLIC "${FIRMIR_PREFIX}/lib")
    target_link_libraries(${target} PUBLIC firmir-gui)
    set_target_properties(${target} PROPERTIES FOLDER "firml")
endfunction()
