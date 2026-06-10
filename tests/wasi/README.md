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
```sh
# after building libdispatch for wasm32-wasip1 (see run.sh header)
DISPATCH_BUILD=/path/to/build-wasi WASI_SDK=/path/to/wasi-sdk ./run.sh
```

## Scope / limitations (single-threaded port)
- Works: queues, `async`, `dispatch_group` (+`notify`), `dispatch_after`/timers,
  `dispatch_main`, the main queue.
- Traps by design: `dispatch_sync`, and blocking `dispatch_semaphore_wait` /
  `dispatch_group_wait` (a single-threaded wasm cannot block-and-be-woken).
- Not supported: file-descriptor / signal `dispatch_source`s (no `poll_oneoff`
  fd readiness in the browser shim).
