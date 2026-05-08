#include <cmath>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

#include "qengine_ffi.h"

namespace {
std::vector<float> synth_sine(float hz, float sample_rate, float seconds)
{
    const std::size_t n = static_cast<std::size_t>(sample_rate * seconds);
    std::vector<float> out;
    out.reserve(n);
    constexpr float pi = 3.14159265358979323846f;
    const float omega = 2.0f * pi * hz / sample_rate;
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(0.3f * std::sin(omega * static_cast<float>(i)));
    }
    return out;
}

void expect(bool cond, const char* msg)
{
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}
} // namespace

int main()
{
    QEngineBandRange ranges[6] = {
        {80.11f, 164.82f},
        {106.87f, 220.00f},
        {142.65f, 293.66f},
        {190.42f, 392.00f},
        {239.91f, 493.88f},
        {320.25f, 659.26f},
    };

    QEngineHandle* handle = qengine_create(44100.0f, -45.0f, 0.85f, ranges, 6);
    expect(handle != nullptr, "qengine_create returned null");

    auto sine = synth_sine(440.0f, 44100.0f, 0.35f);
    qengine_push_audio(handle, sine.data(), sine.size());
    qengine_process(handle);

    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    QEngineDetectionFrame latest{};
    expect(qengine_get_latest_detection(handle, &latest) == 1, "qengine_get_latest_detection failed");
    expect(latest.pitch_hz > 0.0f, "latest pitch should be available");

    QEngineDetectionFrame history[32]{};
    const std::size_t history_count = qengine_get_recent_frames(handle, history, 32);
    expect(history_count > 0, "recent frame history should be non-empty");

    QEngineChordFrame chord_latest{};
    expect(qengine_get_latest_chord_frame(handle, &chord_latest) == 1, "qengine_get_latest_chord_frame failed");

    QEngineChordFrame chord_frames[64]{};
    const std::size_t chord_count = qengine_pop_chord_frames(handle, chord_frames, 64);
    expect(chord_count > 0, "chord frame queue should contain entries");

    bool saw_active = false;
    for (std::size_t i = 0; i < chord_count; ++i) {
        if (chord_frames[i].active_count > 0) {
            saw_active = true;
            break;
        }
    }
    expect(saw_active, "at least one chord frame should have active strings");

    QEngineNoteEvent note_events[64]{};
    const std::size_t event_count = qengine_pop_note_events(handle, note_events, 64);
    expect(event_count > 0, "note event queue should contain onset events");

    qengine_reset(handle);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    qengine_destroy(handle);

    std::cout << "PASS: ffi_bridge\n";
    return 0;
}
