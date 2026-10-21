# Runs run-wasi-test.mjs against a binary with arguments that it must reject.
# The test passes when the runner exits with a non-zero status and prints its
# own failure line FAILURE. This checks the runner, not the guest: a runner
# that accepted a wrong mode, a missing diagnostic, or a missing marker would
# let the crash tests pass for the wrong reason.
#
# NODE_EXECUTABLE runs RUNNER with MODE, BINARY, EXPECTED and, when defined
# and non-empty, the MARKERS list.
set(expected_markers "${EXPECTED}")
if(DEFINED MARKERS AND NOT MARKERS STREQUAL "")
  list(APPEND expected_markers "${MARKERS}")
endif()

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${RUNNER}" "${MODE}" "${BINARY}"
    ${expected_markers}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR
    "runner unexpectedly accepted mode=${MODE} "
    "expected=${expected_markers}\n${output}${error}")
endif()
string(FIND "${output}${error}" "${FAILURE}" found)
if(found EQUAL -1)
  message(FATAL_ERROR
    "runner failed without its failure line '${FAILURE}' "
    "(result: ${result})\n${output}${error}")
endif()
