cmake_minimum_required(VERSION 3.22)
project(godot-qengine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# cycfi/infra + cycfi/Q (header-only)
# ─────────────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    infra
    GIT_REPOSITORY https://github.com/cycfi/infra.git
    GIT_TAG        2dff97a4b107eced78e426152f5001a2331cb1cf
    GIT_SUBMODULES ""
    GIT_PROGRESS   TRUE
)
FetchContent_GetProperties(infra)
if(NOT infra_POPULATED)
    FetchContent_Populate(infra)
endif()

FetchContent_Declare(
    q
    GIT_REPOSITORY https://github.com/cycfi/q.git
    GIT_TAG        ebb4f2231a83c63e741b68630ce73d4fd42d08dd
    GIT_SUBMODULES ""
    GIT_PROGRESS   TRUE
)
FetchContent_GetProperties(q)
if(NOT q_POPULATED)
    FetchContent_Populate(q)
endif()

set(Q_INCLUDE_DIR     "${q_SOURCE_DIR}/q_lib/include")
set(INFRA_INCLUDE_DIR "${infra_SOURCE_DIR}/include")

# ─────────────────────────────────────────────────────────────────────────────
# qengine_core  –  pure C++ detector logic; no Godot dependency
# ─────────────────────────────────────────────────────────────────────────────
add_library(qengine_core STATIC
    src/band_detector.cpp
)

target_include_directories(qengine_core
    PUBLIC  "${CMAKE_SOURCE_DIR}/src"
    SYSTEM PUBLIC "${Q_INCLUDE_DIR}"
    SYSTEM PUBLIC "${INFRA_INCLUDE_DIR}"
)

target_compile_options(qengine_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)

# ─────────────────────────────────────────────────────────────────────────────
# qengine_ffi  –  C ABI bridge over the async C++ detector backend
# ─────────────────────────────────────────────────────────────────────────────
add_library(qengine_ffi STATIC
    src/async_band_detector.cpp
    src/qengine_ffi.cpp
)

target_link_libraries(qengine_ffi PRIVATE qengine_core)

target_include_directories(qengine_ffi
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
)

target_compile_options(qengine_ffi PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)

# ─────────────────────────────────────────────────────────────────────────────
# godot_qengine  –  Rust Godot extension (godot-rust) linked to qengine_ffi
# ─────────────────────────────────────────────────────────────────────────────
find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo-target")
set(RUST_MANIFEST   "${CMAKE_SOURCE_DIR}/rust/Cargo.toml")
set(RUST_LIB_NAME   "${CMAKE_SHARED_LIBRARY_PREFIX}godot_qengine${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(RUST_OUTPUT_LIB "${RUST_TARGET_DIR}/release/${RUST_LIB_NAME}")
set(BUILT_GODOT_LIB "${CMAKE_BINARY_DIR}/${RUST_LIB_NAME}")

add_custom_command(
    OUTPUT "${RUST_OUTPUT_LIB}"
    COMMAND ${CMAKE_COMMAND} -E env
        QENGINE_FFI_LIB_DIR=$<TARGET_FILE_DIR:qengine_ffi>
        cargo build --manifest-path "${RUST_MANIFEST}" --release --target-dir "${RUST_TARGET_DIR}"
    DEPENDS qengine_ffi
            "${CMAKE_SOURCE_DIR}/rust/Cargo.toml"
            "${CMAKE_SOURCE_DIR}/rust/build.rs"
            "${CMAKE_SOURCE_DIR}/rust/src/lib.rs"
            "${CMAKE_SOURCE_DIR}/src/qengine_ffi.h"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building Rust Godot extension (godot-rust + qengine_ffi)"
    VERBATIM
)

add_custom_target(godot_qengine ALL DEPENDS "${RUST_OUTPUT_LIB}")

add_custom_command(TARGET godot_qengine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RUST_OUTPUT_LIB}"
        "${BUILT_GODOT_LIB}"
    COMMENT "Copying built godot_qengine library to CMake build root"
)

# Post-build: copy the shared library into the demo addon bin folder.
set(DEMO_ADDON_BIN_DIR "${CMAKE_SOURCE_DIR}/demo/addons/qengine/bin")
add_custom_command(TARGET godot_qengine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEMO_ADDON_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RUST_OUTPUT_LIB}"
        "${DEMO_ADDON_BIN_DIR}/${RUST_LIB_NAME}"
    COMMENT "Copying godot_qengine library to demo/addons/qengine/bin/"
)

# ─────────────────────────────────────────────────────────────────────────────
# Tests (no Godot required)
# ─────────────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
