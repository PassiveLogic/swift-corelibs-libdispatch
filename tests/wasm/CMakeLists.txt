# Node drives the focused tests (it captures guest output and checks exit mode
# plus expected markers). When Node is missing or too old, everything here
# still builds and every test stays visible in ctest, just DISABLED - the same
# shape tests/CMakeLists.txt uses when WASI_TEST_RUNNER is absent.
set(DISPATCH_WASI_NODE_OK TRUE)
find_program(NODE_EXECUTABLE node)
if(NOT NODE_EXECUTABLE)
  message(STATUS "Node not found; tests in tests/wasm are registered but disabled")
  set(DISPATCH_WASI_NODE_OK FALSE)
  set(NODE_EXECUTABLE node)
else()
  execute_process(
    COMMAND "${NODE_EXECUTABLE}" --version
    RESULT_VARIABLE NODE_VERSION_RESULT
    OUTPUT_VARIABLE NODE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "^v" "" NODE_VERSION "${NODE_VERSION}")
  if(NOT NODE_VERSION_RESULT EQUAL 0)
    message(STATUS "Failed to query Node version with ${NODE_EXECUTABLE}; "
      "tests in tests/wasm are registered but disabled")
    set(DISPATCH_WASI_NODE_OK FALSE)
  elseif(NODE_VERSION VERSION_LESS 19.8)
    message(STATUS "Node 19.8 or newer is required for tests/wasm, found "
      "${NODE_VERSION}; tests are registered but disabled")
    set(DISPATCH_WASI_NODE_OK FALSE)
  endif()
endif()

function(add_wasi_test name source mode expected timeout)
  add_executable(${name} ${source})
  target_compile_options(${name} PRIVATE -fblocks -Wall)
  target_link_libraries(${name} PRIVATE dispatch)
  target_link_options(${name} PRIVATE
    -nodefaultlibs
    -lc
    "${DISPATCH_WASI_BUILTINS}")
  add_test(NAME ${name}
    COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
      ${mode} "$<TARGET_FILE:${name}>" "${expected}" ${ARGN})
  set_tests_properties(${name} PROPERTIES
    TIMEOUT ${timeout})
  if(NOT DISPATCH_WASI_NODE_OK)
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
  endif()
endfunction()

add_wasi_test(dispatch_wasi_smoke smoke.c success "ALL PASS (9/9)" 20)
add_wasi_test(dispatch_wasi_unsupported_source unsupported-source.c crash
  "dispatch source file descriptor is not open" 5)
add_wasi_test(dispatch_wasi_signal_source signal-source.c success
  "signal source OK" 5)
add_wasi_test(dispatch_wasi_write_source write-source.c success
  "write source OK" 5)
add_wasi_test(dispatch_wasi_nested_wait nested-wait.c crash
  "semaphore wait from within a drained work item" 5)
add_wasi_test(dispatch_wasi_nested_timed_wait nested-timed-wait.c success "probe OK" 5)
add_wasi_test(dispatch_wasi_dispatch_main_work dispatch-main-work.c crash
  "dispatch_main(): no runnable work on single-threaded WASI" 5 "main queue work OK")
add_wasi_test(dispatch_wasi_dispatch_main_idle dispatch-main-idle.c crash
  "dispatch_main(): no runnable work on single-threaded WASI" 5
  "dispatch_main idle entry")
add_wasi_test(dispatch_wasi_barrier_order barrier-order.c success "barrier order OK" 5)
add_wasi_test(dispatch_wasi_qos_order qos-order.c success "qos order OK" 5)
add_wasi_test(dispatch_wasi_root_fairness root-fairness.c success
  "root fairness OK" 5)
add_wasi_test(dispatch_wasi_wall_timer wall-timer.c success "wall timer OK" 5)
add_wasi_test(dispatch_wasi_sync_inline sync-inline.c success "sync inline OK" 5)
add_wasi_test(dispatch_wasi_main_queue_order main-queue-order.c success
  "main queue order OK" 5)
add_wasi_test(dispatch_wasi_blocking_waits blocking-waits.c success
  "blocking waits OK" 10)
add_wasi_test(dispatch_wasi_api_surface api-surface.c success "api surface OK" 10)
add_wasi_test(dispatch_wasi_assert_queue assert-queue.c crash
  "Block was expected to execute on queue" 5 "assert on-queue OK")
add_wasi_test(dispatch_wasi_eager_drain eager-drain.c success "eager drain OK" 5)
add_wasi_test(dispatch_wasi_specific_destructor specific-destructor.c success
  "specific destructor OK" 5)
add_wasi_test(dispatch_wasi_sema_late_signal sema-late-signal.c success
  "late signal OK" 5)
add_wasi_test(dispatch_wasi_signal_disposition signal-disposition.c success
  "signal disposition OK" 5)
add_wasi_test(dispatch_wasi_sync_nested_async sync-nested-async.c success
  "sync nested async OK" 5)
