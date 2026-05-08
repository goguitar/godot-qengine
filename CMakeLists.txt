cmake_minimum_required(VERSION 3.22)
project(godot-qengine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# ─────────────────────────────────────────────────────────────────────────────
# godot-cpp  –  official C++ GDExtension bindings
# Pinned to the exact commit used during development (branch 4.4).
# ─────────────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    godotcpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG        b0e3b1e4b78a606f48d162898afb5eeda533d2a9
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(godotcpp)

# ─────────────────────────────────────────────────────────────────────────────
# cycfi/infra  –  header-only support library required by Q
# Fetched without running its CMakeLists (headers only; no targets needed).
# Its own submodules (optional-lite, string-view-lite) are C++14/17 shims
# that are not needed when building with C++20.
# FetchContent_Populate is used here instead of FetchContent_MakeAvailable so
# that infra/Q CMakeLists.txt files are not processed (they would fail because
# each expects to find the other at a relative path).
# cmake_policy CMP0169=OLD keeps FetchContent_Populate available in CMake 3.30+.
# ─────────────────────────────────────────────────────────────────────────────
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

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

# ─────────────────────────────────────────────────────────────────────────────
# cycfi/Q  –  header-only pitch detection library
# Fetched without running its CMakeLists so that Q does not attempt to locate
# infra through its own cmake path logic (we provide the include path directly).
# ─────────────────────────────────────────────────────────────────────────────
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
# qengine_core  –  pure C++ detection logic; no Godot dependency
# Used by the GDExtension AND by the test executable.
# ─────────────────────────────────────────────────────────────────────────────
add_library(qengine_core STATIC
    src/band_detector.cpp
)

target_include_directories(qengine_core
    PUBLIC  "${CMAKE_SOURCE_DIR}/src"
    # Mark Q/infra as SYSTEM to suppress their noisy template warnings
    SYSTEM PUBLIC "${Q_INCLUDE_DIR}"
    SYSTEM PUBLIC "${INFRA_INCLUDE_DIR}"
)

target_compile_options(qengine_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)

# ─────────────────────────────────────────────────────────────────────────────
# godot_qengine  –  GDExtension shared library
# ─────────────────────────────────────────────────────────────────────────────
add_library(godot_qengine SHARED
    src/audio_effect_qengine.cpp
    src/detector_node.cpp
    src/register_types.cpp
)

target_link_libraries(godot_qengine PRIVATE
    godot-cpp
    qengine_core
)

target_include_directories(godot_qengine PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

# On all platforms Godot expects no "lib" prefix in the .gdextension file
# (we reference "godot_qengine.dll" / "libgodot_qengine.so" directly).
if(WIN32)
    set_target_properties(godot_qengine PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
elseif(APPLE)
    set_target_properties(godot_qengine PROPERTIES
        PREFIX "lib"
        SUFFIX ".dylib"
    )
else()
    set_target_properties(godot_qengine PROPERTIES
        PREFIX "lib"
        SUFFIX ".so"
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Post-build: copy the shared library into the demo addon bin folder so the
# Godot demo project can be opened and run immediately after building.
# ─────────────────────────────────────────────────────────────────────────────
set(DEMO_ADDON_BIN_DIR "${CMAKE_SOURCE_DIR}/demo/addons/qengine/bin")
add_custom_command(TARGET godot_qengine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEMO_ADDON_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:godot_qengine>"
        "${DEMO_ADDON_BIN_DIR}/$<TARGET_FILE_NAME:godot_qengine>"
    COMMENT "Copying godot_qengine library to demo/addons/qengine/bin/"
)

# ─────────────────────────────────────────────────────────────────────────────
# Tests (no Godot required)
# ─────────────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
