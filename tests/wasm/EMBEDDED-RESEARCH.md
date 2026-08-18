# libdispatch with Embedded Swift on wasm32-unknown-wasip1

Research notes for using libdispatch from Embedded Swift on WebAssembly.
Fork-only material. Findings below marked "proven" were reproduced on
this machine; the commands live at the end.

## Summary

- The official swift.org embedded wasm SDK works end to end today:
  an Embedded Swift program that imports Dispatch, dispatches work,
  waits on semaphores and groups, arms a timer source, and walks a
  DispatchData passes under both wasmtime v24 LTS and current
  wasmtime v47. Proven.
- The C library needs ZERO changes. The embedded SDK targets the same
  `wasm32-unknown-wasip1` triple and the same sysroot as the regular
  wasm SDK; the cooperative event backend is used as is. The only
  toolset difference on the C side is a `-D__EMBEDDED_SWIFT__` define,
  which nothing in libdispatch reads. Proven.
- The Swift overlay compiles in Embedded Swift mode after two small,
  fully gated source changes (details below). All nine overlay files
  build; no API is removed and no signature moves. Proven.
- Binary size is the headline win: the same consumer smoke test is
  572 KB embedded vs 7.8 MB with the regular static-stdlib SDK,
  about 13.6x smaller. Proven.
- Unlike threads mode, there is no runtime constraint: the output is a
  plain wasip1 module, so every runtime that runs the cooperative port
  runs the embedded build.

## Toolchain facts (proven)

SDK: `swift-6.3.3-RELEASE_wasm-embedded`, installed from swift.org via
`swift sdk install`. It is NOT a separate download: the one
`swift-6.3.3-RELEASE_wasm.artifactbundle` defines two swiftSDK
artifacts over the same sysroot and triple:

    swift-6.3.3-RELEASE_wasm          -> swift-sdk.json + toolset.json
    swift-6.3.3-RELEASE_wasm-embedded -> embedded-swift-sdk.json
                                         + embedded-toolset.json

The embedded toolset differs only in flags:

- C/C++: `-D__EMBEDDED_SWIFT__`
- Swift: `-static-stdlib -enable-experimental-feature Embedded -wmo
  -Xlinker -lc++ -Xlinker -lswift_Concurrency`
- Swift resources: `usr/lib/swift` (the embedded stdlib modules live
  in `usr/lib/swift/embedded`), not `usr/lib/swift_static`.

Facts that cost time to find:

- The host 6.3.3 toolchain does not ship compiler-rt builtins for
  wasm. Linking through the host swiftc needs
  `-Xclang-linker -resource-dir
  -Xclang-linker <bundle>/swift.xctoolchain/usr/lib/clang`
  or wasm-ld fails to find `libclang_rt.builtins.a`. Same gotcha as
  the threads research, different spelling: the driver consults the
  clang resource dir, so overriding the dir beats naming the archive.
- Embedded Swift emits NO autolink metadata. The `link "dispatch"` and
  `link "BlocksRuntime"` directives in the module maps are ignored, so
  an embedded consumer must name every archive on the link line:
  libswiftDispatch.a, libdispatch.a, libBlocksRuntime.a, and the
  wasi-emulated libraries. The regular SDK autolinks all of these.
- Whole-module mode is mandatory (`error: Whole module optimization
  (wmo) must be enabled with embedded Swift`). Under CMake that is
  `CMAKE_Swift_COMPILATION_MODE=wholemodule`, and the variable must be
  propagated into try_compile projects
  (`CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`) or the compiler check runs
  incrementally and reports the compiler as broken.
- Clang-importer surface differences bite before Embedded does:
  wasi-libc's `CLOCK_MONOTONIC` is a pointer macro to an incomplete
  `struct __clockid`, unusable from Embedded Swift ("structure not
  supported"). The overlay never imports it (it funnels through the C
  shims), so libdispatch is unaffected; consumer code that reaches
  for raw libc clocks will notice.

## What the overlay needed (the whole diff)

Compiling the nine overlay sources with `-enable-experimental-feature
Embedded` produced two error strata, 56 errors total, and nothing
else:

1. 56 errors: every use of the `_DispatchOverlayShims` C module.
   Embedded Swift compiles all public functions into the client, so an
   `@_implementationOnly` import is unusable from them. Fix: import
   the shims module plainly under `#if hasFeature(Embedded)`. Privacy
   is moot there; everything is statically linked into one module.
2. 14 errors: the `DispatchSource.make*Source()` factories return
   protocol existentials (`any DispatchSourceTimer`, ...), and
   Embedded Swift only supports class-bound existentials. Fix:
   constrain `DispatchSourceProtocol` to a conditional typealias that
   is `AnyObject` under Embedded and `Any` (a no-op constraint)
   elsewhere. The only conformer is the `DispatchSource` class, so the
   bound is factual. A `#if` cannot split a declaration head, which is
   why the typealias trick is used.

After those two changes: zero errors, zero new warnings (the 36
remaining warnings are the pre-existing `@_implementationOnly`
deprecation notes that the regular build also emits). No file is
excluded; Data.swift, IO.swift, and Source.swift all compile.

The non-embedded arms are unchanged: the cooperative WASI suite still
passes 52 of 52 with these edits, including the Swift consumer test.
The one cross-platform-visible addition is the public typealias
`_DispatchSourceProtocolConstraint`; an upstream reviewer may prefer
an underscored protocol or a duplicated declaration instead.

