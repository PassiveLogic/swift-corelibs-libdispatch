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
drains useful main-queue work and armed timers, then traps with
`dispatch_main(): no runnable work on single-threaded WASI` when the process is
fully idle. It cannot block forever because there is no other thread that can
make progress.

Read, write, and signal dispatch source types remain available to C and Swift,
but they fail with a named diagnostic when registration reaches the WASI event
backend. Swift read/write source factories and `DispatchIO` therefore remain
visible but also fail loudly when they attempt unsupported file-descriptor
registration. The C `DISPATCH_SOURCE_TYPE_PROC` declaration has no linkable
WASI definition because process sources are implemented only by the kevent
backend. Swift process and vnode source APIs are compiled out for WASI.

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

WASI selects `dispatch/wasi/module.modulemap` so static Swift clients autolink
BlocksRuntime and the WASI emulation archives without changing the generic
module map used by Linux and Windows.
