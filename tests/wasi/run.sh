#!/usr/bin/env bash
# Build & run the WASI dispatch tests under wasmtime.
#
# Prereqs:
#   - wasi-sdk (>= 33) with the wasm32-wasip1 sysroot
#   - wasmtime
#   - libdispatch already built for wasm32-wasip1 (static), e.g.:
#       cmake -G Ninja -S <repo> -B build-wasi \
#         -DCMAKE_TOOLCHAIN_FILE=$WASI_SDK/share/cmake/wasi-sdk-p1.cmake \
#         -DWASI_SDK_PREFIX=$WASI_SDK -DBUILD_SHARED_LIBS=OFF -DENABLE_SWIFT=OFF
#       cmake --build build-wasi --target dispatch BlocksRuntime
#
# Override WASI_SDK / DISPATCH_SRC / DISPATCH_BUILD as needed.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WASI_SDK="${WASI_SDK:-/opt/wasi-sdk}"
DISPATCH_SRC="${DISPATCH_SRC:-$HERE/../..}"
DISPATCH_BUILD="${DISPATCH_BUILD:?set DISPATCH_BUILD to your wasm32-wasip1 build dir}"

LIBD="$DISPATCH_BUILD/src/libdispatch.a"
BRT="$(find "$DISPATCH_BUILD" -name libBlocksRuntime.a | head -1)"
EMUL="-lwasi-emulated-signal -lwasi-emulated-mman -lwasi-emulated-getpid -lwasi-emulated-process-clocks"

rc=0
for t in dispatch_wasi_smoke dispatch_wasi_mainqueue; do
  echo "=== $t ==="
  "$WASI_SDK/bin/clang" --target=wasm32-wasip1 -O2 \
    -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_PROCESS_CLOCKS \
    -I"$DISPATCH_SRC" -I"$DISPATCH_SRC/src/BlocksRuntime" \
    "$HERE/$t.c" "$LIBD" "$BRT" $EMUL -o "$HERE/$t.wasm"
  wasmtime run "$HERE/$t.wasm" < /dev/null || rc=$?
  echo "($t exit=$rc)"
done
exit $rc
