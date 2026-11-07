add_executable(firmir-doc main.cpp)

# Smoke test: generate docs for a fixture header and diff against the
# expected Markdown.
#
# Under ThreadSanitizer the tool refuses the default ASLR memory layout on
# some kernels ("unexpected memory mapping"), so run it with ASLR disabled
# just like the main test binary.
if(FIRMIR_BUILD_TESTS)
    if(FIRMIR_SANITIZER STREQUAL "thread")
        set(DOC_TOOL_WRAPPER "setarch -R")
    else()
        set(DOC_TOOL_WRAPPER "")
    endif()
    add_test(
        NAME firmir-doc
        COMMAND ${CMAKE_COMMAND}
            "-DTOOL_WRAPPER=${DOC_TOOL_WRAPPER}"
            -DTOOL=$<TARGET_FILE:firmir-doc>
            -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/testdata/sample.hpp.md
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
    )
endif()
