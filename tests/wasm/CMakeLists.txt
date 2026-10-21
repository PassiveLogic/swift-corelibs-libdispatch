# Focused behavior tests (add_wasi_unit_test) and crash tests
# (add_wasi_crash_test) run under WASI_TEST_RUNNER, like the upstream suite in
# tests/CMakeLists.txt, and are DISABLED only when that runner is missing.
# The remaining tests run under Node. The host event loop tests run under
# run-wasi-host-event-loop-test.mjs; the others under run-wasi-test.mjs,
# which checks the exit mode and the output markers:
#  - the host event loop tests need a Node reactor host.
#  - the regular-file IO and crash-log-file tests need a preopened directory.
#  - the wall-clock jump tests need the runner to shift CLOCK_REALTIME.
#  - the self-checks of run-wasi-test.mjs.
# When Node is missing or too old, those tests still build and stay visible in
# ctest, just DISABLED.
set(DISPATCH_WASI_NODE_OK TRUE)
find_program(NODE_EXECUTABLE node)
if(NOT NODE_EXECUTABLE)
  message(STATUS "Node not found; Node-driven tests in tests/wasm are "
    "registered but disabled")
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
      "Node-driven tests in tests/wasm are registered but disabled")
    set(DISPATCH_WASI_NODE_OK FALSE)
  elseif(NODE_VERSION VERSION_LESS 19.8)
    message(STATUS "Node 19.8 or newer is required for the Node-driven tests "
      "in tests/wasm, found ${NODE_VERSION}; they are registered but disabled")
    set(DISPATCH_WASI_NODE_OK FALSE)
  endif()
endif()

# The tests include the bsdtests harness from tests/ and the private SPI as
# <dispatch/private.h> through the dispatch -> private/ link that
# tests/CMakeLists.txt creates in the tests build directory.
set(DISPATCH_WASI_TEST_INCLUDE_DIRS
  "${PROJECT_SOURCE_DIR}/tests"
  "${PROJECT_BINARY_DIR}/tests")

