if(CMAKE_VERSION VERSION_LESS 3.31)
  message(FATAL_ERROR
    "WASI builds require CMake 3.31 or newer for CMAKE_SYSTEM_NAME=WASI support")
endif()

set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

set(SWIFT_WASI_TOOLCHAIN_PATH "${SWIFT_WASI_TOOLCHAIN_PATH}" CACHE PATH
  "Host Swift .xctoolchain used to build for WASI")
set(SWIFT_WASI_SDK_PATH "${SWIFT_WASI_SDK_PATH}" CACHE PATH
  "wasm32-unknown-wasip1 directory in a Swift WASI SDK")
set(DISPATCH_WASI_BUILTINS_OVERRIDE "" CACHE FILEPATH
  "Optional override for the WASI compiler-rt builtins archive")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  SWIFT_WASI_TOOLCHAIN_PATH
  SWIFT_WASI_SDK_PATH
  DISPATCH_WASI_BUILTINS_OVERRIDE)

if(NOT SWIFT_WASI_TOOLCHAIN_PATH)
  message(FATAL_ERROR "Set SWIFT_WASI_TOOLCHAIN_PATH to the host Swift .xctoolchain")
endif()
if(NOT SWIFT_WASI_SDK_PATH)
  message(FATAL_ERROR "Set SWIFT_WASI_SDK_PATH to the wasm32-unknown-wasip1 SDK directory")
endif()

if(NOT IS_DIRECTORY "${SWIFT_WASI_TOOLCHAIN_PATH}")
  message(FATAL_ERROR "SWIFT_WASI_TOOLCHAIN_PATH is not a directory: ${SWIFT_WASI_TOOLCHAIN_PATH}")
endif()
if(NOT IS_DIRECTORY "${SWIFT_WASI_SDK_PATH}")
  message(FATAL_ERROR "SWIFT_WASI_SDK_PATH is not a directory: ${SWIFT_WASI_SDK_PATH}")
endif()

set(_dispatch_wasi_clang "${SWIFT_WASI_TOOLCHAIN_PATH}/usr/bin/clang")
set(_dispatch_wasi_clangxx "${SWIFT_WASI_TOOLCHAIN_PATH}/usr/bin/clang++")
set(_dispatch_wasi_ar "${SWIFT_WASI_TOOLCHAIN_PATH}/usr/bin/llvm-ar")
set(_dispatch_wasi_ranlib "${SWIFT_WASI_TOOLCHAIN_PATH}/usr/bin/llvm-ranlib")
set(_dispatch_wasi_sysroot "${SWIFT_WASI_SDK_PATH}/WASI.sdk")

foreach(_dispatch_wasi_tool IN ITEMS
    "${_dispatch_wasi_clang}"
    "${_dispatch_wasi_clangxx}"
    "${_dispatch_wasi_ar}"
    "${_dispatch_wasi_ranlib}")
  if(NOT EXISTS "${_dispatch_wasi_tool}")
    message(FATAL_ERROR "Required WASI build tool does not exist: ${_dispatch_wasi_tool}")
  endif()
endforeach()
if(NOT IS_DIRECTORY "${_dispatch_wasi_sysroot}")
  message(FATAL_ERROR "WASI sysroot does not exist: ${_dispatch_wasi_sysroot}")
endif()

if(DISPATCH_WASI_BUILTINS_OVERRIDE)
  set(DISPATCH_WASI_BUILTINS "${DISPATCH_WASI_BUILTINS_OVERRIDE}")
else()
  set(DISPATCH_WASI_BUILTINS
    "${SWIFT_WASI_SDK_PATH}/swift.xctoolchain/usr/lib/clang/lib/wasip1/libclang_rt.builtins-wasm32.a")
endif()
if(NOT EXISTS "${DISPATCH_WASI_BUILTINS}")
  message(FATAL_ERROR "WASI builtins archive does not exist: ${DISPATCH_WASI_BUILTINS}")
endif()
# The host clang looks for its compiler runtime in its own resource dir,
# which has no wasm32 build. So C and C++ executables drop the default
# libraries and link libc and the WASI builtins archive instead. This applies
# to every executable that uses this toolchain, not only the tests. The
# values are forced into the cache, so a reconfigure with another SDK or
# builtins archive takes effect.
set(CMAKE_C_STANDARD_LIBRARIES
  "-nodefaultlibs -lc \"${DISPATCH_WASI_BUILTINS}\""
  CACHE STRING "Libraries linked by default with all C applications" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES
  "-nodefaultlibs -lc++ -lc++abi -lc \"${DISPATCH_WASI_BUILTINS}\""
  CACHE STRING "Libraries linked by default with all C++ applications" FORCE)

set(CMAKE_C_COMPILER "${_dispatch_wasi_clang}")
set(CMAKE_CXX_COMPILER "${_dispatch_wasi_clangxx}")
set(CMAKE_AR "${_dispatch_wasi_ar}")
set(CMAKE_RANLIB "${_dispatch_wasi_ranlib}")
set(CMAKE_C_COMPILER_TARGET wasm32-unknown-wasip1)
set(CMAKE_CXX_COMPILER_TARGET wasm32-unknown-wasip1)
set(CMAKE_SYSROOT "${_dispatch_wasi_sysroot}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXECUTABLE_SUFFIX .wasm)
