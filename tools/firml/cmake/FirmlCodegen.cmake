# Firml code generation: firml_add_target().
#
#   include(<firmir>/tools/firml/cmake/FirmlCodegen.cmake)
#   firml_add_target(my_codegen
#       SOURCES app.fml panel.fml
#       [TYPE STATIC|OBJECT]            # default STATIC
#       [INCLUDE_DIRS <dirs...>]        # extra -I dirs for .fml #include
#   )
#   target_link_libraries(my_app PRIVATE my_codegen)
#
# Each <stem>.fml is compiled with firmlc at build time into
# ${CMAKE_CURRENT_BINARY_DIR}/firml_generated/<stem>.hpp|.cpp; the generated
# directory is on the target's public include path (consumers write
# #include "<stem>.hpp"). The target links firmir-gui publicly.
#
# Requirements: the firmlc and firmir-gui targets must already exist (in-tree
# builds get them automatically; external projects add_subdirectory(firmir)).
# Stems must be unique per target. The default .fml include dir is
# libs/gui/include (the Firmir.Gui item headers, firmir/items/*.hpp).

function(firml_add_target target)
    cmake_parse_arguments(ARG "" "TYPE;ROOT_DIR" "SOURCES;INCLUDE_DIRS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "firml_add_target(${target}): SOURCES is required")
    endif()
    if(NOT TARGET firmlc)
        message(FATAL_ERROR
            "firml_add_target(${target}): the firmlc target is not available; "
            "add_subdirectory(tools/firml) first")
    endif()
    if(NOT TARGET firmir-gui)
        message(FATAL_ERROR
            "firml_add_target(${target}): the firmir-gui target is not available")
    endif()
    if(NOT ARG_TYPE)
        set(ARG_TYPE STATIC)
    endif()

    get_filename_component(FIRML_TOOLS_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    get_filename_component(FIRML_ROOT "${FIRML_TOOLS_DIR}/../.." ABSOLUTE)
    if(NOT ARG_INCLUDE_DIRS)
        set(ARG_INCLUDE_DIRS "${FIRML_ROOT}/libs/gui/include")
    endif()

    set(include_args)
    foreach(dir IN LISTS ARG_INCLUDE_DIRS)
        list(APPEND include_args -I "${dir}")
    endforeach()

    # .fml files change behavior when the item headers change: regenerate then.
    file(GLOB FIRML_ITEM_HEADERS CONFIGURE_DEPENDS
         "${FIRML_ROOT}/libs/gui/include/firmir/items/*.hpp")

    # Under ThreadSanitizer firmlc needs ASLR disabled ("setarch -R"), like the
    # other firml tests; in-tree builds export FIRML_TOOL_WRAPPER for that.
    separate_arguments(FIRML_WRAPPER_LIST UNIX_COMMAND "${FIRML_TOOL_WRAPPER}")

    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/firml_generated")
    set(gen_sources)
    set(gen_headers)
    if(ARG_ROOT_DIR)
        get_filename_component(root_dir "${ARG_ROOT_DIR}" ABSOLUTE)
        list(APPEND include_args --root-dir "${root_dir}")
    endif()
    foreach(fml IN LISTS ARG_SOURCES)
        get_filename_component(fml_abs "${fml}" ABSOLUTE)
        if(ARG_ROOT_DIR)
            file(RELATIVE_PATH stem "${root_dir}" "${fml_abs}")
            string(REGEX REPLACE "\.fml$" "" stem "${stem}")
            set(hpp "${gen_dir}/${stem}.h")
        else()
            get_filename_component(stem "${fml}" NAME_WE)
            set(hpp "${gen_dir}/${stem}.hpp")
        endif()
        set(cpp "${gen_dir}/${stem}.cpp")
        add_custom_command(
            OUTPUT "${hpp}" "${cpp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${gen_dir}"
            COMMAND ${FIRML_WRAPPER_LIST} $<TARGET_FILE:firmlc> "${fml_abs}" -o "${gen_dir}" ${include_args}
            DEPENDS firmlc "${fml_abs}" ${FIRML_ITEM_HEADERS}
            COMMENT "firmlc ${stem}.fml"
            VERBATIM)
        list(APPEND gen_sources "${cpp}")
        list(APPEND gen_headers "${hpp}")
    endforeach()

    add_library(${target} ${ARG_TYPE} ${gen_sources} ${gen_headers})
    target_link_libraries(${target} PUBLIC firmir-gui)
    target_include_directories(${target} PUBLIC "${gen_dir}")
    set_target_properties(${target} PROPERTIES FOLDER "firml")
    # Generated create*() functions are one big flat function per .fml file;
    # in debug builds GCC's variable tracking hits its limit ("variable
    # tracking size limit exceeded", a note that becomes noise next to real
    # warnings). Debug info for the generated code is of little use anyway.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-fno-var-tracking-assignments>)
    endif()
endfunction()
