// audio_effect_qengine.cpp – AudioEffectQEngine implementation.
//
// The C++ layer outputs raw Q pitch-detector results (frequency + periodicity).
// Note-name mapping and tuning selection live in GDScript.

#include "audio_effect_qengine.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

// ─────────────────────────────────────────────────────────────────────────────
// E Standard default band ranges (half-semitone below open, 2 octaves above)
// Used when GDScript has not yet set band_ranges.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr BandRange STANDARD_RANGES[6] = {
    { 80.11f,  329.64f },  // string 6: E2  (82.41 Hz)
    { 106.87f, 440.00f },  // string 5: A2  (110.00 Hz)
    { 142.65f, 587.32f },  // string 4: D3  (146.83 Hz)
    { 190.42f, 784.00f },  // string 3: G3  (196.00 Hz)
    { 239.91f, 987.76f },  // string 2: B3  (246.94 Hz)
    { 320.25f, 1318.52f }, // string 1: E4  (329.63 Hz)
};

// ─────────────────────────────────────────────────────────────────────────────
// _bind_methods
// ─────────────────────────────────────────────────────────────────────────────

void AudioEffectQEngine::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("poll_notes"), &AudioEffectQEngine::poll_notes);
    ClassDB::bind_method(D_METHOD("reset"),      &AudioEffectQEngine::reset);

    ClassDB::bind_method(D_METHOD("set_sample_rate",      "v"), &AudioEffectQEngine::set_sample_rate);
    ClassDB::bind_method(D_METHOD("get_sample_rate"),          &AudioEffectQEngine::get_sample_rate);
    ClassDB::bind_method(D_METHOD("set_threshold_db",     "v"), &AudioEffectQEngine::set_threshold_db);
    ClassDB::bind_method(D_METHOD("get_threshold_db"),         &AudioEffectQEngine::get_threshold_db);
    ClassDB::bind_method(D_METHOD("set_min_periodicity",  "v"), &AudioEffectQEngine::set_min_periodicity);
    ClassDB::bind_method(D_METHOD("get_min_periodicity"),      &AudioEffectQEngine::get_min_periodicity);
    ClassDB::bind_method(D_METHOD("set_band_ranges",      "v"), &AudioEffectQEngine::set_band_ranges);
    ClassDB::bind_method(D_METHOD("get_band_ranges"),          &AudioEffectQEngine::get_band_ranges);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,               "sample_rate"),     "set_sample_rate",     "get_sample_rate");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,               "threshold_db"),    "set_threshold_db",    "get_threshold_db");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,               "min_periodicity"), "set_min_periodicity", "get_min_periodicity");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "band_ranges"),    "set_band_ranges",     "get_band_ranges");
}

// ─────────────────────────────────────────────────────────────────────────────
// poll_notes
// ─────────────────────────────────────────────────────────────────────────────

Array AudioEffectQEngine::poll_notes()
{
    ensure_detector();

    // Drain the Godot capture buffer, downmix stereo → mono in one batch,
    // and push the whole block into the SPSC ring at once.
    const int64_t available = get_frames_available();
    if (available > 0) {
        const PackedVector2Array frames = get_buffer(available);
        const int64_t            n      = frames.size();
        const Vector2*           data   = frames.ptr();

        std::vector<float> mono(static_cast<std::size_t>(n));
        for (int64_t i = 0; i < n; ++i)
            mono[i] = (data[i].x + data[i].y) * 0.5f;

        detector->push_samples(mono.data(), mono.size());
    }

    // Drain ring → detectors, then return one Dictionary per band/string.
    // All 6 bands are always present; GDScript checks midi_note != -1 (or
    // raw_freq > 0) to know whether a note was detected on that string.
    const auto results = detector->process();

    Array out;
    out.resize(6);
    for (const auto& r : results) {
        Dictionary d;
        d["band"]        = r.band;
        d["frequency"]   = static_cast<double>(r.raw_freq);
        d["periodicity"] = static_cast<double>(r.periodicity);
        d["midi_note"]   = r.midi_note;
        d["cents"]       = static_cast<double>(r.cents);
        out[r.band] = d;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// reset
// ─────────────────────────────────────────────────────────────────────────────

void AudioEffectQEngine::reset()
{
    if (detector) {
        detector->reset();
    }
    clear_buffer();
}

// ─────────────────────────────────────────────────────────────────────────────
// current_ranges (private) – build array from band_ranges or Standard defaults
// ─────────────────────────────────────────────────────────────────────────────

std::array<BandRange, 6> AudioEffectQEngine::current_ranges() const
{
    std::array<BandRange, 6> ranges;
    if (band_ranges.size() >= 12) {
        for (int i = 0; i < 6; ++i) {
            ranges[i] = { band_ranges[i * 2], band_ranges[i * 2 + 1] };
        }
    } else {
        for (int i = 0; i < 6; ++i) {
            ranges[i] = STANDARD_RANGES[i];
        }
    }
    return ranges;
}

// ─────────────────────────────────────────────────────────────────────────────
// ensure_detector (private)
// ─────────────────────────────────────────────────────────────────────────────

void AudioEffectQEngine::ensure_detector()
{
    bool needs_rebuild = !detector
        || cfg_sample_rate     != sample_rate
        || cfg_threshold_db    != threshold_db
        || cfg_min_periodicity != min_periodicity
        || cfg_band_ranges     != band_ranges;

    if (!needs_rebuild) {
        return;
    }

    detector = std::make_unique<BandDetector>(
        static_cast<float>(sample_rate),
        current_ranges(),
        static_cast<float>(threshold_db)
    );
    detector->set_min_periodicity(static_cast<float>(min_periodicity));

    cfg_sample_rate     = sample_rate;
    cfg_threshold_db    = threshold_db;
    cfg_min_periodicity = min_periodicity;
    cfg_band_ranges     = band_ranges;
    clear_buffer();
}

} // namespace godot
