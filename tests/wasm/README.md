# WASI tests

## Prerequisites

- Swift 6.3 or newer release toolchain and the matching
  `wasm32-unknown-wasip1` Swift SDK
- CMake 3.31 or newer (the first release that documents
  `CMAKE_SYSTEM_NAME=WASI`)
- Ninja 1.10 or newer
- wasmtime, or another WASI runtime named with `-DWASI_TEST_RUNNER=...`,
  for example `-DWASI_TEST_RUNNER="wasmkit;run"`

When the runner is missing, the tests still build and are registered but
disabled.

Official Swift toolchains normally live under
`~/Library/Developer/Toolchains`, and Swift SDK artifact bundles installed
with `swift sdk install` under `~/Library/org.swift.swiftpm/swift-sdks`.
Set these paths before configuring:

```sh
export SWIFT_WASI_TOOLCHAIN_PATH="$HOME/Library/Developer/Toolchains/swift-6.3-RELEASE.xctoolchain"
export SWIFT_WASI_SDK_PATH="$HOME/Library/org.swift.swiftpm/swift-sdks/swift-6.3-RELEASE_wasm.artifactbundle/swift-6.3-RELEASE_wasm/wasm32-unknown-wasip1"
```

`SWIFT_WASI_TOOLCHAIN_PATH` must be the `.xctoolchain` root containing
`usr/bin/clang`, `usr/bin/clang++`, `usr/bin/llvm-ar`, and
`usr/bin/llvm-ranlib`. `SWIFT_WASI_SDK_PATH` must be the target directory
containing both `WASI.sdk` and
`swift.xctoolchain/usr/lib/clang/lib/wasip1/libclang_rt.builtins-wasm32.a`.
The toolchain validates the WASI compiler-rt builtins archive under the SDK
path on every configure. A nonstandard SDK layout can point at it with
`DISPATCH_WASI_BUILTINS_OVERRIDE`.

## WASI semantics

Single-threaded WASI drains queues and timers cooperatively. The design is
documented at its source: `src/event/event_wasi.c` (drain order and pump
points), `private/private.h` (host event loop SPI), and `src/shims/lock.h`
(what a blocking wait may drain). The consequences, and the tests that pin
them:

- `dispatch_async` never runs its block before the call returns; it runs at
  the next pump point (`deferred-submission.c`).
- A blocking wait never drains the main queue; with only main-queue work
  pending it crashes with a deadlock message that ends in
  `with only main-queue work`, and an expired wait runs no work
  (`timed-wait-main-backlog.c`, `expired-wait.c`).
- Drains do not nest: blocks submitted from a running item run in the outer
  drain, FIFO within one queue, one item per step on a serial queue
  (`deferred-submission.c`, `smoke.c`).
- Work submitted from inside a caller-held critical section never runs
  under that lock (`sync-nested-async.c`, `async-and-wait.c`,
  `specific-destructor.c`).
- A pumped item runs with a worker identity and crashes with the deadlock
  diagnostic when it re-enters what the blocked frame holds
  (`pumped-ownership.c`, `pumped-held-queue-sync.c`, `pumped-once-reenter.c`).
- Nothing can hold the sole thread; every self-reposting shape yields after
  one item (the fairness tests, `reposting-workloop.c`,
  `self-triggering-source.c`).
- A wait inside a running item pumps nothing: indefinite crashes at once,
  timed sleeps to its own deadline (`nested-wait.c`, `nested-timed-wait.c`,
  `wait-diagnostics.c`).
- Hosts pass `consumes_scheduled_turn=true` on exactly one perform call in
  the turn the callback requested; timer-driven calls pass `false`. The
  scheduler callback must return without running Dispatch
  (`scheduler-pump.c`, `scheduler-no-pump.c`, `main-queue-perform.c`).
- Wall-clock timers follow host clock changes at the next wake. The
  remaining limitation: a change while the sole thread sleeps takes effect
  when that sleep ends; Darwin and Linux reposition the timer immediately.
- `_libdispatch_init` runs at constructor priority 101, every crash reason
  reaches stderr regardless of `LIBDISPATCH_LOG`, and an allocation failure
  is a named crash (`constructor-main-queue.c`, `crash-log-env.c`,
  `alloc-failure.c`).

