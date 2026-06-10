// swift-tools-version:5.9
import PackageDescription
import Foundation

// Set DISPATCH_PREFIX to the dir produced by build-prefix.sh. Once this port is
// upstreamed into the Swift Wasm SDK, `import Dispatch` needs no flags at all.
let pfx = ProcessInfo.processInfo.environment["DISPATCH_PREFIX"]
        ?? FileManager.default.currentDirectoryPath + "/dispatch-prefix"

let package = Package(
    name: "DispatchDemo",
    targets: [
        .executableTarget(
            name: "DispatchDemo",
            swiftSettings: [.unsafeFlags([
                "-I", "\(pfx)/lib",
                "-Xcc", "-fblocks",
                "-Xcc", "-D_WASI_EMULATED_SIGNAL", "-Xcc", "-D_WASI_EMULATED_MMAN",
                "-Xcc", "-D_WASI_EMULATED_GETPID", "-Xcc", "-D_WASI_EMULATED_PROCESS_CLOCKS",
                "-Xcc", "-fmodule-map-file=\(pfx)/include/dispatch/module.modulemap",
                "-Xcc", "-I\(pfx)/include", "-Xcc", "-I\(pfx)/include/shims",
            ])],
            linkerSettings: [.unsafeFlags([
                "-L", "\(pfx)/lib", "-lswiftDispatch", "-ldispatch", "-lBlocksRuntime",
                "-lwasi-emulated-signal", "-lwasi-emulated-mman",
                "-lwasi-emulated-getpid", "-lwasi-emulated-process-clocks",
            ])]
        )
    ]
)
