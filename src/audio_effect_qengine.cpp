// audio_effect_qengine.cpp – AudioEffectQEngine implementation.

#include "audio_effect_qengine.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

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
    ClassDB::bind_method(D_METHOD("set_tuning",           "v"), &AudioEffectQEngine::set_tuning);
    ClassDB::bind_method(D_METHOD("get_tuning"),               &AudioEffectQEngine::get_tuning);
    ClassDB::bind_method(D_METHOD("set_min_periodicity",  "v"), &AudioEffectQEngine::set_min_periodicity);
    ClassDB::bind_method(D_METHOD("get_min_periodicity"),      &AudioEffectQEngine::get_min_periodicity);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "sample_rate"),     "set_sample_rate",     "get_sample_rate");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "threshold_db"),    "set_threshold_db",    "get_threshold_db");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "tuning"),          "set_tuning",          "get_tuning");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "min_periodicity"), "set_min_periodicity", "get_min_periodicity");
}

// ─────────────────────────────────────────────────────────────────────────────
// poll_notes
// ─────────────────────────────────────────────────────────────────────────────

Array AudioEffectQEngine::poll_notes()
{
    ensure_detector();

    // Drain the capture buffer and push downmixed mono into the detector.
    int64_t available = get_frames_available();
    if (available > 0) {
        PackedVector2Array frames = get_buffer(available);
        const Vector2*     data  = frames.ptr();
        for (int64_t i = 0; i < frames.size(); ++i) {
            float mono = (data[i].x + data[i].y) * 0.5f;
            detector->push_samples(&mono, 1);
        }
    }

    // Run detection and convert results to a Godot Array of Dictionaries.
    auto  results = detector->process();
    float min_p   = static_cast<float>(min_periodicity);

    Array out;
    for (const auto& r : results) {
        if (r.raw_freq <= 0.0f || r.periodicity < min_p) {
            continue;
        }
        Dictionary d;
        d["band"]        = r.band;
        d["string"]      = String(r.string_label.c_str());
        d["frequency"]   = static_cast<double>(r.raw_freq);
        d["periodicity"] = static_cast<double>(r.periodicity);
        if (r.note) {
            d["note"]  = String(r.note->display().c_str());
            d["cents"] = static_cast<double>(r.note->cents);
        } else {
            d["note"]  = String("");
            d["cents"] = 0.0;
        }
        out.push_back(d);
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
// ensure_detector (private)
// ─────────────────────────────────────────────────────────────────────────────

void AudioEffectQEngine::ensure_detector()
{
    bool needs_rebuild = !detector
        || cfg_sample_rate     != sample_rate
        || cfg_threshold_db    != threshold_db
        || cfg_min_periodicity != min_periodicity
        || cfg_tuning          != tuning;

    if (!needs_rebuild) {
        return;
    }

    TuningId tid = tuning_from_string(tuning.utf8().get_data());
    detector = std::make_unique<BandDetector>(
        static_cast<float>(sample_rate),
        tid,
        static_cast<float>(threshold_db)
    );
    detector->set_min_periodicity(static_cast<float>(min_periodicity));

    cfg_sample_rate     = sample_rate;
    cfg_threshold_db    = threshold_db;
    cfg_min_periodicity = min_periodicity;
    cfg_tuning          = tuning;
    clear_buffer();
}

} // namespace godot
