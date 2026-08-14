# WASI tests

## Prerequisites

- Swift 6.3.2 release toolchain
- Matching Swift 6.3.2 `wasm32-unknown-wasip1` SDK
- CMake 3.31 or newer. This is the first release whose documentation recognizes
  `CMAKE_SYSTEM_NAME=WASI`.
- Ninja 1.10 or newer
- Node.js 19.8 or newer (runs the tests in this directory)
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

Single-threaded WASI drains queues and timers cooperatively. `dispatch_main()`
drains useful main-queue work, then parks in the host on armed timers and
event sources; it traps with
`dispatch_main(): no runnable work on single-threaded WASI` only when the
process is fully idle with no timer and no event source armed. It cannot block
forever because nothing else could ever make progress.

**Read and write dispatch sources are supported.** They ride preview1's
`poll_oneoff` fd subscriptions (through wasi-libc `poll(2)`, so the same code
carries to wasip2's `wasi:io/poll` unchanged) and are merged into the
cooperative drain at every wait point: `dispatch_main()`, blocking semaphore /
group / block waits, and timed waits all wake on fd readiness. A blocking wait
whose progress can only come from an armed fd source parks in the host poll
instead of crashing. Guardrails:

- Regular files and directories are never polled (POSIX always-ready; Node's
  uvwasi also rejects fd subscriptions for them) — they merge as
  level-triggered always-ready, like the epoll backend's `EPERM` handling.
- A capability probe at registration crashes with a named diagnostic on hosts
  whose `poll_oneoff` lacks fd subscriptions (browser WASI shims), instead of
  hanging later. An fd that is not open crashes at registration; an fd closed
  while armed crashes at the next wait.
- Indefinite waits use a bounded (1 hour) poll slice in a loop rather than an
  infinite timeout, which WasmKit's host mishandles.
- When no fd source is armed, idle waits remain a single `poll_oneoff` clock
  subscription (nanosecond-precision `nanosleep`), which even single-
  subscription shims support.

**Signal dispatch sources are supported for in-process `raise()`**, riding
wasi-libc's `_WASI_EMULATED_SIGNAL` (the build defines and links it): a
`raise()` anywhere in the guest invokes the emulated handler synchronously,
and the armed source's handler fires on the next drain with the accumulated
count. This works on every runtime, including browser shims, because no host
poll support is involved. There is no asynchronous or cross-process signal
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

Uptime and wall-clock timers fire normally. The WASI backend converts each wall
timer deadline to the uptime clock when it is armed, so later host wall-clock
adjustments do not reposition an already armed timer.

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

Additional focused tests pin behaviors that differentiated the two original
WASI port candidates (PRs #1 and #2):

- `sync-inline.c`, `main-queue-order.c` — inline `dispatch_sync` without a
  drain, and thread-bound main-queue FIFO order (adapted from PR #1's tests).
- `blocking-waits.c` — blocking-wait contracts: `dispatch_block_wait` runs the
  queued block, `dispatch_group_wait(FOREVER)` returns once the group empties,
  and a timed semaphore wait consumes its full timeout instead of returning
  early.
- `api-surface.c` — one binary sweeping the object/attr/block/data/group/
  source families: `dispatch_once`, queue specifics, initially-inactive +
  `dispatch_activate`, suspend/resume, finalizers, `DispatchData` operations,
  block cancel/testcancel, user-data sources with registration/event/cancel
  handlers, timer sources, and wall-clock `dispatch_after`.
- `assert-queue.c` — `dispatch_assert_queue` must trap off-queue and pass
  on-queue; guards the tid-vs-`DLOCK_OWNER_MASK` encoding in `shims/lock.h`
  (a constant tid that masks to zero makes every unlocked queue look owned
  by the current thread).

Event-source tests:

- `write-source.c` — a write source on stdout fires through poll readiness,
  rearms level-triggered after each `EV_DISPATCH` delivery, and parks
  `dispatch_main()` instead of trapping.
- `read-source.c`, `fd-wakeup-wait.c` — the runner pipes stdin only after a
  delay (`--stdin-after`), so passing proves the guest genuinely parks in the
  host poll: once under `dispatch_main()`, once inside a blocking
  `dispatch_semaphore_wait(FOREVER)` satisfied by the read source's handler.
- `signal-source.c` — two `raise(SIGUSR1)` from a queued item deliver one
  handler invocation with count 2.
- `unsupported-source.c` — a read source on an fd that is not open crashes at
  registration with a named diagnostic.

WASI selects `dispatch/wasi/module.modulemap` so static Swift clients autolink
BlocksRuntime and the WASI emulation archives without changing the generic
module map used by Linux and Windows.