The backend is for plain, single-threaded `wasip1` only; wasm atomics or
`-pthread` hit an `#error` in `event_wasi.c`. File-descriptor and signal
dispatch sources are not supported; registering one crashes with
`file-descriptor and signal dispatch sources are not supported on
single-threaded WASI`. Timer sources and user-data sources work.

## Build and test

From the repository root, configure, build, and test the C library with one
command ladder:

```sh
cmake -S . -B build-wasi -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/WASI.cmake \
  -DSWIFT_WASI_TOOLCHAIN_PATH="$SWIFT_WASI_TOOLCHAIN_PATH" \
  -DSWIFT_WASI_SDK_PATH="$SWIFT_WASI_SDK_PATH" \
  -DBUILD_TESTING=ON
cmake --build build-wasi
ctest --test-dir build-wasi --output-on-failure
```

The Swift overlay does not support WASI; configuring with
`ENABLE_SWIFT=YES` is rejected.

CTest runs two groups of WASI tests:

- The single-thread-compatible subset of the upstream `tests/` suite (see
  `DISPATCH_C_TESTS` in `tests/CMakeLists.txt`) runs under
  `WASI_TEST_RUNNER` (default `wasmtime`), which only needs to propagate the
  guest exit code.
- The focused behavior tests and crash tests in this directory run under
  `WASI_TEST_RUNNER` the same way. `add_wasi_unit_test()` builds a test on
  the upstream bsdtests harness; each check prints a `[PASS]` or `[FAIL]`
  line, and the test passes when the guest exits 0. `add_wasi_crash_test()`
  registers a test that must crash; `expect-crash.cmake` passes only when
  the guest exits non-zero and its output contains one stable substring of
  the expected diagnostic plus every marker the test prints before the
  crash. `regfile-io` is the one test with a runner argument: `--dir` grants
  the guest a scratch directory, which both wasmtime and WasmKit accept.

`wasi-test.h` holds the shared helpers: `wasi_test_now_ns()` reads the
uptime clock, and `wasi_test_pump()` is a top-level wait that drains
everything the test submitted to the default root queue. The tests, by
subject:

Ordering and fairness:

- `barrier-order.c`, `qos-order.c` - a barrier submitted between two
  concurrent-queue items orders all three, and root queues drain highest
  QoS first even when the HIGH item was submitted after the LOW one.
- `sync-inline.c`, `main-queue-order.c` - inline `dispatch_sync` without a
  drain, and thread-bound main-queue FIFO order interleaved with a
  global-queue item.
- `deferred-submission.c` - top-level async does not run before the call
  returns and runs at the next pump, a wait drains nested root submissions
  but leaves a nested main-queue item pending, group notify follows earlier
  root submissions, and the pending main-queue item runs first in
  `dispatch_main()`.
