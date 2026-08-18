# Multi-threaded dispatch on wasm32-unknown-wasip1-threads

Research notes for making libdispatch multi-threaded on WebAssembly.
Fork-only material. Findings below marked "proven" were reproduced on
this machine; the commands live at the end of each section.

## Summary

- The threads toolchain works end to end today: a 4-thread pthread
  program with atomics, semaphores, and accurate timed waits passes
  under wasmtime v24 LTS. Proven.
- `_REENTRANT` is the compile-time discriminator: the threads triple
  defines it, the single-threaded triple does not. Proven.
- The slice A seams need no change for threads mode. The poke-defer
  hooks compile to no-ops there, exactly as they do on Linux. No API
  signature changes. This answers the #5 pre-upstream blocker.
- The runtime landscape is the main constraint, not the toolchain:
  current wasmtime (v47) has removed wasi-threads support. Only
  wasmtime v24 LTS (proven), WAMR, and toywasm (both unverified) run
  it. The wasi-threads proposal is deprecated upstream in favor of
  shared-everything-threads. Threads mode should therefore be an
  experimental build knob; the cooperative backend stays the default.

## Toolchain facts (proven)

SDK: `swift-wasm-6.3-RELEASE-wasm32-unknown-wasip1-threads`
artifactbundle from swiftwasm/swift releases, installed via
`swift sdk install <url> --checksum <sha256>`. Layout:

    <bundle>/6.3-RELEASE-wasm32-unknown-wasip1-threads/
        wasm32-unknown-wasip1-threads/WASI.sdk      <- sysroot
        wasm32-unknown-wasip1-threads/swift.xctoolchain

Predefines added by `--target=wasm32-unknown-wasip1-threads -pthread`:
`_REENTRANT`, `__wasm_atomics__`, `__wasm_bulk_memory__`. The plain
wasip1 triple defines none of these.

Two working compile+link combinations, both proven against the
pthread smoke test:

1. wasi-sdk 33 clang with its own sysroot.
2. Swift 6.3.3 host clang + the artifactbundle sysroot + an explicit
   compiler-rt builtins archive (the host toolchain does not ship
   builtins for the threads triple). wasi-sdk 33 provides one at
   `lib/clang/22/lib/wasm32-unknown-wasip1-threads/libclang_rt.builtins.a`
   and it links fine from the Swift clang. This mirrors the
   `DISPATCH_WASI_BUILTINS` pattern the WASI toolchain file already
   uses.

Required link flags, or the module gets a private non-shared memory
and every `pthread_create` fails with EAGAIN (we hit this):

    -Wl,--import-memory,--export-memory,--max-memory=<bytes>

`-pthread` alone adds `--shared-memory` but does not import/export the
memory, and wasi-threads hosts can only spawn threads against an
imported shared memory.

## Sysroot surface (proven by header inspection)

wasi-libc's posix THREAD_MODEL is full musl: `pthread_create`, `join`,
`detach`, mutexes (incl. recursive), condvars with `condattr_setclock`,
rwlocks, barriers, once, TLS keys, `pthread_getname_np`. `semaphore.h`
has `sem_init/wait/trywait/timedwait/post/getvalue`. Timed waits work
and are accurate: `sem_timedwait` 150 ms deadline observed at 160 ms,
`pthread_cond_timedwait` 100 ms observed at 100 ms (proven under
wasmtime v24).

Still absent, unchanged from single-threaded WASI: signals to
processes (`kill`), `sigaction`, `fork`/`exec`, `pipe(2)` and socket
creation, `mmap`. The capability model is the same; only threads and
shared memory are new.

## Runtime support for wasi-threads

| Runtime | Status |
|---|---|
| wasmtime v24 LTS | Works: `wasmtime run -S threads app.wasm`. Proven. |
| wasmtime v47 (current) | Removed. `-S threads` rejected; `wasi::thread-spawn` import unfulfilled. Proven. |
| Node.js built-in WASI | No wasi-threads. A worker_threads-based shim is possible but nobody ships one. |
| browser (bjorn3 shim, uwasi) | No `thread-spawn`. Same shim caveat. |
| WasmKit | No wasi-threads. |
| WAMR, toywasm | Claim support. Not verified here. |

Strategic context: the wasi-threads proposal is frozen/deprecated;
the successor is the shared-everything-threads proposal (component
model era). Compiled modules still target `wasi::thread-spawn` today.
Consequence: threads mode is a forward-looking experiment with one
solid LTS runtime, not a replacement for the cooperative backend.

## Gate audit: every `__wasi__` conditional in the port

Classification: KEEP (correct for both modes), SPLIT (single-thread
only; re-gate on `defined(__wasi__) && !defined(_REENTRANT)`), THREADS
(needs a threaded replacement).

