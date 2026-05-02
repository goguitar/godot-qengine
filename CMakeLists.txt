cmake_minimum_required(VERSION 3.22)
project(godot-qengine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ─────────────────────────────────────────────────────────────────────────────
# godot-cpp  –  official C++ GDExtension bindings (fetched at configure time)
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG        godot-4.4-stable
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(godot-cpp)

# ─────────────────────────────────────────────────────────────────────────────
# cycfi/Q and cycfi/infra  –  git submodules  (third_party/)
# ─────────────────────────────────────────────────────────────────────────────
set(Q_INCLUDE_DIR     "${CMAKE_SOURCE_DIR}/third_party/q/q_lib/include")
set(INFRA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/infra/include")

if(NOT EXISTS "${Q_INCLUDE_DIR}/q/pitch/pitch_detector.hpp")
    message(FATAL_ERROR
        "Q library headers not found at ${Q_INCLUDE_DIR}.\n"
        "Please run: git submodule update --init --recursive")
endif()

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
# Tests (no Godot required)
# ─────────────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
