# WASI tests

## Prerequisites

- Swift 6.3 or newer release toolchain and the matching
  `wasm32-unknown-wasip1` Swift SDK
- CMake 3.31 or newer (the first release that documents
  `CMAKE_SYSTEM_NAME=WASI`)
- Ninja 1.10 or newer
- wasmtime, or another WASI runtime named with `-DWASI_TEST_RUNNER=...`,
  for the WASI subset of the upstream suite in `tests/` and the focused and
  crash tests here
- Node.js 19.8 or newer for the Node-driven tests (see Build and test)

Tests whose runner is missing or too old still build and are registered but
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
  pending it crashes with `with only main-queue work`, and an expired
  wait runs no work (`timed-wait-main-backlog.c`, `expired-wait.c`).
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
  (`host-event-loop.c`, `scheduler-pump.c`, `scheduler-no-pump.c`).
- Wall-clock timers follow host clock changes at the next wake
  (`wall-timer-forward-jump.c`, `wall-timer-backward-jump.c`). The
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

The host perform operation uses a zero-timeout source harvest. It can consume
readiness the host already made visible to WASI, but it does not notify the
host when a file descriptor becomes ready later. An embedded fd event source
still needs runtime-specific readiness integration. Without a registered
scheduler, fd and signal sources deliver only at blocking waits or inside
`dispatch_main()`.

**Read and write dispatch sources are supported.** They ride preview1's
`poll_oneoff` fd subscriptions (through wasi-libc `poll(2)`, so the same code
carries to wasip2's `wasi:io/poll` unchanged) and are merged into the
cooperative drain at every wait point: `dispatch_main()`, blocking semaphore /
group / block waits, and timed waits all wake on fd readiness. A blocking wait
whose progress can only come from an armed fd source parks in the host poll
instead of crashing. Guardrails:

- Regular files and directories are never polled (POSIX always-ready; Node's
  uvwasi also rejects fd subscriptions for them) - they merge as
  level-triggered always-ready, like the epoll backend's `EPERM` handling.
- Pipe EOF: as on Darwin, a descriptor at EOF stays readable, the handler
  fires, and the client is expected to observe the 0-byte `read()` and cancel
  the source. On hosts whose `poll_oneoff` reports the hangup flag (Node's
  uvwasi), the harvest additionally delivers EOF and stops watching the
  descriptor, mirroring the epoll backend's `EPOLLHUP` handling. Hosts that
  never report hangup (wasmtime, for pipes) cannot distinguish EOF from
  readiness, so a client that never cancels would turn the park into a
  silent hot loop; the port instead converts that into a named crash when a
  park's poll reports readiness 100000 times within two seconds (a rate only
  an instantly-ready descriptor can sustain). `pipe-eof-source.c` pins both
  host shapes.
