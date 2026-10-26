# Runs a WASI test binary that must crash. The test passes when the guest
# exits with a non-zero status, its output contains one stable substring of
# the expected diagnostic, and its output contains every optional marker that
# the test itself prints (for example to prove that work ran before the
# crash).
#
# RUNNER is WASI_TEST_RUNNER with its list separators replaced by "|".
# MARKERS, when defined and non-empty, is a "|"-separated list of markers.
# "|" is the list separator, so it cannot appear in a runner argument or in a
# marker.
string(REPLACE "|" ";" runner "${RUNNER}")
execute_process(
  COMMAND ${runner} "${BINARY}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(result STREQUAL "0")
  message(FATAL_ERROR
    "expected a crash, but the guest exited normally (result: ${result})\n"
    "${output}${error}")
endif()
string(FIND "${output}${error}" "${EXPECTED}" found)
if(found EQUAL -1)
  message(FATAL_ERROR
    "the guest crashed without the expected diagnostic '${EXPECTED}' "
    "(result: ${result})\n"
    "${output}${error}")
endif()
if(DEFINED MARKERS AND NOT MARKERS STREQUAL "")
  string(REPLACE "|" ";" markers "${MARKERS}")
  foreach(marker IN LISTS markers)
    string(FIND "${output}${error}" "${marker}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR
        "the guest crashed without the expected marker '${marker}' "
        "(result: ${result})\n"
        "${output}${error}")
    endif()
  endforeach()
endif()
