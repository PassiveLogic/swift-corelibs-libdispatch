set(expected_markers "${EXPECTED}")
if(DEFINED EXTRA_EXPECTED AND NOT EXTRA_EXPECTED STREQUAL "")
  list(APPEND expected_markers "${EXTRA_EXPECTED}")
endif()

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${RUNNER}" "${MODE}" "${BINARY}" ${expected_markers}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR
    "runner unexpectedly accepted mode=${MODE} expected=${expected_markers}\n${output}${error}")
endif()
