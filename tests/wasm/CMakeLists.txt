# Focused behavior tests (add_wasi_unit_test) and crash tests
# (add_wasi_crash_test) run under WASI_TEST_RUNNER, like the upstream suite in
# tests/CMakeLists.txt, and are DISABLED only when that runner is missing.

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
  "dispatch_main() with no work or timer" 5)
add_wasi_crash_test(dispatch_wasi_dispatch_main_drain_idle
  dispatch-main-drain-idle.c
  "dispatch_main() with no work or timer" 5
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

# Regular-file dispatch_read, dispatch_write and dispatch_io with payload
# checks. The runner grants the guest a scratch directory and the test gets
# its path as the first argument.
set(_wasi_regfile_scratch "${CMAKE_CURRENT_BINARY_DIR}/regfile-io-scratch")
file(MAKE_DIRECTORY "${_wasi_regfile_scratch}")
add_executable(dispatch_wasi_regfile_io regfile-io.c)
target_include_directories(dispatch_wasi_regfile_io PRIVATE
  ${DISPATCH_WASI_TEST_INCLUDE_DIRS})
target_compile_options(dispatch_wasi_regfile_io PRIVATE -fblocks -Wall)
target_link_libraries(dispatch_wasi_regfile_io PRIVATE dispatch)
add_test(NAME dispatch_wasi_regfile_io
  COMMAND ${WASI_TEST_RUNNER} --dir "${_wasi_regfile_scratch}"
    "$<TARGET_FILE:dispatch_wasi_regfile_io>" "${_wasi_regfile_scratch}")
set_tests_properties(dispatch_wasi_regfile_io PROPERTIES TIMEOUT 10)
if(NOT WASI_TEST_RUNNER_EXECUTABLE)
  set_tests_properties(dispatch_wasi_regfile_io PROPERTIES DISABLED TRUE)
endif()
