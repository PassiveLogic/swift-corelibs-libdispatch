# WASI tests

## Prerequisites

- Swift 6.3.2 release toolchain
- Matching Swift 6.3.2 `wasm32-unknown-wasip1` SDK
- CMake 3.31 or newer. This is the first release whose documentation recognizes
  `CMAKE_SYSTEM_NAME=WASI`.
- Ninja 1.10 or newer
- Node.js 19.8 or newer (runs the tests in this directory; when Node is
  missing or too old these tests still build and are registered but disabled)
- wasmtime, or another WASI runtime named with `-DWASI_TEST_RUNNER=...`
  (runs the WASI subset of the upstream test suite in `tests/`; when the
  runner is missing those tests are registered but disabled)

Official Swift toolchains installed for the current user normally live under
`~/Library/Developer/Toolchains`. Swift SDK artifact bundles installed with
`swift sdk install` normally live under
`~/Library/org.swift.swiftpm/swift-sdks`.

Set these paths before configuring:

```sh
export SWIFT_WASI_TOOLCHAIN_PATH="$HOME/Library/Developer/Toolchains/swift-6.3.2-RELEASE.xctoolchain"
export SWIFT_WASI_SDK_PATH="$HOME/Library/org.swift.swiftpm/swift-sdks/swift-6.3.2-RELEASE_wasm.artifactbundle/swift-6.3.2-RELEASE_wasm/wasm32-unknown-wasip1"
```

`SWIFT_WASI_TOOLCHAIN_PATH` must be the `.xctoolchain` root containing
`usr/bin/clang`, `usr/bin/clang++`, and, for Swift builds, `usr/bin/swiftc`.
`SWIFT_WASI_SDK_PATH` must be the target directory containing both `WASI.sdk`
and `swift.xctoolchain/usr/lib/swift_static`.

The toolchain derives and validates the Swift static resources and WASI
compiler-rt builtins from the SDK path on every configure. Nonstandard SDK
layouts can override them with `SWIFT_WASI_STATIC_RESOURCES_OVERRIDE` and
`DISPATCH_WASI_BUILTINS_OVERRIDE`.

## WASI semantics

By default, single-threaded WASI drains queues and timers cooperatively. `dispatch_main()`
drains useful main-queue work, then parks in the host on armed timers and
event sources; it traps with
`dispatch_main(): no runnable work on single-threaded WASI` only when the
process is fully idle with no timer and no pollable event source armed. It
cannot block forever because nothing else could ever make progress. An armed
signal source alone does not keep the park alive: signals are in-process
`raise()` only, so a parked sole thread with nothing else runnable could
never be signaled - a signal-source-only `dispatch_main()` traps as truly
idle, deliberately.

**Eager submission is the behavior when no host scheduler is registered.** At top level
(outside any drain), a block submitted with `dispatch_async` runs to
completion on the submitting stack before the call returns. This is the port's
one deliberate divergence from threaded Dispatch, and it is what makes the
library usable in embedded hosts (browser modules, reactors) that never call
`dispatch_main()`: without it, queued work would never execute there. The
costs: callbacks can re-enter code on the same stack that
native callers do not expect to be re-entered there, and cross-queue ordering
differs from threaded platforms (a `dispatch_group_notify` installed after the
group already drained runs before later submissions). Blocks submitted from
inside a running work item defer to the outer drain - there are no nested
drains - and the outer drain resumes in category priority order: due timers,
the manager queue, the main queue, then root queues by QoS. All of this is
pinned by `eager-drain.c`; a change in these orderings is a behavior change,
not an implementation detail. Two boundary contracts to keep in mind: inside
a caller-held critical section (an inline `dispatch_sync` body, a
`dispatch_once` initializer, dispose, `dispatch_queue_set_specific`) pokes
only record work and the flush is deferred to the section's exit, but a
*blocking wait* issued inside such a section still pumps queued work under
the caller's lock (see `WAIT-PUMPING-AUDIT.md`); and fd/signal sources
deliver only at blocking waits or inside `dispatch_main()` - eager drains
fire due timers but never harvest fd readiness or pending signals, so an
embedding that neither blocks nor calls `dispatch_main()` will not observe
source events.

