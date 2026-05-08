#include "qengine_ffi.h"

#include <array>
#include <memory>
#include <vector>

#include "async_band_detector.hpp"

struct QEngineHandle {
    std::unique_ptr<AsyncBandDetector> detector;
};

namespace {
std::array<BandRange, 6> to_ranges(const QEngineBandRange* ranges, std::size_t count)
{
    std::array<BandRange, 6> out{};
    if (!ranges || count < 6) {
        return out;
    }
    for (int i = 0; i < 6; ++i) {
        out[i] = {ranges[i].freq_min, ranges[i].freq_max};
    }
    return out;
}

QEngineDetectionFrame to_detection_frame(const DetectionFrame& in)
{
    QEngineDetectionFrame out{};
    out.time_sec = in.time_sec;
    out.pitch_hz = in.pitch_hz;
    out.midi_float = in.midi_float;
    out.midi_note = in.midi_note;
    out.confidence = in.confidence;
    out.level = in.level;
    out.onset = in.onset ? 1u : 0u;
    out.pitch_valid = in.pitch_valid ? 1u : 0u;
    return out;
}

QEngineNoteEvent to_note_event(const NoteEvent& in)
{
    QEngineNoteEvent out{};
    out.time_sec = in.time_sec;
    out.pitch_hz = in.pitch_hz;
    out.midi_note = in.midi_note;
    out.confidence = in.confidence;
    out.level = in.level;
    return out;
}

QEngineStringComponent to_string_component(const StringComponent& in)
{
    QEngineStringComponent out{};
    out.band = in.band;
    out.pitch_hz = in.pitch_hz;
    out.midi_float = in.midi_float;
    out.midi_note = in.midi_note;
    out.confidence = in.confidence;
    out.cents = in.cents;
    out.active = in.active ? 1u : 0u;
    return out;
}

QEngineChordFrame to_chord_frame(const ChordFrame& in)
{
    QEngineChordFrame out{};
    out.time_sec = in.time_sec;
    out.level = in.level;
    for (int i = 0; i < 6; ++i) {
        out.strings[i] = to_string_component(in.strings[i]);
    }
    out.dominant_band = in.dominant_band;
    out.dominant_midi = in.dominant_midi;
    out.dominant_pitch_hz = in.dominant_pitch_hz;
    out.dominant_confidence = in.dominant_confidence;
    out.active_count = in.active_count;
    return out;
}
} // namespace

extern "C" {

QEngineHandle* qengine_create(float sample_rate,
                              float threshold_db,
                              float min_periodicity,
                              const QEngineBandRange* ranges,
                              std::size_t range_count)
{
    if (!ranges || range_count < 6) {
        return nullptr;
    }

    auto handle = std::make_unique<QEngineHandle>();
    handle->detector = std::make_unique<AsyncBandDetector>(
        sample_rate,
        to_ranges(ranges, range_count),
        threshold_db,
        min_periodicity
    );
    return handle.release();
}

void qengine_destroy(QEngineHandle* handle)
{
    delete handle;
}

void qengine_reset(QEngineHandle* handle)
{
    if (!handle || !handle->detector) {
        return;
    }
    handle->detector->request_reset();
}

void qengine_set_min_periodicity(QEngineHandle* handle, float v)
{
    if (!handle || !handle->detector) {
        return;
    }
    handle->detector->set_min_periodicity(v);
}

void qengine_push_audio(QEngineHandle* handle, const float* samples, std::size_t count)
{
    if (!handle || !handle->detector || !samples || count == 0) {
        return;
    }
    handle->detector->push_samples(samples, count);
}

void qengine_process(QEngineHandle* handle)
{
    if (!handle || !handle->detector) {
        return;
    }
    // Async backend processes automatically on its worker thread.
}

int qengine_get_latest_detection(QEngineHandle* handle, QEngineDetectionFrame* out_frame)
{
    if (!handle || !handle->detector || !out_frame) {
        return 0;
    }
    *out_frame = to_detection_frame(handle->detector->latest_frame());
    return 1;
}

std::size_t qengine_pop_note_events(QEngineHandle* handle, QEngineNoteEvent* out_events, std::size_t max_events)
{
    if (!handle || !handle->detector || !out_events || max_events == 0) {
        return 0;
    }

    std::size_t out_count = 0;
    NoteEvent ev;
    while (out_count < max_events && handle->detector->pop_event(ev)) {
        out_events[out_count++] = to_note_event(ev);
    }
    return out_count;
}

std::size_t qengine_get_recent_frames(QEngineHandle* handle, QEngineDetectionFrame* out_frames, std::size_t max_frames)
{
    if (!handle || !handle->detector || !out_frames || max_frames == 0) {
        return 0;
    }

    std::vector<DetectionFrame> tmp(max_frames);
    const std::size_t got = handle->detector->get_frame_history(tmp.data(), max_frames);
    for (std::size_t i = 0; i < got; ++i) {
        out_frames[i] = to_detection_frame(tmp[i]);
    }
    return got;
}

std::size_t qengine_pop_chord_frames(QEngineHandle* handle, QEngineChordFrame* out_frames, std::size_t max_frames)
{
    if (!handle || !handle->detector || !out_frames || max_frames == 0) {
        return 0;
    }

    std::size_t out_count = 0;
    ChordFrame chord;
    while (out_count < max_frames && handle->detector->pop_chord_frame(chord)) {
        out_frames[out_count++] = to_chord_frame(chord);
    }
    return out_count;
}

int qengine_get_latest_chord_frame(QEngineHandle* handle, QEngineChordFrame* out_frame)
{
    if (!handle || !handle->detector || !out_frame) {
        return 0;
    }
    *out_frame = to_chord_frame(handle->detector->latest_chord_frame());
    return 1;
}

} // extern "C"
