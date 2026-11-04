# Snapshot test driver for firmlc.
#   MODE=ast:  firmlc --dump-ast <case>.fml             (parser only)
#   MODE=sema: firmlc --dump-sema <case>.fml -I include (parser + semantics)
#   MODE=gen:  firmlc --dump-gen <case>.fml -I include  (C++ code generation)
# The tool runs in ${TESTDATA} with a relative input path, so diagnostics in
# the snapshots stay stable. Expectations:
#   <case>.err    => failure expected (rc != 0); stderr must match byte-for-byte
#   <case>.<mode> => success expected; stdout must match byte-for-byte
#   <case>.warn   => (success only) stderr must match; otherwise stderr must
#                    be empty
file(REMOVE_RECURSE "${WORK_DIR}/${CASE}")
file(MAKE_DIRECTORY "${WORK_DIR}/${CASE}")
separate_arguments(TOOL_WRAPPER)

if(MODE STREQUAL "sema")
    set(args --dump-sema "${CASE}.fml" -I include)
elseif(MODE STREQUAL "gen")
    set(args --dump-gen "${CASE}.fml" -I include)
else()
    set(args --dump-ast "${CASE}.fml")
endif()

execute_process(
    COMMAND ${TOOL_WRAPPER} "${TOOL}" ${args}
    WORKING_DIRECTORY "${TESTDATA}"
    RESULT_VARIABLE rc
    OUTPUT_FILE "${WORK_DIR}/${CASE}/out.txt"
    ERROR_FILE "${WORK_DIR}/${CASE}/err.txt"
)

if(EXISTS "${TESTDATA}/${CASE}.err")
    if(rc EQUAL 0)
        message(FATAL_ERROR "firmlc ${CASE}: expected failure, but the tool succeeded")
    endif()
    file(READ "${WORK_DIR}/${CASE}/err.txt" actual)
    file(READ "${TESTDATA}/${CASE}.err" expected)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "firmlc ${CASE}: error output differs from the snapshot:\n${actual}")
    endif()
    return()
endif()

if(NOT rc EQUAL 0)
    file(READ "${WORK_DIR}/${CASE}/err.txt" err)
    message(FATAL_ERROR "firmlc ${CASE}: expected success, got exit code ${rc}:\n${err}")
endif()

set(snapshot "${TESTDATA}/${CASE}.${MODE}")
if(NOT EXISTS "${snapshot}")
    message(FATAL_ERROR "firmlc ${CASE}: missing snapshot file ${snapshot}")
endif()
file(READ "${WORK_DIR}/${CASE}/out.txt" actual)
file(READ "${snapshot}" expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "firmlc ${CASE}: output differs from the snapshot:\n${actual}")
endif()

file(READ "${WORK_DIR}/${CASE}/err.txt" actualErr)
if(EXISTS "${TESTDATA}/${CASE}.warn")
    file(READ "${TESTDATA}/${CASE}.warn" expectedErr)
    if(NOT actualErr STREQUAL expectedErr)
        message(FATAL_ERROR "firmlc ${CASE}: warnings differ from the snapshot:\n${actualErr}")
    endif()
elseif(NOT actualErr STREQUAL "")
    message(FATAL_ERROR "firmlc ${CASE}: unexpected warnings on stderr:\n${actualErr}")
endif()