**An embedded host can drive Dispatch from its own event loop.** The private
WASI event-loop SPI registers a scheduler that requests a later host callback
instead of draining from a poke. Requests are coalesced while a callback is
outstanding. The host calls `_dispatch_wasi_event_loop_perform()` with a
nonzero drain-phase budget. A manager or main-queue phase may drain a captured
queue snapshot. When perform returns `true`, Dispatch has already requested
or retained one outstanding turn through the registered scheduler. Hosts pass
`consumes_scheduled_turn=true` only from that scheduler callback; timer-driven
calls pass `false` so they do not clear a callback that is still queued.
`_dispatch_wasi_event_loop_next_timer_delay()` returns a relative nanosecond
delay, or `-1` when no timer is armed, so the host can own one replaceable
timer. The scheduler must not call perform inline; doing so traps with a named
diagnostic. Registration must happen once, before asynchronous Dispatch use.
The Node reactor test in `host-event-loop.c` verifies deferred submission,
step-budgeted root-queue progress, wakeup coalescing, and timers firing without
`dispatch_main()`.

The host perform operation uses a zero-timeout source harvest. It can consume
readiness the host already made visible to WASI, but it does not notify the host
when a file descriptor becomes ready later. An embedded fd event source still
needs runtime-specific readiness integration. Dispatch blocking waits retain
their existing cooperative behavior in both modes.

**Wall-clock timers are anchored at arm time - a documented WASI limitation.**
A wall-deadline timer is converted to the uptime clock when it is armed, so a
later host wall-clock adjustment does not reposition an armed timer (Darwin
repositions them). This is deliberate: WASI has no clock-change notification
mechanism, so tracking adjustments reliably is not possible; anchoring gives
one predictable behavior instead of a racy approximation. `wall-timer.c`
pins it.

**wasip1-threads boundary.** The cooperative backend is for plain,
single-threaded `wasip1` only, and the build enforces that: compiling with
wasm atomics or `-pthread` (`__wasm_atomics__` / `_REENTRANT`) hits an
`#error` in `event_wasi.c`. A future `wasip1-threads` port should not extend
this backend - with real threads, the normal worker-pool model (internal
pthread workqueue plus a poll-driven manager) is the right shape, and the
compile-time guard is the seam where that fork happens.

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
cmake -S . -B build-wasi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/WASI.cmake -DSWIFT_WASI_TOOLCHAIN_PATH="$SWIFT_WASI_TOOLCHAIN_PATH" -DSWIFT_WASI_SDK_PATH="$SWIFT_WASI_SDK_PATH" -DBUILD_TESTING=ON && cmake --build build-wasi && ctest --test-dir build-wasi --output-on-failure
```

Add `-DENABLE_SWIFT=YES` to the configure step to build and test the Dispatch
Swift overlay.

CTest runs two groups of WASI tests. The single-thread-compatible subset of
the upstream `tests/` suite (see `DISPATCH_C_TESTS` in `tests/CMakeLists.txt`)
runs under `WASI_TEST_RUNNER` (default `wasmtime`), which only needs to
propagate the guest exit code. The focused tests in this directory run under
Node WASI with a watchdog; that runner captures guest output and passes only
when the guest exit mode and every expected output marker or diagnostic
match. Focused tests cover deferred barrier ordering, initial root-queue QoS
order, fairness across self-replenishing roots, uptime and wall timers, signal
and file-descriptor source failures, and both useful and immediately idle
`dispatch_main()` paths.

Additional focused tests pin behaviors that differentiated earlier
iterations of the WASI port:

- `sync-inline.c`, `main-queue-order.c` - inline `dispatch_sync` without a
  drain, and thread-bound main-queue FIFO order.
- `blocking-waits.c` - blocking-wait contracts: `dispatch_block_wait` runs the
  queued block, `dispatch_group_wait(FOREVER)` returns once the group empties,
  and a timed semaphore wait consumes its full timeout instead of returning
  early.
- `api-surface.c` - one binary sweeping the object/attr/block/data/group/
  source families: `dispatch_once`, queue specifics, initially-inactive +
  `dispatch_activate`, suspend/resume, finalizers, `DispatchData` operations,
  block cancel/testcancel, user-data sources with registration/event/cancel
  handlers, timer sources, and wall-clock `dispatch_after`.
- `assert-queue.c` - `dispatch_assert_queue` must trap off-queue and pass
  on-queue; guards the tid-vs-`DLOCK_OWNER_MASK` encoding in `shims/lock.h`
  (a constant tid that masks to zero makes every unlocked queue look owned
  by the current thread).
- `eager-drain.c` - pins the eager-submission semantics described above:
  top-level async runs before the call returns, nested submissions defer and
  drain main-before-root, group notify precedes later submissions.

Event-source tests:

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

WASI selects `dispatch/wasi/module.modulemap` so static Swift clients autolink
BlocksRuntime and the WASI emulation archives without changing the generic
module map used by Linux and Windows.