add_wasi_test(dispatch_wasi_async_and_wait async-and-wait.c success
  "async and wait OK" 5)

# Readiness-driven tests: the runner pipes stdin only after a delay, so the
# guest must park in the host poll and wake on fd readiness to pass.
add_executable(dispatch_wasi_read_source read-source.c)
add_executable(dispatch_wasi_fd_wakeup_wait fd-wakeup-wait.c)
add_executable(dispatch_wasi_io_stream dispatch-io-stream.c)
add_executable(dispatch_wasi_pipe_eof pipe-eof-source.c)
foreach(_stdin_test dispatch_wasi_read_source dispatch_wasi_fd_wakeup_wait
    dispatch_wasi_io_stream dispatch_wasi_pipe_eof)
  target_compile_options(${_stdin_test} PRIVATE -fblocks -Wall)
  target_link_libraries(${_stdin_test} PRIVATE dispatch)
  target_link_options(${_stdin_test} PRIVATE
    -nodefaultlibs
    -lc
    "${DISPATCH_WASI_BUILTINS}")
endforeach()
add_test(NAME dispatch_wasi_read_source
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" success "$<TARGET_FILE:dispatch_wasi_read_source>"
    "read source OK" "read source armed")
add_test(NAME dispatch_wasi_fd_wakeup_wait
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" success "$<TARGET_FILE:dispatch_wasi_fd_wakeup_wait>"
    "fd wakeup OK" "waiting on semaphore")
add_test(NAME dispatch_wasi_io_stream
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" success "$<TARGET_FILE:dispatch_wasi_io_stream>"
    "dispatch io stream OK")
# pipe EOF with a source that never cancels: on hangup-reporting hosts the
# harvest drops the source and dispatch_main() traps idle; on hosts without
# the hangup flag (emulated with --suppress-poll-hangup) the permanently
# ready park must become a named crash, not a silent hot loop
add_test(NAME dispatch_wasi_pipe_eof_hangup
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" crash "$<TARGET_FILE:dispatch_wasi_pipe_eof>"
    "dispatch_main(): no runnable work on single-threaded WASI"
    "pipe EOF observed")
add_test(NAME dispatch_wasi_pipe_eof_spin_crash
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" --suppress-poll-hangup crash
    "$<TARGET_FILE:dispatch_wasi_pipe_eof>"
    "permanently ready with the sole thread parked"
    "pipe EOF observed")
# capability guardrail: under a host whose poll_oneoff rejects fd
# subscriptions (the runner emulates one with --deny-fd-poll), fd-source
# registration must crash with the named diagnostic instead of hanging
add_test(NAME dispatch_wasi_fd_poll_denied
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --deny-fd-poll crash "$<TARGET_FILE:dispatch_wasi_read_source>"
    "does not support file-descriptor readiness")
# close-while-armed: hosts fail the whole harvest poll with EBADF when an
# armed source's fd is no longer open (wasmtime's shape, emulated by
# failing every fd poll after the registration probe); the crash must name
# the armed-source cause, not a generic poll failure
add_test(NAME dispatch_wasi_fd_closed_while_armed
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:never-read" --fd-poll-ebadf-after 1 crash
    "$<TARGET_FILE:dispatch_wasi_read_source>"
    "file descriptor closed while dispatch source is armed"
    "read source armed")
set_tests_properties(dispatch_wasi_read_source dispatch_wasi_fd_wakeup_wait
  dispatch_wasi_io_stream dispatch_wasi_pipe_eof_hangup
  dispatch_wasi_pipe_eof_spin_crash dispatch_wasi_fd_poll_denied
  dispatch_wasi_fd_closed_while_armed
  PROPERTIES TIMEOUT 10)
# runner robustness: the guest exits long before the delayed stdin write
# lands; the runner must still report the guest's own result rather than
# dying on EPIPE from the orphaned write
add_test(NAME dispatch_wasi_runner_early_exit
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "400:never-read" success
    "$<TARGET_FILE:dispatch_wasi_sync_inline>" "sync inline OK")
set_tests_properties(dispatch_wasi_runner_early_exit PROPERTIES TIMEOUT 10)

add_executable(dispatch_wasi_clean_c_consumer clean-consumer.c)
target_compile_options(dispatch_wasi_clean_c_consumer PRIVATE -fblocks -Wall)
target_link_libraries(dispatch_wasi_clean_c_consumer PRIVATE dispatch)
target_link_options(dispatch_wasi_clean_c_consumer PRIVATE
  -nodefaultlibs
  -lc
  "${DISPATCH_WASI_BUILTINS}")
add_test(NAME dispatch_wasi_clean_c_consumer
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    success "$<TARGET_FILE:dispatch_wasi_clean_c_consumer>" "consumer OK")
set_tests_properties(dispatch_wasi_clean_c_consumer PROPERTIES
  TIMEOUT 20)

