// audio_effect_qengine.hpp – Godot AudioEffectCapture subclass that feeds
// captured stereo bus audio into a BandDetector.
//
// Tuning selection and note-name mapping are handled by GDScript via the
// band_ranges property (12 floats: freq_min0, freq_max0, …, freq_min5, freq_max5).

#pragma once

#include <array>
#include <memory>
#include <godot_cpp/classes/audio_effect_capture.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "band_detector.hpp"

namespace godot {

class AudioEffectQEngine : public AudioEffectCapture {
    GDCLASS(AudioEffectQEngine, AudioEffectCapture)

public:
    static void _bind_methods();

    // ── GDScript API ──────────────────────────────────────────────────────

    /// Drain the capture buffer, run pitch detection on the downmixed mono
    /// signal, and return an Array of Dictionaries:
    ///   { "band": int, "frequency": float, "periodicity": float }
    /// Note-name and cents computation are left to GDScript.
    Array poll_notes();

    /// Reset the Q detectors and clear the capture buffer.
    void reset();

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
    double             min_periodicity = 0.8;
    PackedFloat32Array band_ranges;  // populated with Standard defaults on first use

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