## Build system

`cmake ... -DDISPATCH_WASI_EMBEDDED=ON -DENABLE_SWIFT=YES` builds
libdispatch.a, libBlocksRuntime.a, and libswiftDispatch.a in embedded
mode. The knob:

- appends `-enable-experimental-feature Embedded` to the Swift flags,
- selects `usr/lib/swift` as the Swift resource dir,
- forces `CMAKE_Swift_COMPILATION_MODE=wholemodule` (and propagates it
  through try_compile),
- adds `-D__EMBEDDED_SWIFT__` to the C/C++ side, matching the SDK's
  embedded toolset.

The C library produced with the knob is the cooperative port,
unchanged. `BUILD_TESTING` stays OFF for now: the ctest harness links
its Swift tests with the regular flags, and wiring an embedded test
lane is future work.

## Proof

`tests/wasm/embedded-dispatch-smoke.swift`, built against the
embedded overlay and run under wasmtime v24 LTS and wasmtime v47:

    PASS: async=8/8 sync=1 group=1 timer=1 source=1 data=4

Coverage: eager async submission on the cooperative backend, serial
`sync`, `DispatchGroup` wait, a `dispatch_after` timer firing while a
semaphore wait pumps, a timer `DispatchSource` through the class-bound
existential path, and `DispatchData` append/iterate through the shims
funnel.

Size, same source, same machine:

| Build | Bytes |
|---|---|
| Embedded SDK | 572,214 |
| Regular SDK, -static-stdlib | 7,796,878 |

## Not covered yet

- Swift Concurrency (async/await) on top of embedded Dispatch: the
  embedded `_Concurrency` runtime links (the toolset demands it), but
  executor integration with the cooperative backend is untested.
- `DispatchIO` and fd/signal `DispatchSource`s at runtime from
  embedded consumers (they compile; the C paths are proven by the C
  suite).
- An embedded lane in the ctest harness.
- Swift Embedded for non-WASI targets (bare-metal wasm32-unknown-none
  has no wasi-libc, so it is out of scope for this port).

## Consequences for the slice plan

1. Slice A (#5) is untouched: the embedded work changes no C code at
   all, so the seams and signatures are unaffected.
2. The two overlay edits are candidates for slice C (the Swift
   overlay slice), as a small "Embedded Swift compatibility" commit.
   They are safe on every platform and need no WASI-specific gates.
3. The `DISPATCH_WASI_EMBEDDED` knob can ride with slice C or stay
   fork-only until upstream asks for it.
4. Embedded mode and threads mode are orthogonal experiments: embedded
   rides the cooperative backend on the plain wasip1 triple; a
   combined embedded+threads build was not attempted.

## Reproduction commands

    # configure + build (library + overlay) in embedded mode
    cmake -S . -B build-wasi-embedded -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/WASI.cmake \
        -DSWIFT_WASI_TOOLCHAIN_PATH=$HOME/Library/Developer/Toolchains/swift-6.3.3-RELEASE.xctoolchain \
        -DSWIFT_WASI_SDK_PATH=$HOME/Library/org.swift.swiftpm/swift-sdks/swift-6.3.3-RELEASE_wasm.artifactbundle/swift-6.3.3-RELEASE_wasm/wasm32-unknown-wasip1 \
        -DDISPATCH_WASI_EMBEDDED=ON -DBUILD_TESTING=OFF -DENABLE_SWIFT=YES
    ninja -C build-wasi-embedded

    # smoke test against the built artifacts (host swiftc; note the
    # explicit archives: embedded emits no autolink metadata)
    SDK=$HOME/Library/org.swift.swiftpm/swift-sdks/swift-6.3.3-RELEASE_wasm.artifactbundle/swift-6.3.3-RELEASE_wasm/wasm32-unknown-wasip1
    swiftc -target wasm32-unknown-wasip1 -sdk $SDK/WASI.sdk \
        -resource-dir $SDK/swift.xctoolchain/usr/lib/swift \
        -enable-experimental-feature Embedded -wmo -static-stdlib \
        -Xclang-linker -resource-dir -Xclang-linker $SDK/swift.xctoolchain/usr/lib/clang \
        -I build-wasi-embedded/src/swift/swift \
        -Xcc -fblocks -Xcc -fmodule-map-file=dispatch/module.modulemap \
        -Xcc -I. -Xcc -Isrc/swift/shims \
        -vfsoverlay build-wasi-embedded/dispatch-vfs-overlay.yaml \
        -Xcc -D_WASI_EMULATED_SIGNAL -Xcc -D_WASI_EMULATED_MMAN -Xcc -D_WASI_EMULATED_GETPID \
        tests/wasm/embedded-dispatch-smoke.swift \
        build-wasi-embedded/src/swift/libswiftDispatch.a \
        build-wasi-embedded/src/libdispatch.a \
        build-wasi-embedded/src/BlocksRuntime/libBlocksRuntime.a \
        -Xlinker -lwasi-emulated-signal -Xlinker -lwasi-emulated-mman \
        -Xlinker -lwasi-emulated-getpid \
        -Xlinker -lc++ -Xlinker -lswift_Concurrency \
        -o smoke.wasm
    wasmtime run smoke.wasm