| Site | Class | Notes |
|---|---|---|
| `internal.h` header excludes (sys/mount, sysctl, syslog) | KEEP | libc surface identical |
| `shims.h`, `shims/getprogname.h`, `shims/time.h`, `shims/hw_config.h`, `transform.c` | KEEP | byte order, progname, clocks, cpu count unchanged (`sysconf(_SC_NPROCESSORS_ONLN)` path already exists) |
| `io.c` absolute-path + no-mkfifo gates | KEEP | capability model unchanged |
| `event/event_config.h` backend select | SPLIT+THREADS | threads mode needs an event strategy (below); cooperative backend is single-thread only |
| `shims/lock.h` tid/lock encoding arm | SPLIT | threads mode: derive `dispatch_tid` from `pthread_self()`; musl pthread pointers are >=4-aligned so the `tid<<2`-style low-bit space still works |
| `shims/lock.h` sema4 counter arm | SPLIT | threads mode: `USE_POSIX_SEM=1` (sem_t proven). Later option: real futex via `__builtin_wasm_memory_atomic_wait32/notify` for WAIT_ON_ADDRESS |
| `shims/lock.c` three wait/park arms | SPLIT | threads mode blocks for real; no pumping |
| `queue.c` poke -> eager-drain sites (~32 gates) | SPLIT | threads mode pokes wake workers, upstream shape |
| `queue.c` `DISPATCH_USE_PTHREAD_POOL` excludes | THREADS | re-enable the pthread root-queue pool, Linux shape |
| `queue.c` main-queue drain `#if DISPATCH_COCOA_COMPAT \|\| __wasi__` | KEEP | slice A hoist serves both modes; in threads mode `dispatch_main` can also park normally |
| `init.c` gates (4) | SPLIT | mix: priority/workqueue init returns |
| CMake `CMAKE_HAVE_LIBC_PTHREAD`, `HAVE_*` cache arm | SPLIT | threads mode: internal workqueue on, `USE_POSIX_SEM=1`, pthread pool on |

Slice A files (`event_internal.h` hook macros, `queue.c` brackets,
`object.c`, `once.c`): NO CHANGE. In threads mode the hooks stay
`((void)0)` like every other threaded platform. The seams and API
signatures survive as designed.

## Threads-mode design sketch

- Worker model: `DISPATCH_USE_INTERNAL_WORKQUEUE=1` plus the pthread
  root-queue pool, the same shape Linux uses. `pthread_create` is
  proven under wasmtime v24.
- Locks: keep the dq_state encoding; `dispatch_tid` from
  `pthread_self()`.
- Semaphores: POSIX sem_t (proven, incl. timed waits).
- Timers: a manager thread on `pthread_cond_timedwait` needs no fds.
- fd sources: the hard problem. `poll_oneoff` has no cross-thread
  wakeup object in wasip1-threads (no self-pipe, no eventfd; atomics
  cannot interrupt a poll). A poller thread must use bounded poll
  slices (the WasmKit guard already established the pattern), trading
  arm/cancel latency for correctness.
- `dispatch_main()`: park on a semaphore like Linux; the slice A
  drain hoist still serves the main-queue drain path.

## Consequences for the slice plan

1. #5 (slice A) is unblocked: threads mode needs zero changes to the
   seams or their signatures. Proceed with the upstream copy.
2. Threads mode fits as an additive experimental slice after B1
   (build knob + gate splits + worker pool), with fd sources deferred
   until a runtime story firms up.
3. The test runner needs a wasmtime v24 pin (or WAMR) for any threads
   CI lane; current wasmtime cannot run it.

## Reproduction commands

    # smoke (4 threads, atomics + semaphore): PASS counter=10 threads=4
    wasi-sdk-33.0/bin/clang --target=wasm32-unknown-wasip1-threads \
        -pthread -O1 threads-smoke.c -o threads-smoke.wasm \
        -Wl,--import-memory,--export-memory,--max-memory=67108864
    wasmtime-v24.0.5/wasmtime run -S threads threads-smoke.wasm

    # same source through the Swift host clang: PASS
    swift-6.3.3-RELEASE.xctoolchain/usr/bin/clang \
        --target=wasm32-unknown-wasip1-threads --sysroot=<bundle WASI.sdk> \
        -pthread -O1 -nodefaultlibs threads-smoke.c -o out.wasm -lc \
        wasi-sdk-33.0/lib/clang/22/lib/wasm32-unknown-wasip1-threads/libclang_rt.builtins.a \
        -Wl,--import-memory,--export-memory,--max-memory=67108864

    # timed waits: sem_timedwait 150ms -> 160ms, cond_timedwait 100ms -> 100ms
    # (same compile line, timed-waits.c)