- A capability probe at registration crashes with a named diagnostic on hosts
  whose `poll_oneoff` lacks fd subscriptions (browser WASI shims), instead of
  hanging later. An fd that is not open crashes at registration; an fd closed
  while armed crashes at the next wait with
  `file descriptor closed while dispatch source is armed`, whether the host
  reports the closed descriptor per subscription (wasi-libc maps that to
  `POLLNVAL`) or by failing the whole poll with `EBADF` (wasmtime's shape;
  `fd-closed-while-armed` pins it via the runner's `--fd-poll-ebadf-after`).
  Caveat: under Node this diagnostic is only reachable for non-stdio fds -
  the guest closing an armed stdio descriptor aborts the host process inside
  libuv (`uv__close` asserts on `fd <= STDERR_FILENO`) before libdispatch
  sees anything.
- Indefinite waits use a bounded (1 hour) poll slice in a loop rather than an
  infinite timeout, which WasmKit's host mishandles.
- When no fd source is armed, idle waits remain a single `poll_oneoff` clock
  subscription (nanosecond-precision `nanosleep`), which even single-
  subscription shims support.

**Signal dispatch sources are supported for in-process `raise()`**, riding
wasi-libc's `_WASI_EMULATED_SIGNAL` (the build defines and links it): a
`raise()` anywhere in the guest invokes the emulated handler synchronously,
and the armed source's handler fires at the next blocking wait or
`dispatch_main()` park with the accumulated count. This works on every
runtime, including browser shims, because no host poll support is involved. There is no asynchronous or cross-process signal
delivery on any current or announced WASI version, and none is possible here.

Runtime support for fd readiness (empirically verified): wasmtime, Node's
built-in `node:wasi`/uvwasi (except regular files, which the always-ready path
covers), and WasmKit all support it; `@bjorn3/browser_wasi_shim` and `uwasi`
do not (fd sources crash loudly at registration there; timers still work on
the former).

The C `DISPATCH_SOURCE_TYPE_PROC` declaration has no linkable WASI definition
because process sources are implemented only by the kevent backend, and WASI
has no processes. Swift process and vnode source APIs are compiled out for
WASI.

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

Add `-DENABLE_SWIFT=YES` to the configure step to build and test the Dispatch
Swift overlay. Swift builds also need `usr/bin/swiftc` in the toolchain and
`swift.xctoolchain/usr/lib/swift_static` in the SDK (or
`SWIFT_WASI_STATIC_RESOURCES_OVERRIDE`).

CTest runs three groups of WASI tests:

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
  crash.
- The remaining tests run under Node. The six host event loop tests need a
  Node reactor host, `run-wasi-host-event-loop-test.mjs`, with a 5 s
  watchdog on the default and `--inline-check` runs; one binary,
  `host-event-loop.c`, serves all six modes. The other tests use
  `run-wasi-test.mjs`, which captures guest output and passes only when the
  guest exit mode and every expected marker or diagnostic match: the
  regular-file IO and crash-log-file tests (`--preopen` of a scratch
  directory and of `/var/tmp`), the two wall-clock jump tests
  (`--wall-clock-offset` shifts the guest's `CLOCK_REALTIME` after a
  delay), and the three runner self-checks. It has no watchdog; a hung
  guest stops at the ctest `TIMEOUT`.

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

Host event loop and scheduler SPI:

- `host-event-loop.c` - a WASI reactor driven by a Node event loop through
  the scheduler SPI: later-turn execution, coalescing, budgeted steps,
  host-owned timers, hand-over at registration (`--late-registration`,
  `--register-in-drain`), a timer-driven perform that keeps the outstanding
  turn (`--timer-race`), a self-reposting serial lane that advances one
  item per perform (`--serial-lane`), and the inline callback diagnostic
  (`--inline-check`).
- `main-root-fairness-perform.c` - the host-loop variant of
  `main-root-fairness.c`: a one-step host turn must not pick the main queue
  forever while a root item is pending. The command module stands in for the
  host and serves each scheduler request with `perform(1, true)`.
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
- `crash-log-env.c` - builds three binaries: with `LIBDISPATCH_LOG=NO`, an
  over-resume (`CRASH_LOG_ASSERT=0`) and an off-queue `dispatch_assert_queue`
  (`CRASH_LOG_ASSERT=1`) still put their reason on stderr after the marker;
  with `LIBDISPATCH_LOG=file` (under Node) the over-resume reason reaches
  stderr while the log goes to the preopened `/var/tmp`. The guest sets the
  variable itself because the runner starts it with an empty environment.
- `alloc-failure.c` - builds two crash binaries linked with a 16 MiB
  `--max-memory`: after the test fills the heap, an object allocation
  (`dispatch_queue_create`) and a continuation allocation (`dispatch_async_f`,
  `ALLOC_FAILURE_ASYNC=1`) crash with `Out of memory` after the `heap exhausted` marker.

`dispatch_main()` paths:

- `dispatch-main-work.c` - `dispatch_main()` runs main-queue work scheduled
  with `dispatch_after`.
- `dispatch-main-idle.c`, `dispatch-main-drain-idle.c` - `dispatch_main()`
  with nothing to run crashes with
  `dispatch_main() with no work, timer, or event source` instead of
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
- `wall-timer-forward-jump.c`, `wall-timer-backward-jump.c` - run under Node
  with `--wall-clock-offset`: a 30 s walltime timer fires at the next drain
  step after the runner moves the wall clock forward by 60 s while the loop
  is busy, and a 500 ms one does not fire early after a 60 s move back.
- `regfile-io.c` - runs under Node: `dispatch_read`, `dispatch_write`, and
  `dispatch_io` on a regular file in a preopened directory, with the payload
  content-asserted: whole-file write/read-back plus a `DISPATCH_IO_RANDOM`
  byte-range read.
- `clean-consumer.c` - a program that uses Dispatch without ever pumping (no
  `dispatch_main()`, no blocking wait) exits 0: an uncontended
  `dispatch_sync` runs inline, and releasing the queue submits nothing.

The three runner self-checks (`dispatch_wasi_runner_wrong_mode`,
`dispatch_wasi_runner_missing_diagnostic`,
`dispatch_wasi_runner_missing_marker`) run `run-wasi-test.mjs` against
`smoke.c` and `dispatch-main-idle.c` through `expect-runner-failure.cmake`
and pass only when the runner fails with its own failure line: a successful
guest declared as a crash, a crash without the expected diagnostic, and a
crash without an expected marker.

Event-source and Swift tests. These run under `run-wasi-test.mjs` through
`add_wasi_node_test()`, or with explicit runner options. They are not on the
bsdtests harness yet.


- `write-source.c` - a write source on stdout fires through poll readiness,
  rearms level-triggered after each `EV_DISPATCH` delivery, and parks
  `dispatch_main()` instead of trapping.
- `read-source.c`, `fd-wakeup-wait.c`, `group-fd-wakeup.c` - the runner
  pipes stdin only after a delay (`--stdin-after`), so passing proves the
  guest genuinely parks in the host poll: once under `dispatch_main()`, once
  inside a blocking `dispatch_semaphore_wait(FOREVER)`, and once inside a
  blocking `dispatch_group_wait(FOREVER)`, the latter two satisfied by the
  read source's handler.
- `regfile-io.c` - `dispatch_read`/`dispatch_write`/`DispatchIO` on a
  regular file in a preopened directory (the runner's `--preopen`), with the
  payload content-asserted through the always-ready path: whole-file
  write/read-back plus a `DISPATCH_IO_RANDOM` byte-range read.
- `pipe-eof-source.c` - pipe EOF against a source that never cancels: on
  hangup-reporting hosts the source is dropped and `dispatch_main()` traps
  idle; with the hangup flag suppressed (`--suppress-poll-hangup`, the
  wasmtime shape) the permanently ready park crashes with the named
  spin diagnostic instead of looping silently.
- `signal-source.c` - two `raise(SIGUSR1)` from a queued item deliver one
  handler invocation with count 2.
- `unsupported-source.c` - a read source on an fd that is not open crashes at
  registration with a named diagnostic.

- `clean-consumer.swift` - with `ENABLE_SWIFT`, a Swift program that uses
  Dispatch links and exits 0, once with explicit link options and once
  through module-map autolinking.

WASI selects `dispatch/wasi/module.modulemap` so static Swift clients autolink
BlocksRuntime and the WASI emulation archives without changing the generic
module map used by Linux and Windows.
