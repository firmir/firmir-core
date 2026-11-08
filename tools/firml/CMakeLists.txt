# firmlc — Firml (.fml) to C++ transpiler. Hand-written lexer and recursive
# descent parser; no external tools or libraries, only the C++17 standard
# library (milestone M1: parser + AST dump).
add_executable(firmlc
    src/codegen.cpp
    src/dump.cpp
    src/lexer.cpp
    src/main.cpp
    src/parser.cpp
    src/sema.cpp
    src/types.cpp
)
target_include_directories(firmlc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The transpiler and the installed-tree codegen module are part of the SDK:
# external applications compile their .fml UIs with them.
install(TARGETS firmlc RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/cmake/FirmirFirmlInstalled.cmake
        DESTINATION share/firmir/cmake
        RENAME FirmirFirml.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FirmlCodegen.cmake)

# Snapshot tests: run `firmlc --dump-ast` on each fixture in testdata/ and
# compare stdout with <case>.ast (success) or stderr with <case>.err
# (expected parse error). No test framework — ctest + a CMake driver script.
if(FIRMIR_BUILD_TESTS)
    # FIRML_TOOL_WRAPPER (the TSan setarch prefix) is set at the root so that
    # firml_add_target() callers in other directories see it too.

    # Parser-only snapshots (no include resolution needed).
    set(FIRML_AST_CASES
        01_minimal
        02_include
        03_nested
        04_expressions
        05_ternary
        06_anchors_block
        07_anchors_shorthand
        08_behavior
        09_strings_bools
        10_builtins
        11_full
        12_comments_semicolons
        13_dotted_types
        40_handlers
        50_window_timer
        60_custom_props
        70_positioners
        71_repeater
        80_rectangle_border
        81_connections_binding
        err_bad_token
        err_two_roots
        err_unclosed_brace
    )
    # Semantic snapshots (run with -I testdata/include; includes resolved).
    set(FIRML_SEMA_CASES
        01_minimal
        03_nested
        04_expressions
        05_ternary
        06_anchors_block
        08_behavior
        11_full
        12_comments_semicolons
        13_dotted_types
        20_sema_basic
        21_sema_parent_refs
        22_sema_sibling_anchor
        23_unused_include
        24_quoted_include
        40_handlers
        50_window_timer
        60_custom_props
        70_positioners
        71_repeater
        80_rectangle_border
        81_connections_binding
        err_sema_anchor_axis_conflict
        err_sema_anchor_center_conflict
        err_sema_anchor_fill_conflict
        err_sema_anchor_late_sibling
        err_sema_anchor_overconstrained
        err_sema_animation_type
        err_sema_assign_readonly
        err_sema_assign_unknown
        err_sema_bad_easing
        err_sema_behavior_bool
        err_sema_color_on_number
        err_sema_cycle_anchor
        err_sema_cycle_binding
        err_sema_delegate_id
        err_sema_dup_id
        err_sema_handler_property_lowercase
        err_sema_include_not_found
        err_sema_listelement_literal
        err_sema_listelement_placement
        err_sema_missing_type_include
        err_sema_model_not_model
        err_sema_model_unknown_role
        err_sema_nested_repeater
        err_sema_repeater_delegates
        err_sema_repeater_no_model
        err_sema_binding_readonly
        err_sema_binding_unknown_property
        err_sema_concat_bool
        err_sema_connections_no_target
        err_sema_connections_unknown_target
        err_sema_property_builtin
        err_sema_property_dup
        err_sema_property_type
        err_sema_timer_binding
        err_sema_timer_children
        err_sema_type_mismatch
        err_sema_undefined_property
        err_sema_unknown_function
        err_sema_unknown_identifier
        err_sema_unknown_property
        err_sema_window_anchor
        err_sema_window_not_root
    )
    foreach(case IN LISTS FIRML_AST_CASES)
        add_test(
            NAME firmlc.${case}
            COMMAND ${CMAKE_COMMAND}
                "-DTOOL_WRAPPER=${FIRML_TOOL_WRAPPER}"
                -DTOOL=$<TARGET_FILE:firmlc>
                -DCASE=${case}
                -DMODE=ast
                -DTESTDATA=${CMAKE_CURRENT_SOURCE_DIR}/testdata
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-out
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
        )
    endforeach()
    foreach(case IN LISTS FIRML_SEMA_CASES)
        add_test(
            NAME firmlc.sema.${case}
            COMMAND ${CMAKE_COMMAND}
                "-DTOOL_WRAPPER=${FIRML_TOOL_WRAPPER}"
                -DTOOL=$<TARGET_FILE:firmlc>
                -DCASE=${case}
                -DMODE=sema
                -DTESTDATA=${CMAKE_CURRENT_SOURCE_DIR}/testdata
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-out
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
        )
    endforeach()

    # Code generation snapshots (run with -I testdata/include).
    set(FIRML_GEN_CASES
        30_codegen_basic
        31_codegen_expressions
        32_codegen_opaque
        33_codegen_bindings
        34_codegen_behavior
        40_handlers
        50_window_timer
        60_custom_props
        70_positioners
        71_repeater
        80_rectangle_border
        81_connections_binding
    )
    foreach(case IN LISTS FIRML_GEN_CASES)
        add_test(
            NAME firmlc.gen.${case}
            COMMAND ${CMAKE_COMMAND}
                "-DTOOL_WRAPPER=${FIRML_TOOL_WRAPPER}"
                -DTOOL=$<TARGET_FILE:firmlc>
                -DCASE=${case}
                -DMODE=gen
                -DTESTDATA=${CMAKE_CURRENT_SOURCE_DIR}/testdata
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-out
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
        )
    endforeach()

    # End-to-end: generate C++ from a .fml file, compile it against the real
    # firmir-gui and run the result (checks anchors react to live changes).
    if(FIRMIR_BUILD_LIB_GUI AND TARGET firmir-gui)
        # FIRML_TOOL_WRAPPER is a plain string ("setarch -R" under TSan);
        # expand it into separate command arguments here.
        separate_arguments(FIRML_TOOL_WRAPPER_LIST UNIX_COMMAND "${FIRML_TOOL_WRAPPER}")
        set(E2E_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/e2e-gen)
        add_custom_command(
            OUTPUT ${E2E_GEN_DIR}/app.hpp ${E2E_GEN_DIR}/app.cpp
            COMMAND ${FIRML_TOOL_WRAPPER_LIST} $<TARGET_FILE:firmlc>
                    ${CMAKE_CURRENT_SOURCE_DIR}/testdata/e2e/app.fml
                    -I ${CMAKE_SOURCE_DIR}/libs/gui/include
                    -o ${E2E_GEN_DIR}
            DEPENDS firmlc ${CMAKE_CURRENT_SOURCE_DIR}/testdata/e2e/app.fml
            VERBATIM
        )
        add_executable(firml_e2e_app
            testdata/e2e/main.cpp
            ${E2E_GEN_DIR}/app.cpp
        )
        target_include_directories(firml_e2e_app PRIVATE ${E2E_GEN_DIR})
        target_link_libraries(firml_e2e_app PRIVATE firmir-gui)
        if(FIRMIR_SANITIZER STREQUAL "thread")
            add_test(NAME firmlc.e2e.app COMMAND setarch -R $<TARGET_FILE:firml_e2e_app>)
        else()
            add_test(NAME firmlc.e2e.app COMMAND $<TARGET_FILE:firml_e2e_app>)
        endif()

        # Repeater/ListModel/Grid: delegate instantiation against the real
        # firmir-gui, including sparse roles and the row index in handlers.
        add_custom_command(
            OUTPUT ${E2E_GEN_DIR}/repeater.hpp ${E2E_GEN_DIR}/repeater.cpp
            COMMAND ${FIRML_TOOL_WRAPPER_LIST} $<TARGET_FILE:firmlc>
                    ${CMAKE_CURRENT_SOURCE_DIR}/testdata/e2e/repeater.fml
                    -I ${CMAKE_SOURCE_DIR}/libs/gui/include
                    -o ${E2E_GEN_DIR}
            DEPENDS firmlc ${CMAKE_CURRENT_SOURCE_DIR}/testdata/e2e/repeater.fml
            VERBATIM
        )
        add_executable(firml_e2e_repeater
            testdata/e2e/repeater_main.cpp
            ${E2E_GEN_DIR}/repeater.cpp
        )
        target_include_directories(firml_e2e_repeater PRIVATE ${E2E_GEN_DIR})
        target_link_libraries(firml_e2e_repeater PRIVATE firmir-gui)
        if(FIRMIR_SANITIZER STREQUAL "thread")
            add_test(NAME firmlc.e2e.repeater COMMAND setarch -R $<TARGET_FILE:firml_e2e_repeater>)
        else()
            add_test(NAME firmlc.e2e.repeater COMMAND $<TARGET_FILE:firml_e2e_repeater>)
        endif()

        # Generated signal/property handlers + singleton + Button, exercised
        # through the real headless input/render stack.
        if(TARGET firmir-gui-headless)
            firml_add_target(firmlc_handlers_codegen
                ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/testdata/e2e
                SOURCES testdata/e2e/handlers.fml)
            add_executable(firmlc_handlers_app testdata/e2e/handlers_main.cpp)
            target_link_libraries(firmlc_handlers_app PRIVATE
                firmlc_handlers_codegen firmir-gui-headless)
            if(FIRMIR_SANITIZER STREQUAL "thread")
                add_test(NAME firmlc.handlers.app
                         COMMAND setarch -R $<TARGET_FILE:firmlc_handlers_app>)
            else()
                add_test(NAME firmlc.handlers.app
                         COMMAND $<TARGET_FILE:firmlc_handlers_app>)
            endif()

            # QML-style Connections/Binding attached to a target object.
            firml_add_target(firmlc_connections_codegen
                ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/testdata/e2e
                SOURCES testdata/e2e/connections.fml)
            add_executable(firmlc_connections_app testdata/e2e/connections_main.cpp)
            target_link_libraries(firmlc_connections_app PRIVATE
                firmlc_connections_codegen firmir-gui-headless)
            if(FIRMIR_SANITIZER STREQUAL "thread")
                add_test(NAME firmlc.connections.app
                         COMMAND setarch -R $<TARGET_FILE:firmlc_connections_app>)
            else()
                add_test(NAME firmlc.connections.app
                         COMMAND $<TARGET_FILE:firmlc_connections_app>)
            endif()
        endif()

        # M8 e2e: Window root, App singleton auto-registration, Timer-driven
        # handler assignments and behavior animations on the headless stack.
        if(TARGET firmir-gui-headless)
            firml_add_target(firmlc_window_codegen
                SOURCES testdata/e2e/window_app.fml)
            add_executable(firmlc_window_app testdata/e2e/window_app_main.cpp)
            target_link_libraries(firmlc_window_app PRIVATE
                firmlc_window_codegen firmir-gui-headless)
            if(FIRMIR_SANITIZER STREQUAL "thread")
                add_test(NAME firmlc.window.app
                         COMMAND setarch -R $<TARGET_FILE:firmlc_window_app>)
            else()
                add_test(NAME firmlc.window.app
                         COMMAND $<TARGET_FILE:firmlc_window_app>)
            endif()
        endif()

        # M9 e2e: script-declared properties and string concatenation — a
        # mini calculator driven by synthesized button clicks.
        if(TARGET firmir-gui-headless)
            firml_add_target(firmlc_calc_codegen
                SOURCES testdata/e2e/calc_app.fml)
            add_executable(firmlc_calc_app testdata/e2e/calc_app_main.cpp)
            target_link_libraries(firmlc_calc_app PRIVATE
                firmlc_calc_codegen firmir-gui-headless)
            if(FIRMIR_SANITIZER STREQUAL "thread")
                add_test(NAME firmlc.calc.app
                         COMMAND setarch -R $<TARGET_FILE:firmlc_calc_app>)
            else()
                add_test(NAME firmlc.calc.app
                         COMMAND $<TARGET_FILE:firmlc_calc_app>)
            endif()
        endif()

        # M6: firml_add_target() integration — app.fml is compiled to C++ at
        # build time, the generated library is linked into a test binary.
        firml_add_target(firmlc_m6_codegen SOURCES testdata/m6/app.fml)
        add_executable(firmlc_m6_app testdata/m6/main.cpp)
        target_link_libraries(firmlc_m6_app PRIVATE firmlc_m6_codegen)
        if(FIRMIR_SANITIZER STREQUAL "thread")
            add_test(NAME firmlc.m6.app COMMAND setarch -R $<TARGET_FILE:firmlc_m6_app>)
        else()
            add_test(NAME firmlc.m6.app COMMAND $<TARGET_FILE:firmlc_m6_app>)
        endif()
    endif()
endif()
