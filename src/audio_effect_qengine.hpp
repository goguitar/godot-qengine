// audio_effect_qengine.hpp – Godot AudioEffectCapture subclass that feeds
// captured stereo bus audio into a BandDetector.

#pragma once

#include <memory>
#include <godot_cpp/classes/audio_effect_capture.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "band_detector.hpp"

namespace godot {

class AudioEffectQEngine : public AudioEffectCapture {
    GDCLASS(AudioEffectQEngine, AudioEffectCapture)

public:
    static void _bind_methods();

    // ── GDScript API ──────────────────────────────────────────────────────

    /// Drain the capture buffer, run pitch detection on the downmixed mono
    /// signal, and return an Array of Dictionaries with band results.
    Array poll_notes();

    /// Reset the Q detectors and clear the capture buffer.
    void reset();

    // ── Exported properties ───────────────────────────────────────────────

    void   set_sample_rate(double v)         { sample_rate = v; }
    double get_sample_rate()         const   { return sample_rate; }

    void   set_threshold_db(double v)        { threshold_db = v; }
    double get_threshold_db()        const   { return threshold_db; }

    void   set_tuning(const String& v)       { tuning = v; }
    String get_tuning()              const   { return tuning; }

    void   set_min_periodicity(double v)     { min_periodicity = v; }
    double get_min_periodicity()     const   { return min_periodicity; }

private:
    double sample_rate     = 44100.0;
    double threshold_db    = -45.0;
    String tuning          = "Standard";
    double min_periodicity = 0.8;

    std::unique_ptr<BandDetector> detector;

    // Track last-built configuration to detect when rebuild is needed.
    double cfg_sample_rate     = 0.0;
    double cfg_threshold_db    = 0.0;
    String cfg_tuning;
    double cfg_min_periodicity = 0.0;

    void ensure_detector();
};

} // namespace godot
