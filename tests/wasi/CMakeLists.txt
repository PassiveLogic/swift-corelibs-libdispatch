# WASI single-threaded port tests.
#
# Each test is a small C program (see *.c here) built to a wasm32-wasip1 module
# and run under a WASI runtime; it exits 0 on success. The legacy bsdtest harness
# uses fork/exec and does not apply on WASI, so tests/CMakeLists.txt routes here
# and returns early when DISPATCH_WASI is set.
#
# A runner is required to register ctest cases; wasmtime is used by default. Set
# -DWASI_TEST_RUNNER="/path/to/runtime;run" to override (e.g. a node+shim wrapper).

if(NOT DEFINED WASI_TEST_RUNNER)
  find_program(WASMTIME_EXECUTABLE NAMES wasmtime)
  if(WASMTIME_EXECUTABLE)
    set(WASI_TEST_RUNNER "${WASMTIME_EXECUTABLE};run")
  endif()
endif()

set(_wasi_tests
  dispatch_wasi_smoke
  dispatch_wasi_mainqueue
  dispatch_wasi_sync)

foreach(_t ${_wasi_tests})
  add_executable(${_t} ${_t}.c)
  target_include_directories(${_t} PRIVATE ${PROJECT_SOURCE_DIR})
  # `dispatch` PUBLIC-links BlocksRuntime; the emulation libs satisfy libdispatch's
  # signal/mman/getpid/clock references and must follow it on the link line.
  target_link_libraries(${_t} PRIVATE dispatch
    -lwasi-emulated-signal -lwasi-emulated-mman
    -lwasi-emulated-getpid -lwasi-emulated-process-clocks)
  if(WASI_TEST_RUNNER)
    add_test(NAME ${_t} COMMAND ${WASI_TEST_RUNNER} $<TARGET_FILE:${_t}>)
  endif()
endforeach()

if(NOT WASI_TEST_RUNNER)
  message(STATUS "WASI: no test runner found (wasmtime); tests build but are not "
    "registered to run. Set -DWASI_TEST_RUNNER to enable `ctest`.")
endif()
