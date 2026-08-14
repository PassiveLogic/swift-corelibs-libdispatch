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
set(SWIFT_WASI_STATIC_RESOURCES_OVERRIDE "" CACHE PATH
  "Optional override for the Swift static resource directory")
set(DISPATCH_WASI_BUILTINS_OVERRIDE "" CACHE FILEPATH
  "Optional override for the WASI compiler-rt builtins archive")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  SWIFT_WASI_TOOLCHAIN_PATH
  SWIFT_WASI_SDK_PATH
  SWIFT_WASI_STATIC_RESOURCES_OVERRIDE
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
set(_dispatch_wasi_swiftc "${SWIFT_WASI_TOOLCHAIN_PATH}/usr/bin/swiftc")
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

if(SWIFT_WASI_STATIC_RESOURCES_OVERRIDE)
  set(SWIFT_WASI_STATIC_RESOURCES "${SWIFT_WASI_STATIC_RESOURCES_OVERRIDE}")
else()
  set(SWIFT_WASI_STATIC_RESOURCES
    "${SWIFT_WASI_SDK_PATH}/swift.xctoolchain/usr/lib/swift_static")
endif()
if(DISPATCH_WASI_BUILTINS_OVERRIDE)
  set(DISPATCH_WASI_BUILTINS "${DISPATCH_WASI_BUILTINS_OVERRIDE}")
else()
  set(DISPATCH_WASI_BUILTINS
    "${SWIFT_WASI_SDK_PATH}/swift.xctoolchain/usr/lib/clang/lib/wasip1/libclang_rt.builtins-wasm32.a")
endif()
set(SWIFT_WASI_CLANG_RESOURCES
  "${SWIFT_WASI_SDK_PATH}/swift.xctoolchain/usr/lib/clang")
if(NOT IS_DIRECTORY "${SWIFT_WASI_STATIC_RESOURCES}")
  message(FATAL_ERROR
    "Swift static resource directory does not exist: ${SWIFT_WASI_STATIC_RESOURCES}")
endif()
if(NOT EXISTS "${DISPATCH_WASI_BUILTINS}")
  message(FATAL_ERROR "WASI builtins archive does not exist: ${DISPATCH_WASI_BUILTINS}")
endif()
if(NOT IS_DIRECTORY "${SWIFT_WASI_CLANG_RESOURCES}")
  message(FATAL_ERROR
    "WASI Clang resource directory does not exist: ${SWIFT_WASI_CLANG_RESOURCES}")
endif()

set(CMAKE_C_COMPILER "${_dispatch_wasi_clang}")
set(CMAKE_CXX_COMPILER "${_dispatch_wasi_clangxx}")
set(CMAKE_AR "${_dispatch_wasi_ar}")
set(CMAKE_RANLIB "${_dispatch_wasi_ranlib}")
set(CMAKE_C_COMPILER_TARGET wasm32-unknown-wasip1)
set(CMAKE_CXX_COMPILER_TARGET wasm32-unknown-wasip1)
set(CMAKE_SYSROOT "${_dispatch_wasi_sysroot}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXECUTABLE_SUFFIX .wasm)

if(ENABLE_SWIFT)
  if(NOT EXISTS "${_dispatch_wasi_swiftc}")
    message(FATAL_ERROR "Swift compiler does not exist: ${_dispatch_wasi_swiftc}")
  endif()
  set(CMAKE_Swift_COMPILER "${_dispatch_wasi_swiftc}")
  set(CMAKE_Swift_COMPILER_TARGET wasm32-unknown-wasip1)
  set(CMAKE_Swift_FLAGS
    "-sdk \"${CMAKE_SYSROOT}\" -resource-dir \"${SWIFT_WASI_STATIC_RESOURCES}\"")
  set(dispatch_MODULE_TRIPLE wasm32-unknown-wasip1 CACHE STRING "Swift module triple")
  set(dispatch_ARCH wasm32 CACHE STRING "Swift architecture")
  set(dispatch_PLATFORM wasi CACHE STRING "Swift platform")
endif()
