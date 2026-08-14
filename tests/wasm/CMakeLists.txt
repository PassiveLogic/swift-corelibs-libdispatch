find_program(NODE_EXECUTABLE node REQUIRED)
execute_process(
  COMMAND "${NODE_EXECUTABLE}" --version
  RESULT_VARIABLE NODE_VERSION_RESULT
  OUTPUT_VARIABLE NODE_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT NODE_VERSION_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to query Node version with ${NODE_EXECUTABLE}")
endif()
string(REGEX REPLACE "^v" "" NODE_VERSION "${NODE_VERSION}")
if(NODE_VERSION VERSION_LESS 19.8)
  message(FATAL_ERROR "Node 19.8 or newer is required, found ${NODE_VERSION}")
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
endfunction()

add_wasi_test(dispatch_wasi_smoke smoke.c success "ALL PASS (9/9)" 20)
add_wasi_test(dispatch_wasi_unsupported_source unsupported-source.c crash
  "file-descriptor and signal dispatch sources are unsupported on single-threaded WASI" 5)
add_wasi_test(dispatch_wasi_signal_source signal-source.c crash
  "file-descriptor and signal dispatch sources are unsupported on single-threaded WASI" 5)
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