# Focused WASI behavior tests on the bsdtests harness. They build and run like
# the upstream unit tests: WASI_TEST_RUNNER executes the binary directly and
# the exit code decides the result.
function(add_wasi_unit_test name source timeout)
  add_unit_test(${name} SOURCES ${source})
  target_include_directories(${name} PRIVATE
    ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
  set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
endfunction()

# Crash tests: the guest must crash with a named diagnostic, for example a
# wait that can never be satisfied on one thread, a failed client assertion,
# or a failure that must not become a hang. The test passes when the guest
# exits with a non-zero status and its output contains one stable substring
# of the diagnostic plus every optional marker listed after the timeout.
# Markers are progress lines that the test itself prints, e.g. to prove that
# some work ran before the crash. "|" separates runner arguments and markers
# internally, so neither can contain it.
string(REPLACE ";" "|" DISPATCH_WASI_CRASH_RUNNER "${WASI_TEST_RUNNER}")
function(add_wasi_crash_test name source expected timeout)
  set(markers_arg)
  if(NOT "${ARGN}" STREQUAL "")
    string(REPLACE ";" "|" markers "${ARGN}")
    set(markers_arg "-DMARKERS=${markers}")
  endif()
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
  target_compile_options(${name} PRIVATE -fblocks -Wall)
  target_link_libraries(${name} PRIVATE dispatch)
  add_test(NAME ${name}
    COMMAND "${CMAKE_COMMAND}"
      "-DRUNNER=${DISPATCH_WASI_CRASH_RUNNER}"
      "-DBINARY=$<TARGET_FILE:${name}>"
      "-DEXPECTED=${expected}"
      ${markers_arg}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/expect-crash.cmake")
  set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
  if(NOT WASI_TEST_RUNNER_EXECUTABLE)
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
  endif()
endfunction()


add_wasi_unit_test(dispatch_wasi_smoke smoke.c 20)
add_wasi_crash_test(dispatch_wasi_nested_wait nested-wait.c
  "Deadlock: semaphore wait from a work item" 5
  "nested wait started")
add_wasi_unit_test(dispatch_wasi_nested_timed_wait nested-timed-wait.c 5)
add_wasi_unit_test(dispatch_wasi_dispatch_main_work dispatch-main-work.c 5)
add_wasi_crash_test(dispatch_wasi_dispatch_main_idle dispatch-main-idle.c
  "dispatch_main() with no work, timer, or event source" 5)
add_wasi_crash_test(dispatch_wasi_dispatch_main_drain_idle
  dispatch-main-drain-idle.c
  "dispatch_main() with no work, timer, or event source" 5
  "main queue timer ran")
add_wasi_unit_test(dispatch_wasi_barrier_order barrier-order.c 5)
add_wasi_unit_test(dispatch_wasi_qos_order qos-order.c 5)
add_wasi_unit_test(dispatch_wasi_root_fairness root-fairness.c 5)
add_wasi_unit_test(dispatch_wasi_wall_timer wall-timer.c 5)
add_wasi_unit_test(dispatch_wasi_sync_inline sync-inline.c 5)
add_wasi_unit_test(dispatch_wasi_main_queue_order main-queue-order.c 5)
add_wasi_unit_test(dispatch_wasi_blocking_waits blocking-waits.c 10)
add_wasi_unit_test(dispatch_wasi_api_surface api-surface.c 10)
add_wasi_crash_test(dispatch_wasi_assert_queue assert-queue.c
  "Block was expected to execute on queue" 5
  "on-queue assertions passed")
add_wasi_unit_test(dispatch_wasi_deferred_submission deferred-submission.c 5)
add_wasi_unit_test(dispatch_wasi_specific_destructor specific-destructor.c 5)
add_wasi_unit_test(dispatch_wasi_sema_late_signal sema-late-signal.c 5)
add_wasi_unit_test(dispatch_wasi_sync_nested_async sync-nested-async.c 5)
add_wasi_unit_test(dispatch_wasi_async_and_wait async-and-wait.c 5)
add_wasi_unit_test(dispatch_wasi_cancel_and_wait cancel-and-wait.c 5)
add_wasi_unit_test(dispatch_wasi_main_root_fairness main-root-fairness.c 5)
add_wasi_unit_test(dispatch_wasi_main_lane_fairness main-lane-fairness.c 10)
add_wasi_unit_test(dispatch_wasi_main_source_handlers main-source-handlers.c 5)
add_wasi_unit_test(dispatch_wasi_main_root_fairness_perform
  main-root-fairness-perform.c 5)
add_wasi_unit_test(dispatch_wasi_serial_lane_fairness serial-lane-fairness.c 5)
add_wasi_unit_test(dispatch_wasi_serial_lane_wait serial-lane-wait.c 5)
add_wasi_unit_test(dispatch_wasi_expired_wait expired-wait.c 5)
add_wasi_unit_test(dispatch_wasi_self_triggering_source
  self-triggering-source.c 5)
add_wasi_unit_test(dispatch_wasi_reposting_workloop reposting-workloop.c 5)
add_wasi_unit_test(dispatch_wasi_timed_wait_main_backlog
  timed-wait-main-backlog.c 5)
add_wasi_unit_test(dispatch_wasi_pumped_ownership pumped-ownership.c 5)
add_wasi_unit_test(dispatch_wasi_pumped_mgr_under_sync
  pumped-mgr-under-sync.c 5)
add_wasi_unit_test(dispatch_wasi_main_queue_perform main-queue-perform.c 5)
add_wasi_unit_test(dispatch_wasi_async_and_wait_pump async-and-wait-pump.c 5)
add_wasi_unit_test(dispatch_wasi_constructor_main_queue
  constructor-main-queue.c 5)
add_wasi_unit_test(dispatch_wasi_clean_consumer clean-consumer.c 5)

# Each indefinite-wait deadlock diagnostic names the public call that blocked.
set(_wasi_nested_sync_diag "Deadlock: dispatch_sync or \
dispatch_async_and_wait from a work item")
set(_wasi_nested_wait_diag "Deadlock: dispatch_group_wait, \
dispatch_block_wait, or dispatch_source_cancel_and_wait from a work item")
add_wasi_crash_test(dispatch_wasi_nested_sync_diag wait-diagnostics.c
  "${_wasi_nested_sync_diag}" 5 "nested sync starting")
target_compile_definitions(dispatch_wasi_nested_sync_diag PRIVATE
  WAIT_DIAG_SYNC=1)
add_wasi_crash_test(dispatch_wasi_nested_group_diag wait-diagnostics.c
  "${_wasi_nested_wait_diag}" 5 "nested group wait starting")
target_compile_definitions(dispatch_wasi_nested_group_diag PRIVATE
  WAIT_DIAG_GROUP=1)
add_wasi_crash_test(dispatch_wasi_nested_cancel_diag wait-diagnostics.c
  "${_wasi_nested_wait_diag}" 5 "nested cancel_and_wait starting")
target_compile_definitions(dispatch_wasi_nested_cancel_diag PRIVATE
  WAIT_DIAG_CANCEL=1)
add_wasi_crash_test(dispatch_wasi_pumped_held_queue_sync
  pumped-held-queue-sync.c
  "${_wasi_nested_sync_diag}" 5 "pumped item syncs onto the held queue")
add_wasi_crash_test(dispatch_wasi_pumped_once_reenter pumped-once-reenter.c
  "Deadlock: lock contended on single-threaded WASI" 5
  "pumped item enters the same dispatch_once")
add_wasi_crash_test(dispatch_wasi_nested_dispatch_main nested-dispatch-main.c
  "dispatch_main() called from a work item" 5
  "nested dispatch_main starting")
add_wasi_crash_test(dispatch_wasi_once_corrupt_predicate
  once-corrupt-predicate.c
  "Corrupt lock value" 5 "garbage predicate set")
# The host scheduler callback must return without running Dispatch. Each
# mode runs one pump point inside the callback.
foreach(_mode 0 1 2 3 4)
  add_wasi_crash_test(dispatch_wasi_scheduler_pump_${_mode} scheduler-pump.c
    "Scheduler ran Dispatch inline" 5
    "scheduler callback entered")
  target_compile_definitions(dispatch_wasi_scheduler_pump_${_mode} PRIVATE
    SCHEDULER_PUMP_MODE=${_mode})
endforeach()
add_wasi_unit_test(dispatch_wasi_scheduler_no_pump scheduler-no-pump.c 5)
# Crash reasons reach stderr for every LIBDISPATCH_LOG value. A wasm trap
# carries no payload, so a disabled or redirected log must not hide them.
add_wasi_crash_test(dispatch_wasi_crash_log_disabled crash-log-env.c
  "Over-resume of an object" 5 "crash log env NO armed")
add_wasi_crash_test(dispatch_wasi_crash_log_disabled_assert crash-log-env.c
  "Block was expected to execute on queue [wasi.crash_log_env]" 5
  "crash log env NO armed")
target_compile_definitions(dispatch_wasi_crash_log_disabled_assert PRIVATE
  CRASH_LOG_ASSERT=1)
# Allocation failure must be a named crash, not a hang. The sole thread
# cannot free memory while it sleeps, so the inherited sleep-and-retry loop
# never succeeds. A small --max-memory (16 MiB) lets the test reach the
# memory limit in milliseconds. alloc-failure.c uses the same limit.
add_wasi_crash_test(dispatch_wasi_alloc_failure alloc-failure.c
  "Out of memory" 5 "heap exhausted")
add_wasi_crash_test(dispatch_wasi_alloc_failure_async alloc-failure.c
  "Out of memory" 5 "heap exhausted")
target_compile_definitions(dispatch_wasi_alloc_failure_async PRIVATE
  ALLOC_FAILURE_ASYNC=1)
foreach(_alloc_test dispatch_wasi_alloc_failure
    dispatch_wasi_alloc_failure_async)
  target_link_options(${_alloc_test} PRIVATE "-Wl,--max-memory=16777216")
endforeach()

set(_wasi_node_tests)

add_executable(dispatch_wasi_host_event_loop host-event-loop.c)
target_include_directories(dispatch_wasi_host_event_loop PRIVATE
  ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
target_compile_options(dispatch_wasi_host_event_loop PRIVATE -fblocks -Wall)
target_link_libraries(dispatch_wasi_host_event_loop PRIVATE dispatch)
target_link_options(dispatch_wasi_host_event_loop PRIVATE -mexec-model=reactor)
add_test(NAME dispatch_wasi_host_event_loop
  COMMAND "${NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-host-event-loop-test.mjs"
    "$<TARGET_FILE:dispatch_wasi_host_event_loop>")
add_test(NAME dispatch_wasi_host_event_loop_inline
  COMMAND "${NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-host-event-loop-test.mjs"
    --inline-check "$<TARGET_FILE:dispatch_wasi_host_event_loop>")
add_test(NAME dispatch_wasi_host_event_loop_late_registration
  COMMAND "${NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-host-event-loop-test.mjs"
    --late-registration "$<TARGET_FILE:dispatch_wasi_host_event_loop>")
add_test(NAME dispatch_wasi_host_event_loop_register_in_drain
  COMMAND "${NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-host-event-loop-test.mjs"
    --register-in-drain "$<TARGET_FILE:dispatch_wasi_host_event_loop>")
add_test(NAME dispatch_wasi_host_event_loop_timer_race
  COMMAND "${NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-host-event-loop-test.mjs"
    --timer-race "$<TARGET_FILE:dispatch_wasi_host_event_loop>")
add_test(NAME dispatch_wasi_host_event_loop_serial_lane
  COMMAND "${NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-host-event-loop-test.mjs"
    --serial-lane "$<TARGET_FILE:dispatch_wasi_host_event_loop>")
set(_wasi_host_loop_tests
  dispatch_wasi_host_event_loop
  dispatch_wasi_host_event_loop_inline
  dispatch_wasi_host_event_loop_late_registration
  dispatch_wasi_host_event_loop_register_in_drain
  dispatch_wasi_host_event_loop_timer_race
  dispatch_wasi_host_event_loop_serial_lane)
set_tests_properties(${_wasi_host_loop_tests} PROPERTIES TIMEOUT 10)
list(APPEND _wasi_node_tests ${_wasi_host_loop_tests})

# Regular-file dispatch_read, dispatch_write and dispatch_io with payload
# checks. The runner maps a scratch directory into the guest.
add_executable(dispatch_wasi_regfile_io regfile-io.c)
target_include_directories(dispatch_wasi_regfile_io PRIVATE
  ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
target_compile_options(dispatch_wasi_regfile_io PRIVATE -fblocks -Wall)
target_link_libraries(dispatch_wasi_regfile_io PRIVATE dispatch)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/regfile-io-scratch")
add_test(NAME dispatch_wasi_regfile_io
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --preopen "/scratch:${CMAKE_CURRENT_BINARY_DIR}/regfile-io-scratch"
    success "$<TARGET_FILE:dispatch_wasi_regfile_io>"
    "dispatch_write done" "dispatch_read payload OK" "io random range OK"
    "regular file io OK")
set_tests_properties(dispatch_wasi_regfile_io PROPERTIES TIMEOUT 10)
list(APPEND _wasi_node_tests dispatch_wasi_regfile_io)
# LIBDISPATCH_LOG=file writes to /var/tmp, which the runner preopens
add_executable(dispatch_wasi_crash_log_file crash-log-env.c)
target_compile_definitions(dispatch_wasi_crash_log_file PRIVATE
  "CRASH_LOG_ENV=\"file\"")
target_include_directories(dispatch_wasi_crash_log_file PRIVATE
  ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
target_compile_options(dispatch_wasi_crash_log_file PRIVATE -fblocks -Wall)
target_link_libraries(dispatch_wasi_crash_log_file PRIVATE dispatch)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/crash-log-var-tmp")
add_test(NAME dispatch_wasi_crash_log_file
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --preopen "/var/tmp:${CMAKE_CURRENT_BINARY_DIR}/crash-log-var-tmp"
    crash "$<TARGET_FILE:dispatch_wasi_crash_log_file>"
    "BUG IN CLIENT OF LIBDISPATCH: Over-resume of an object"
    "crash log env file armed")
set_tests_properties(dispatch_wasi_crash_log_file PROPERTIES TIMEOUT 5)
list(APPEND _wasi_node_tests dispatch_wasi_crash_log_file)
# Walltime deadlines follow host wall-clock changes at the next wake. The
# runner shifts CLOCK_REALTIME after a delay
# (--wall-clock-offset <delay ms>:<offset ms>).
foreach(_wall_test forward backward)
  add_executable(dispatch_wasi_wall_timer_${_wall_test}_jump
    wall-timer-${_wall_test}-jump.c)
  target_include_directories(dispatch_wasi_wall_timer_${_wall_test}_jump
    PRIVATE ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
  target_compile_options(dispatch_wasi_wall_timer_${_wall_test}_jump
    PRIVATE -fblocks -Wall)
  target_link_libraries(dispatch_wasi_wall_timer_${_wall_test}_jump
    PRIVATE dispatch)
endforeach()
add_test(NAME dispatch_wasi_wall_timer_forward_jump
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --wall-clock-offset "300:60000" success
    "$<TARGET_FILE:dispatch_wasi_wall_timer_forward_jump>"
    "wall timer followed the forward clock change")
add_test(NAME dispatch_wasi_wall_timer_backward_jump
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --wall-clock-offset "100:-60000" success
    "$<TARGET_FILE:dispatch_wasi_wall_timer_backward_jump>"
    "wall timer followed the backward clock change")
set_tests_properties(dispatch_wasi_wall_timer_forward_jump
  dispatch_wasi_wall_timer_backward_jump PROPERTIES TIMEOUT 10)
list(APPEND _wasi_node_tests dispatch_wasi_wall_timer_forward_jump
  dispatch_wasi_wall_timer_backward_jump)

# The self-checks of run-wasi-test.mjs pass only when the runner fails with
# its own failure line: a successful guest declared as a crash, a crash
# without the expected diagnostic, and a crash without an expected marker.
add_test(NAME dispatch_wasi_runner_wrong_mode
  COMMAND "${CMAKE_COMMAND}"
    "-DNODE_EXECUTABLE=${NODE_EXECUTABLE}"
    "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    "-DBINARY=$<TARGET_FILE:dispatch_wasi_smoke>"
    "-DMODE=crash"
    "-DEXPECTED=[PASS] dispatch_once_f runs its function once"
    "-DFAILURE=expected crash, observed exit=0"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/expect-runner-failure.cmake")
add_test(NAME dispatch_wasi_runner_missing_diagnostic
  COMMAND "${CMAKE_COMMAND}"
    "-DNODE_EXECUTABLE=${NODE_EXECUTABLE}"
    "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    "-DBINARY=$<TARGET_FILE:dispatch_wasi_dispatch_main_idle>"
    "-DMODE=crash"
    "-DEXPECTED=diagnostic-that-is-not-emitted"
    "-DFAILURE=expected output was not found"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/expect-runner-failure.cmake")
add_test(NAME dispatch_wasi_runner_missing_marker
  COMMAND "${CMAKE_COMMAND}"
    "-DNODE_EXECUTABLE=${NODE_EXECUTABLE}"
    "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    "-DBINARY=$<TARGET_FILE:dispatch_wasi_dispatch_main_idle>"
    "-DMODE=crash"
    "-DEXPECTED=dispatch_main() with no work, timer, or event source"
    "-DMARKERS=marker-that-is-not-emitted"
    "-DFAILURE=expected output was not found"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/expect-runner-failure.cmake")
set(_wasi_runner_tests
  dispatch_wasi_runner_wrong_mode
  dispatch_wasi_runner_missing_diagnostic
  dispatch_wasi_runner_missing_marker)
set_tests_properties(${_wasi_runner_tests} PROPERTIES TIMEOUT 20)
list(APPEND _wasi_node_tests ${_wasi_runner_tests})

# Event-source and Swift tests. They still use run-wasi-test.mjs and its
# output markers, not the bsdtests harness.
function(add_wasi_node_test name source mode expected timeout)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
  target_compile_options(${name} PRIVATE -fblocks -Wall)
  target_link_libraries(${name} PRIVATE dispatch)
  add_test(NAME ${name}
    COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
      ${mode} "$<TARGET_FILE:${name}>" "${expected}" ${ARGN})
  set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
  set(_wasi_node_tests ${_wasi_node_tests} ${name} PARENT_SCOPE)
endfunction()

add_wasi_crash_test(dispatch_wasi_unsupported_source unsupported-source.c
  "dispatch source file descriptor is not open" 5)
add_wasi_node_test(dispatch_wasi_signal_source signal-source.c success
  "signal source OK" 5)
add_wasi_node_test(dispatch_wasi_write_source write-source.c success
  "write source OK" 5)
add_wasi_node_test(dispatch_wasi_signal_disposition signal-disposition.c
  success "signal disposition OK" 5)

# Readiness tests. The runner pipes stdin only after a delay, so the guest
# must park in the host poll and wake on fd readiness to pass.
add_executable(dispatch_wasi_read_source read-source.c)
add_executable(dispatch_wasi_fd_wakeup_wait fd-wakeup-wait.c)
add_executable(dispatch_wasi_group_fd_wakeup group-fd-wakeup.c)
add_executable(dispatch_wasi_io_stream dispatch-io-stream.c)
add_executable(dispatch_wasi_pipe_eof pipe-eof-source.c)
foreach(_stdin_test dispatch_wasi_read_source dispatch_wasi_fd_wakeup_wait
    dispatch_wasi_group_fd_wakeup dispatch_wasi_io_stream
    dispatch_wasi_pipe_eof)
  target_compile_options(${_stdin_test} PRIVATE -fblocks -Wall)
  target_link_libraries(${_stdin_test} PRIVATE dispatch)
endforeach()
add_test(NAME dispatch_wasi_read_source
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" success "$<TARGET_FILE:dispatch_wasi_read_source>"
    "read source OK" "read source armed")
add_test(NAME dispatch_wasi_fd_wakeup_wait
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" success "$<TARGET_FILE:dispatch_wasi_fd_wakeup_wait>"
    "fd wakeup OK" "waiting on semaphore")
add_test(NAME dispatch_wasi_group_fd_wakeup
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" success "$<TARGET_FILE:dispatch_wasi_group_fd_wakeup>"
    "group fd wakeup OK" "waiting on group")
add_test(NAME dispatch_wasi_io_stream
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" success "$<TARGET_FILE:dispatch_wasi_io_stream>"
    "dispatch io stream OK")
# Pipe EOF with a source that never cancels. On hosts that report hangup,
# the harvest drops the source and dispatch_main() crashes as idle. On hosts
# without the hangup flag (emulated with --suppress-poll-hangup), the park
# that is always ready must become a named crash, not a silent hot loop.
add_test(NAME dispatch_wasi_pipe_eof_hangup
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" crash "$<TARGET_FILE:dispatch_wasi_pipe_eof>"
    "dispatch_main() with no work, timer, or event source"
    "pipe EOF observed")
add_test(NAME dispatch_wasi_pipe_eof_spin_crash
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:ping" --suppress-poll-hangup crash
    "$<TARGET_FILE:dispatch_wasi_pipe_eof>"
    "permanently ready with the sole thread parked"
    "pipe EOF observed")
# Capability guard. When the poll_oneoff of the host rejects fd
# subscriptions (the runner emulates this with --deny-fd-poll), fd source
# registration must crash with the named message instead of a hang.
add_test(NAME dispatch_wasi_fd_poll_denied
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --deny-fd-poll crash "$<TARGET_FILE:dispatch_wasi_read_source>"
    "does not support file-descriptor readiness")
# Close while armed. Some hosts fail the whole harvest poll with EBADF when
# the fd of an armed source is no longer open. wasmtime does this, and the
# runner emulates it by failing every fd poll after the registration probe.
# The crash must name the armed source as the cause, not a generic poll
# failure.
add_test(NAME dispatch_wasi_fd_closed_while_armed
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "250:never-read" --fd-poll-ebadf-after 1 crash
    "$<TARGET_FILE:dispatch_wasi_read_source>"
    "file descriptor closed while dispatch source is armed"
    "read source armed")
# Runner robustness. The guest exits long before the delayed stdin write
# lands, and the runner must still report the result of the guest instead of
# failing on EPIPE from the orphaned write.
add_test(NAME dispatch_wasi_runner_early_exit
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run-wasi-test.mjs"
    --stdin-after "400:never-read" success
    "$<TARGET_FILE:dispatch_wasi_sync_inline>" "[PASS]")
set(_wasi_fd_tests
  dispatch_wasi_read_source dispatch_wasi_fd_wakeup_wait
  dispatch_wasi_group_fd_wakeup dispatch_wasi_io_stream
  dispatch_wasi_pipe_eof_hangup dispatch_wasi_pipe_eof_spin_crash
  dispatch_wasi_fd_poll_denied dispatch_wasi_fd_closed_while_armed
  dispatch_wasi_runner_early_exit)
set_tests_properties(${_wasi_fd_tests} PROPERTIES TIMEOUT 10)
list(APPEND _wasi_node_tests ${_wasi_fd_tests})

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
    "SHELL:-Xcc -D_WASI_EMULATED_SIGNAL")
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
  list(APPEND _wasi_node_tests dispatch_wasi_clean_swift_consumer
    dispatch_wasi_swift_autolink_consumer)
endif()

if(NOT DISPATCH_WASI_NODE_OK)
  set_tests_properties(${_wasi_node_tests} PROPERTIES DISABLED TRUE)
endif()
