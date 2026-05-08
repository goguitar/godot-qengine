# tests/CMakeLists.txt

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
