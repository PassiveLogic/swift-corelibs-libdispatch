# Building the Swift `Dispatch` overlay for wasm32-wasip1

The Swift overlay (`src/swift/*.swift`) compiles to a `Dispatch.swiftmodule` +
`libswiftDispatch.a` for WASI, so downstream packages can `import Dispatch`.
Verified end to end: `dispatch_wasi_swift.swift` builds, links, and runs under
wasmtime with imports limited to `wasi_snapshot_preview1` (no JavaScript).

## Toolchain notes (host = macOS)
- Use the **swift.org** toolchain that matches the Swift Wasm SDK
  (e.g. `swift-6.3.1-RELEASE`), **not** Xcode's Swift — Xcode's swift-frontend
  aborts in clang PCH generation against this SDK.
- The normal build path is `swift build --swift-sdk <wasm-sdk>` (it wires the
  sysroot, swift/clang resource dirs, and `clang_rt` automatically). The manual
  `swiftc` recipe below is for building the overlay out-of-tree; it needs:
  - `-Xcc -D_WASI_EMULATED_SIGNAL -Xcc -D_WASI_EMULATED_MMAN
     -Xcc -D_WASI_EMULATED_GETPID -Xcc -D_WASI_EMULATED_PROCESS_CLOCKS`
  - the C module map at `dispatch/module.modulemap` (the CMake build supplies
    this via a vfsoverlay onto `dispatch/generic/module.modulemap`; out-of-tree,
    copy that file to `dispatch/module.modulemap` so the umbrella header
    resolves, then remove it afterwards).
  - the wasm `libclang_rt.builtins` must be findable by the linker (the SDK ships
    it as `.../clang/lib/wasip1/libclang_rt.builtins-wasm32.a`).

## Build the overlay (out-of-tree example)
```sh
SWIFTC=<swift.org-toolchain>/usr/bin/swiftc
BUN=<wasm-sdk>/wasm32-unknown-wasip1
cp dispatch/generic/module.modulemap dispatch/module.modulemap
"$SWIFTC" -emit-module -emit-library -static -parse-as-library -module-name Dispatch \
  -target wasm32-unknown-wasip1 -sdk "$BUN/WASI.sdk" \
  -resource-dir "$BUN/swift.xctoolchain/usr/lib/swift_static" \
  -Xcc -fblocks \
  -Xcc -D_WASI_EMULATED_SIGNAL -Xcc -D_WASI_EMULATED_MMAN \
  -Xcc -D_WASI_EMULATED_GETPID -Xcc -D_WASI_EMULATED_PROCESS_CLOCKS \
  -Xcc -fmodule-map-file=dispatch/module.modulemap \
  -Xcc -I. -Xcc -Isrc/swift/shims \
  -emit-module-path out/Dispatch.swiftmodule -o out/libswiftDispatch.a \
  src/swift/*.swift
rm dispatch/module.modulemap
```

## Build + run the consumer
```sh
"$SWIFTC" -target wasm32-unknown-wasip1 -sdk "$BUN/WASI.sdk" \
  -resource-dir "$BUN/swift.xctoolchain/usr/lib/swift_static" -I out \
  <same -Xcc flags as above> \
  dispatch_wasi_swift.swift \
  -L out -lswiftDispatch -L <build>/src -ldispatch \
  -L <build>/src/BlocksRuntime -lBlocksRuntime \
  -lwasi-emulated-signal -lwasi-emulated-mman -lwasi-emulated-getpid \
  -lwasi-emulated-process-clocks -o dtest.wasm
wasmtime run dtest.wasm     # -> grouped async / group.notify / asyncAfter
```

When this port is upstreamed, the Swift Wasm SDK would build the overlay with
`-DENABLE_SWIFT=ON` and install `Dispatch.swiftmodule` + the libs into the SDK,
so consumers just `import Dispatch` with no extra flags.
