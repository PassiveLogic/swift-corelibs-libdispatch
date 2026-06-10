# `swift build --swift-sdk` consumer demo (`import Dispatch`)

A vanilla SwiftPM executable that `import Dispatch` and cross-compiles to wasm
via `swift build --swift-sdk`, the real downstream-consumer flow.

```sh
export SWIFTC=<swift.org-toolchain>/usr/bin/swiftc          # matches the Wasm SDK
export WASM_SDK_BUNDLE=<...>.artifactbundle/.../wasm32-unknown-wasip1
export DISPATCH_BUILD=<your wasm32-wasip1 libdispatch build dir>
./build-prefix.sh                                           # builds overlay + ./dispatch-prefix

export DISPATCH_PREFIX="$PWD/dispatch-prefix"
<swift.org-toolchain>/usr/bin/swift build --swift-sdk <wasm-sdk-name>
wasmtime run .build/wasm32-unknown-wasip1/debug/DispatchDemo.wasm
# -> task 1/2/3, "all tasks done" (group.notify on .main),
#    "timer fired -> exit" (DispatchQueue.main.asyncAfter). Imports: wasi only.
```

The `swiftSettings`/`linkerSettings` flags in `Package.swift` only exist because
the overlay is consumed out-of-tree. Once this port is upstreamed into the Swift
Wasm SDK, a consumer just writes `import Dispatch` with no extra flags.
