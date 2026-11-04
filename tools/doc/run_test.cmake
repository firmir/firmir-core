# Smoke test for firmir-doc: generate docs for FIXTURE_DIR and compare
# sample.hpp.md against EXPECTED.
file(REMOVE_RECURSE "${WORK_DIR}")
separate_arguments(TOOL_WRAPPER)
execute_process(
    COMMAND ${TOOL_WRAPPER} "${TOOL}" "${FIXTURE_DIR}" "${WORK_DIR}"
    RESULT_VARIABLE rc
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "firmir-doc exited with ${rc}")
endif()

set(generated "${WORK_DIR}/sample.hpp.md")
if(NOT EXISTS "${generated}")
    message(FATAL_ERROR "missing generated file: ${generated}")
endif()
if(NOT EXISTS "${WORK_DIR}/index.md")
    message(FATAL_ERROR "missing index.md")
endif()

file(READ "${generated}" actual)
file(READ "${EXPECTED}" expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
        "generated Markdown differs from the expected output:\n${actual}")
endif()
