# WASI tests for the single-threaded libdispatch port

Minimal end-to-end tests for the `wasm32-wasip1` (single-threaded) port. They
run under any WASI runtime that provides `clock_time_get` + `poll_oneoff`
(verified on `wasmtime` and `bjorn3/browser_wasi_shim`). The module imports
only `wasi_snapshot_preview1` — no JavaScript.

- `dispatch_wasi_smoke.c` — `dispatch_async`, `dispatch_group_async` +
  `dispatch_group_notify`, and a `dispatch_after` timer on the global queue,
  driven by `dispatch_main()`.
- `dispatch_wasi_mainqueue.c` — the thread-bound **main queue** drains in serial
  FIFO order under `dispatch_main()`, interleaved with global-queue work and a
  main-queue timer.

## Run

Integrated (CTest): when libdispatch is configured for WASI (the toolchain sets
`CMAKE_SYSTEM_NAME=WASI`), the C tests here are built to wasm and registered as
ctest cases. With `wasmtime` on `PATH` they run automatically:
```sh
cmake --build <build-wasi>            # builds dispatch_wasi_* test executables
ctest --test-dir <build-wasi> --output-on-failure
```
Override the runner with `-DWASI_TEST_RUNNER="<runtime>;run"` (e.g. a node+shim
wrapper). The Swift overlay tests (`dispatch_wasi_swift.swift`,
`dispatch_wasi_api.swift`, `swift-demo/`) require the Swift overlay — see
`swift-overlay.md`.

Standalone (no CMake): `run.sh` builds + runs the C tests directly:
```sh
DISPATCH_BUILD=/path/to/build-wasi WASI_SDK=/path/to/wasi-sdk ./run.sh
```

## Scope / limitations (single-threaded port)
- Works: serial/concurrent queues, `async`, `dispatch_group` (+`notify`),
  `DispatchWorkItem`, `dispatch_after` + repeating `DispatchSource` timers,
  `dispatch_main`, the main queue, `concurrentPerform` (inline), `DispatchData`,
  and **`dispatch_sync`** (it runs inline when uncontended — the common case).
- Traps by design (a single-threaded wasm cannot block-and-be-woken): a
  `dispatch_semaphore_wait` / `dispatch_group_wait` that would actually block
  (nothing to signal it), and re-entrant `dispatch_sync` onto the current queue.
  These raise a clean `DISPATCH_CLIENT_CRASH` (wasm trap), not a hang.
- Not supported: file-descriptor / signal `dispatch_source`s (no `poll_oneoff`
  fd readiness in the browser shim).