- `sync-nested-async.c` - async work submitted from a `dispatch_sync` body
  that syncs back onto the outer queue runs at the next pump, after the
  outer sync released the lock, never inline (a spurious "already owned by
  current thread" crash); so does a destructor batch submitted mid-dispose.
- `async-and-wait.c` - work submitted from a `dispatch_async_and_wait` body
  runs at the next pump for both funnels, the plain block and the
  `dispatch_block_create()` private-data block.
- `specific-destructor.c` - replacing a queue-specific value runs the old
  value's destructor at a later pump, never on the submitting stack under
  the specifics lock.
- `pumped-ownership.c` - a root item that a top-level wait pumps runs as a
  worker: it does not inherit the drain locks, thread frames, or main-queue
  ownership of the code that waits (`dispatch_assert_queue` checks).
- `pumped-mgr-under-sync.c` - a top-level wait inside an uncontended
  `dispatch_sync` block pumps two manager items. The manager drain does not
  narrow, although the interrupted sync frame belongs to the main queue.
- `root-fairness.c` - a self-replenishing HIGH root does not starve a LOW
  root item: the drain rotates among pending roots.
- `main-root-fairness.c` - main-queue work that reposts itself gives one
  pending root item the next turn after each main step under
  `dispatch_main()`; a starved root fails after a fixed tick count.
- `main-lane-fairness.c` - a serial queue targeting the main queue that
  re-posts a tick runs one item per main step and then moves to the back of
  the main queue, so root work and a 10 ms timer still get a turn.
- `main-source-handlers.c` - a source on the main queue applies all of its
  queued handler changes before it calls a handler. Its own drain does not
  narrow, so the second event calls the last handler that was set.
- `serial-lane-fairness.c` - a serial queue that keeps re-posting to itself
  runs one item per root step and is re-enqueued, so a timer, a LOW root
  item, and a main-queue item all run while it is busy; a lane targeting
  another serial queue behaves the same, and each lane keeps FIFO.
- `reposting-workloop.c`, `self-triggering-source.c` - a workloop item that
  queues itself again, and a data source on the overcommit root whose
  handler merges into itself, run one item per turn, so a timed wait still
  times out near its deadline.

Waits and deadlock diagnostics:

- `blocking-waits.c` - blocking-wait contracts: `dispatch_block_wait` runs the
  queued block, `dispatch_group_wait(FOREVER)` returns once the group empties,
  and a timed semaphore wait that nothing signals times out only after its
  250 ms timeout, not early.
- `expired-wait.c` - a semaphore or group wait whose deadline already passed
  (the Swift `wait(timeout: .now())` shape) reports a timeout without
  running queued work and strands no count; a wait with time left still
  runs the work that satisfies it.
- `timed-wait-main-backlog.c` - timed and expired top-level waits never run
  main-queue blocks, so a main-queue backlog cannot make them return late;
  root work still runs one item per step, and `dispatch_main()` then runs
  the backlog in order.
- `serial-lane-wait.c` - top-level waits get the thread back from a serial
  queue that keeps re-posting to itself: a timed semaphore wait returns at
  its deadline and a group wait returns once a lane item leaves the group.
- `sema-late-signal.c` - a timed semaphore wait whose deadline lapses while
  the drain is running the item that signals it reports success and consumes
  the signal, never a timeout with the count stranded.
- `cancel-and-wait.c` - `dispatch_source_cancel_and_wait` returns at top
  level for a timer source (the wait pumps the manager queue until the
  source unregisters) and inside a work item for an inline cancel.
- `async-and-wait-pump.c` - a blocking wait inside a `dispatch_async_and_wait`
  body is a top-level wait and pumps; each pumped step (root item, group
  item, timer; a pending main-queue item is left alone) starts with no
  borrowed workloop handle and gives it back, for the plain, barrier, and
  private-data entry points.
- `nested-wait.c`, `nested-timed-wait.c` - waits inside a drained work item
  pump nothing: an indefinite semaphore wait crashes at once with
  `Deadlock: semaphore wait from a work item` after the marker the
  item prints, and a timed one sleeps to its own deadline, not to a later
  timer.
- `wait-diagnostics.c` - builds three crash binaries, one per `WAIT_DIAG_*`
  define: a `dispatch_sync` behind queued work, a `dispatch_group_wait`, and
  a `dispatch_source_cancel_and_wait` on a timer, each inside a drained work
  item, crash after the marker with a deadlock diagnostic that names the
  public call that blocked.
- `pumped-held-queue-sync.c`, `pumped-once-reenter.c` - a pumped item that
  syncs onto the queue the blocked frame holds crashes with
  `Deadlock: dispatch_sync or dispatch_async_and_wait from a work item`,
  and one that enters the `dispatch_once` the blocked frame is initializing
  crashes with `Deadlock: lock contended on single-threaded WASI`, each
  after its marker and never with the generic recursive-lock crash.
- `once-corrupt-predicate.c` - a garbage `dispatch_once_t` names an owner
  that cannot exist on one thread; `dispatch_once` crashes with
  `Corrupt lock value` after the marker, not spinning.

Host event loop and scheduler SPI, with the command module standing in for
the host:

- `main-root-fairness-perform.c` - the host-loop variant of
  `main-root-fairness.c`: a one-step host turn must not pick the main queue
  forever while a root item is pending. The test serves each scheduler
  request with `perform(1, true)`.
- `main-queue-perform.c` - a main-queue drain that empties the queue leaves
  no pending main work: perform returns `true` only when work remains, and
  one main-queue batch requests exactly one host turn.
- `scheduler-no-pump.c` - calls that do not pump are allowed inside the
  scheduler callback (`dispatch_async`, an uncontended `dispatch_sync`, a
  `DISPATCH_TIME_NOW` wait); none runs the block the poking `dispatch_async`
  recorded, the callback runs once for two pokes, and the requested turn
  then runs both blocks.
- `scheduler-pump.c` - builds five crash binaries, one per
  `SCHEDULER_PUMP_MODE`: an indefinite semaphore wait, a timed semaphore
  wait, `dispatch_main()`, a contended `dispatch_sync`, and an indefinite
  group wait, each inside the scheduler callback, crash with
  `Scheduler ran Dispatch inline` after the marker,
  before the recorded block can run.

Startup, logging, and allocation:

- `constructor-main-queue.c` - a default-priority client constructor sees an
  initialized libdispatch: it runs on the main queue, can post deferred
  main-queue work and wait for global-queue work, and the deferred block
  runs on the main queue under `dispatch_main()`.
- `crash-log-env.c` - builds two binaries: with `LIBDISPATCH_LOG=NO`, an
  over-resume (`CRASH_LOG_ASSERT=0`) and an off-queue `dispatch_assert_queue`
  (`CRASH_LOG_ASSERT=1`) still put their reason on stderr after the marker.
  The guest sets the variable itself because the runner starts it with an
  empty environment.
- `alloc-failure.c` - builds two crash binaries linked with a 16 MiB
  `--max-memory`: after the test fills the heap, an object allocation
  (`dispatch_queue_create`) and a continuation allocation (`dispatch_async_f`,
  `ALLOC_FAILURE_ASYNC=1`) crash with `Out of memory` after the `heap exhausted` marker.

`dispatch_main()` paths:

- `dispatch-main-work.c` - `dispatch_main()` runs main-queue work scheduled
  with `dispatch_after`.
- `dispatch-main-idle.c`, `dispatch-main-drain-idle.c` - `dispatch_main()`
  with nothing to run crashes with
  `dispatch_main() with no work or timer` instead of
  parking forever: at once when nothing was ever queued, and after the only
  main-queue timer fired and drained (after the timer's marker).
- `nested-dispatch-main.c` - `dispatch_main()` called from inside a drained
  work item crashes with `dispatch_main() called from a work item` after the marker; an armed timer must not turn that into a sleep.

API surface:

- `smoke.c` - one binary over the basic surface, pumped only by top-level
  waits: `dispatch_once`, queue specifics, serial FIFO order with a backlog
  submitted from inside a running item, sync and barrier sync, semaphore
  timing, group wait and notify (a notify queued on a serial queue runs
  once that queue drains, not during the group wait), `dispatch_after` and
  a repeating timer, `dispatch_apply`, and `dispatch_data` concatenation.
- `api-surface.c` - one binary sweeping the object/attr/block/data/group/
  source families: `dispatch_once`, queue specifics, initially-inactive +
  `dispatch_activate`, suspend/resume, finalizers, `dispatch_data`
  operations, block cancel/testcancel, user-data sources with all three
  handlers, timer sources, and wall-clock `dispatch_after`.
- `assert-queue.c` - `dispatch_assert_queue` must pass on-queue and crash
  off-queue with `Block was expected to execute on queue` after the marker
  the on-queue checks print. It guards the tid-vs-`DLOCK_OWNER_MASK`
  encoding in `shims/lock.h` (a constant tid that masks to zero makes every
  unlocked queue look owned by the current thread).
- `wall-timer.c` - a `dispatch_after` with a `dispatch_walltime()` deadline
  fires on a serial queue while main blocks in a timed semaphore wait, not
  before its 20 ms deadline.
- `regfile-io.c` - `dispatch_read`, `dispatch_write`, and `dispatch_io` on
  a regular file in the directory named by its argument, with the payload
  content-asserted: whole-file write/read-back plus a `DISPATCH_IO_RANDOM`
  byte-range read.
- `clean-consumer.c` - a program that uses Dispatch without ever pumping (no
  `dispatch_main()`, no blocking wait) exits 0: an uncontended
  `dispatch_sync` runs inline, and releasing the queue submits nothing.
