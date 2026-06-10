#!/usr/bin/env bash
# Build the Swift Dispatch overlay for wasm32-wasip1 and assemble a self-contained
# "SDK-overlay prefix" (headers + module maps + Dispatch.swiftmodule + libs) that the
# SwiftPM demo here consumes via `swift build --swift-sdk`. Required env:
#   SWIFTC            swift.org toolchain swiftc matching the Wasm SDK
#   WASM_SDK_BUNDLE   <swift-sdk.artifactbundle>/.../wasm32-unknown-wasip1
#   DISPATCH_BUILD    a wasm32-wasip1 libdispatch build dir (has src/libdispatch.a)
# Optional: PREFIX (default: ./dispatch-prefix next to this script)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; REPO="$HERE/../../.."
: "${SWIFTC:?}"; : "${WASM_SDK_BUNDLE:?}"; : "${DISPATCH_BUILD:?}"
PREFIX="${PREFIX:-$HERE/dispatch-prefix}"
SDKROOT="$WASM_SDK_BUNDLE/WASI.sdk"; RES="$WASM_SDK_BUNDLE/swift.xctoolchain/usr/lib/swift_static"
BRT="$(find "$DISPATCH_BUILD" -name libBlocksRuntime.a | head -1)"
EMU=(-Xcc -D_WASI_EMULATED_SIGNAL -Xcc -D_WASI_EMULATED_MMAN -Xcc -D_WASI_EMULATED_GETPID -Xcc -D_WASI_EMULATED_PROCESS_CLOCKS)
rm -rf "$PREFIX"; mkdir -p "$PREFIX/include/dispatch" "$PREFIX/include/os" "$PREFIX/include/shims" "$PREFIX/lib"
cp "$REPO/dispatch/generic/module.modulemap" "$REPO/dispatch/module.modulemap"  # replicate the CMake vfsoverlay
trap 'rm -f "$REPO/dispatch/module.modulemap"' EXIT
"$SWIFTC" -emit-module -emit-library -static -parse-as-library -module-name Dispatch \
  -module-link-name swiftDispatch \
  -target wasm32-unknown-wasip1 -sdk "$SDKROOT" -resource-dir "$RES" \
  -Xcc -fblocks "${EMU[@]}" \
  -Xcc -fmodule-map-file="$REPO/dispatch/module.modulemap" -Xcc -I"$REPO" -Xcc -I"$REPO/src/swift/shims" \
  -emit-module-path "$PREFIX/lib/Dispatch.swiftmodule" -o "$PREFIX/lib/libswiftDispatch.a" \
  "$REPO"/src/swift/*.swift
cp "$REPO"/dispatch/*.h "$PREFIX/include/dispatch/"
cp "$REPO"/os/*.h "$PREFIX/include/os/"
cp "$REPO/dispatch/generic/module.modulemap" "$PREFIX/include/dispatch/module.modulemap"
cp "$REPO"/src/swift/shims/DispatchOverlayShims.h "$REPO"/src/swift/shims/module.modulemap "$PREFIX/include/shims/"
cp "$DISPATCH_BUILD/src/libdispatch.a" "$BRT" "$PREFIX/lib/"
echo "assembled $PREFIX"
