// audio_effect_qengine.hpp – Godot AudioEffectCapture subclass that feeds
// captured mono bus audio into a BandDetector.
//
// Architecture v2: the effect is a real-time analysis engine, not a gameplay
// judge.  It exposes three tiers of data to GDScript:
//
//   1. get_latest_detection()  – snapshot of the best detected pitch this frame.
//   2. pop_note_events()       – onset-triggered event queue (ring buffer).
//   3. get_frame_history(n)    – last n analysis frames for sustain/bend tracking.
//
// GDScript / gameplay code consumes these outputs and performs chart-aware
// hit/miss judgment.  The effect itself does not score or match notes.
//
// Legacy API: poll_notes() is retained for the per-string tuner UI display.
//
// Tuning selection and note-name mapping are handled by GDScript via the
// band_ranges property (12 floats: freq_min0, freq_max0, …, freq_min5, freq_max5).
// band_ranges MUST be set from GDScript before detection works.
// The audio input is treated as mono — channel x is used directly.

#pragma once

#include <array>
#include <memory>
#include <godot_cpp/classes/audio_effect_capture.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "band_detector.hpp"

namespace godot {

class AudioEffectQEngine : public AudioEffectCapture {
    GDCLASS(AudioEffectQEngine, AudioEffectCapture)

public:
    static void _bind_methods();

    // ── GDScript API – legacy per-string tuner ────────────────────────────

    /// Drain the capture buffer, run pitch detection on the downmixed mono
    /// signal, and return an Array of Dictionaries:
    ///   { "band": int, "frequency": float, "periodicity": float,
    ///     "midi_note": int, "cents": float }
    /// Index 6 is a chord summary row.
    Array poll_notes();

    /// Reset the Q detectors and clear the capture buffer.
    void reset();

    // ── GDScript API – new analysis-state tier ────────────────────────────

    /// Latest analysis snapshot from the most recent poll_notes() call.
    /// Dictionary keys: time_sec, pitch_hz, midi_note, midi_float,
    ///                  confidence, level, onset, pitch_valid.
    /// Intended for: tuner UI, per-frame chart judgment.
    Dictionary get_latest_detection() const;

    /// Pop all pending NoteEvents from the onset queue.
    /// Each event Dictionary has: time_sec, pitch_hz, midi_note,
    ///                            confidence, level.
    /// Intended for: attack detection in chart-guided gameplay.
    Array pop_note_events();

    /// Pop all pending ChordFrames from the per-string chord queue.
    /// Each frame Dictionary has: time_sec, level, dominant_band,
    ///   dominant_midi, dominant_pitch_hz, dominant_confidence,
    ///   active_count, strings (Array of 6 per-string Dicts).
    /// Each per-string Dict: band, pitch_hz, midi_float, midi_note,
    ///   confidence, cents, active.
    /// Intended for: Rocksmith-style per-string chord detection.
    Array pop_chord_frames();

    /// Return up to count recent DetectionFrames (newest first).
    /// Same keys as get_latest_detection() minus onset.
    /// Intended for: sustain checking, bend, vibrato analysis.
    Array get_frame_history(int count) const;

    // ── Exported properties ───────────────────────────────────────────────

    void   set_sample_rate(double v)                       { sample_rate = v; }
    double get_sample_rate()                       const   { return sample_rate; }

    void   set_threshold_db(double v)                      { threshold_db = v; }
    double get_threshold_db()                      const   { return threshold_db; }

    void   set_min_periodicity(double v)                   { min_periodicity = v; }
    double get_min_periodicity()                   const   { return min_periodicity; }

    /// Per-band frequency bounds: 12 floats – [min0, max0, min1, max1, …, min5, max5].
    /// Index 0 is the lowest string.  Set this from GDScript when the tuning changes.
    void                  set_band_ranges(const PackedFloat32Array& v) { band_ranges = v; }
    PackedFloat32Array    get_band_ranges()                    const   { return band_ranges; }

private:
    double             sample_rate     = 44100.0;
    double             threshold_db    = -45.0;
    double             min_periodicity = 0.85;
    PackedFloat32Array band_ranges;

    std::unique_ptr<BandDetector> detector;

    // Track last-built configuration to detect when rebuild is needed.
    double             cfg_sample_rate     = 0.0;
    double             cfg_threshold_db    = 0.0;
    double             cfg_min_periodicity = 0.0;
    PackedFloat32Array cfg_band_ranges;

    void ensure_detector();
    std::array<BandRange, 6> current_ranges() const;
};

} // namespace godot
