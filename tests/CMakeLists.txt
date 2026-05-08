# tests/CMakeLists.txt

# ── Synthetic pitch detection tests (no WAV files required) ─────────────────
add_executable(qengine_tests test_pitch_detection.cpp)

# qengine_core is a PUBLIC-include library, so the Q/infra include paths and
# src/ are propagated here automatically.
target_link_libraries(qengine_tests PRIVATE qengine_core)

# Suppress cycfi/Q template noise in test build
target_compile_options(qengine_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)

add_test(NAME pitch_detection COMMAND qengine_tests)

# ── Real-audio WAV pitch detection tests (GuitarSet dataset) ────────────────
add_executable(qengine_wav_tests test_wav_pitch_detection.cpp)
target_link_libraries(qengine_wav_tests PRIVATE qengine_core)
target_compile_options(qengine_wav_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)
# Bake the dataset directory path into the binary at configure time.
# Tests gracefully skip individual assertions when the WAV file cannot be opened
# (e.g. running on a machine without the full repo checkout).
target_compile_definitions(qengine_wav_tests PRIVATE
    QENGINE_DATASET_DIR="${CMAKE_SOURCE_DIR}/demo/tests/dataset/guitarset/audio/mic"
)

add_test(NAME wav_pitch_detection COMMAND qengine_wav_tests)