if(ENABLE_SWIFT)
  add_executable(dispatch_wasi_clean_swift_consumer clean-consumer.swift)
  target_link_libraries(dispatch_wasi_clean_swift_consumer PRIVATE swiftDispatch)
  add_test(NAME dispatch_wasi_clean_swift_consumer
    COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
      success "$<TARGET_FILE:dispatch_wasi_clean_swift_consumer>" "Swift consumer OK")
  set_tests_properties(dispatch_wasi_clean_swift_consumer PROPERTIES
    TIMEOUT 20)

  add_executable(dispatch_wasi_swift_autolink_consumer clean-consumer.swift)
  add_dependencies(dispatch_wasi_swift_autolink_consumer
    swiftDispatch dispatch BlocksRuntime)
  target_include_directories(dispatch_wasi_swift_autolink_consumer PRIVATE
    "${PROJECT_BINARY_DIR}/src/swift/swift")
  target_compile_options(dispatch_wasi_swift_autolink_consumer PRIVATE
    "SHELL:-sdk \"${CMAKE_SYSROOT}\""
    "SHELL:-resource-dir \"${SWIFT_WASI_STATIC_RESOURCES}\""
    -static-stdlib
    "SHELL:-vfsoverlay \"${PROJECT_BINARY_DIR}/dispatch-vfs-overlay.yaml\""
    "SHELL:-Xcc -fblocks"
    "SHELL:-Xcc -I\"${PROJECT_SOURCE_DIR}\""
    "SHELL:-Xcc -D_WASI_EMULATED_SIGNAL"
    "SHELL:-Xcc -D_WASI_EMULATED_MMAN"
    "SHELL:-Xcc -D_WASI_EMULATED_GETPID")
  target_link_directories(dispatch_wasi_swift_autolink_consumer PRIVATE
    "${PROJECT_BINARY_DIR}/src"
    "${PROJECT_BINARY_DIR}/src/swift"
    "${PROJECT_BINARY_DIR}/src/BlocksRuntime")
  target_link_options(dispatch_wasi_swift_autolink_consumer PRIVATE
    "SHELL:-Xclang-linker -resource-dir -Xclang-linker \"${SWIFT_WASI_CLANG_RESOURCES}\"")
  add_test(NAME dispatch_wasi_swift_autolink_consumer
    COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
      success "$<TARGET_FILE:dispatch_wasi_swift_autolink_consumer>" "Swift consumer OK")
  set_tests_properties(dispatch_wasi_swift_autolink_consumer PROPERTIES TIMEOUT 20)
endif()

add_test(NAME dispatch_wasi_runner_wrong_mode
  COMMAND "${CMAKE_COMMAND}"
    -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
    -DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs
    -DBINARY=$<TARGET_FILE:dispatch_wasi_smoke>
    -DMODE=crash
    -DEXPECTED=ALL\ PASS
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect-runner-failure.cmake)
add_test(NAME dispatch_wasi_runner_missing_diagnostic
  COMMAND "${CMAKE_COMMAND}"
    -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
    -DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs
    -DBINARY=$<TARGET_FILE:dispatch_wasi_unsupported_source>
    -DMODE=crash
    -DEXPECTED=diagnostic-that-is-not-emitted
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect-runner-failure.cmake)
add_test(NAME dispatch_wasi_runner_missing_marker
  COMMAND "${CMAKE_COMMAND}"
    -DNODE_EXECUTABLE=${NODE_EXECUTABLE}
    -DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs
    -DBINARY=$<TARGET_FILE:dispatch_wasi_dispatch_main_work>
    -DMODE=crash
    -DEXPECTED=main\ queue\ work\ OK
    -DEXTRA_EXPECTED=marker-that-is-not-emitted
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect-runner-failure.cmake)
set_tests_properties(
  dispatch_wasi_runner_wrong_mode
  dispatch_wasi_runner_missing_diagnostic
  dispatch_wasi_runner_missing_marker
  PROPERTIES TIMEOUT 20)

if(NOT DISPATCH_WASI_NODE_OK)
  # tests registered outside add_wasi_test()
  set(_wasi_explicit_tests
    dispatch_wasi_read_source
    dispatch_wasi_fd_wakeup_wait
    dispatch_wasi_io_stream
    dispatch_wasi_pipe_eof_hangup
    dispatch_wasi_pipe_eof_spin_crash
    dispatch_wasi_fd_poll_denied
    dispatch_wasi_fd_closed_while_armed
    dispatch_wasi_runner_early_exit
    dispatch_wasi_clean_c_consumer
    dispatch_wasi_runner_wrong_mode
    dispatch_wasi_runner_missing_diagnostic
    dispatch_wasi_runner_missing_marker)
  if(ENABLE_SWIFT)
    list(APPEND _wasi_explicit_tests
      dispatch_wasi_clean_swift_consumer
      dispatch_wasi_swift_autolink_consumer)
  endif()
  set_tests_properties(${_wasi_explicit_tests} PROPERTIES DISABLED TRUE)
endif()
