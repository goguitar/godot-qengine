// audio_effect_qengine.cpp – AudioEffectQEngine implementation.
//
// Architecture v2: the C++ layer is a real-time analysis engine, not a
// gameplay judge.  It exposes:
//   poll_notes()           – per-string tuner rows (legacy, kept for UI)
//   get_latest_detection() – best-pitch snapshot for this frame
//   pop_note_events()      – onset-triggered event queue (chart matching)
//   get_frame_history(n)   – last n frames (sustain/bend/vibrato analysis)
//
// Note-name mapping and chart-aware judgment live in GDScript.
// Band frequency ranges must be set from GDScript via band_ranges before
// any detection works.

#include "audio_effect_qengine.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <array>
#include <cmath>

namespace godot {

namespace {
void invalidate_result(DetectionResult& r)
{
    r.raw_freq = 0.0f;
    r.periodicity = 0.0f;
    r.midi_note = -1;
    r.cents = 0.0f;
}

bool is_same_pitch(const DetectionResult& a, const DetectionResult& b)
{
    if (a.raw_freq <= 0.0f || b.raw_freq <= 0.0f) {
        return false;
    }
    const double cents = 1200.0 * std::log2(static_cast<double>(a.raw_freq) / static_cast<double>(b.raw_freq));
    return std::abs(cents) <= 35.0;
}

void apply_q_filters(std::vector<DetectionResult>& results, float min_periodicity)
{
    for (auto& r : results) {
        const bool valid = r.midi_note >= 0 && r.midi_note <= 127
            && r.raw_freq > 0.0f
            && r.periodicity >= min_periodicity;
        if (!valid) {
            invalidate_result(r);
        }
    }

    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        if (results[i].midi_note == -1) {
            continue;
        }
        for (int j = i + 1; j < static_cast<int>(results.size()); ++j) {
            if (results[j].midi_note == -1 || !is_same_pitch(results[i], results[j])) {
                continue;
            }
            if (results[j].periodicity > results[i].periodicity) {
                invalidate_result(results[i]);
                break;
            }
            invalidate_result(results[j]);
        }
    }
}

Dictionary make_chord_row(const std::vector<DetectionResult>& results)
{
    static constexpr const char* NOTE_NAMES[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    std::array<bool, 12> seen{};
    seen.fill(false);
    PackedStringArray chord_notes;
    for (const auto& r : results) {
        if (r.midi_note < 0 || r.midi_note > 127) {
            continue;
        }
        const int cls = ((r.midi_note % 12) + 12) % 12;
        if (!seen[cls]) {
            chord_notes.append(String(NOTE_NAMES[cls]));
            seen[cls] = true;
        }
    }

    Dictionary chord;
    chord["band"] = 6;
    chord["kind"] = String("chord");
    chord["frequency"] = 0.0;
    chord["periodicity"] = 0.0;
    chord["midi_note"] = -1;
    chord["cents"] = 0.0;
    chord["chord_notes"] = chord_notes;
    return chord;
}

Dictionary frame_to_dict(const DetectionFrame& f)
{
    Dictionary d;
    d["time_sec"]    = f.time_sec;
    d["pitch_hz"]    = static_cast<double>(f.pitch_hz);
    d["midi_note"]   = f.midi_note;
    d["midi_float"]  = static_cast<double>(f.midi_float);
    d["confidence"]  = static_cast<double>(f.confidence);
    d["level"]       = static_cast<double>(f.level);
    d["onset"]       = f.onset;
    d["pitch_valid"] = f.pitch_valid;
    return d;
}

Dictionary event_to_dict(const NoteEvent& ev)
{
    Dictionary d;
    d["time_sec"]   = ev.time_sec;
    d["pitch_hz"]   = static_cast<double>(ev.pitch_hz);
    d["midi_note"]  = ev.midi_note;
    d["confidence"] = static_cast<double>(ev.confidence);
    d["level"]      = static_cast<double>(ev.level);
    return d;
}

Dictionary string_component_to_dict(const StringComponent& sc)
{
    Dictionary d;
    d["band"]       = sc.band;
    d["pitch_hz"]   = static_cast<double>(sc.pitch_hz);
    d["midi_float"] = static_cast<double>(sc.midi_float);
    d["midi_note"]  = sc.midi_note;
    d["confidence"] = static_cast<double>(sc.confidence);
    d["cents"]      = static_cast<double>(sc.cents);
    d["active"]     = sc.active;
    return d;
}

Dictionary chord_frame_to_dict(const ChordFrame& cf)
{
    Dictionary d;
    d["time_sec"]            = cf.time_sec;
    d["level"]               = static_cast<double>(cf.level);
    d["dominant_band"]       = cf.dominant_band;
    d["dominant_midi"]       = cf.dominant_midi;
    d["dominant_pitch_hz"]   = static_cast<double>(cf.dominant_pitch_hz);
    d["dominant_confidence"] = static_cast<double>(cf.dominant_confidence);
    d["active_count"]        = cf.active_count;
    Array strings;
    strings.resize(6);
    for (int b = 0; b < 6; ++b)
        strings[b] = string_component_to_dict(cf.strings[b]);
    d["strings"] = strings;
    return d;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// _bind_methods
// ─────────────────────────────────────────────────────────────────────────────

void AudioEffectQEngine::_bind_methods()
{
    // Legacy per-string API
    ClassDB::bind_method(D_METHOD("poll_notes"), &AudioEffectQEngine::poll_notes);
    ClassDB::bind_method(D_METHOD("reset"),      &AudioEffectQEngine::reset);

    // New analysis-state API
    ClassDB::bind_method(D_METHOD("get_latest_detection"),        &AudioEffectQEngine::get_latest_detection);
    ClassDB::bind_method(D_METHOD("pop_note_events"),             &AudioEffectQEngine::pop_note_events);
    ClassDB::bind_method(D_METHOD("get_frame_history", "count"),  &AudioEffectQEngine::get_frame_history);
    ClassDB::bind_method(D_METHOD("pop_chord_frames"),            &AudioEffectQEngine::pop_chord_frames);

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
// poll_notes  (legacy per-string tuner row API)
// ─────────────────────────────────────────────────────────────────────────────

Array AudioEffectQEngine::poll_notes()
{
    ensure_detector();

    // Return 7 rows (6 strings + 1 chord summary) if detector is not ready yet.
    if (!detector) {
        Array out;
        out.resize(7);
        for (int i = 0; i < 6; ++i) {
            Dictionary d;
            d["band"] = i; d["frequency"] = 0.0; d["periodicity"] = 0.0;
            d["midi_note"] = -1; d["cents"] = 0.0;
            out[i] = d;
        }
        std::vector<DetectionResult> empty_results;
        out[6] = make_chord_row(empty_results);
        return out;
    }

    // Drain the Godot capture buffer.
    const int64_t available = get_frames_available();
    if (available > 0) {
        const PackedVector2Array frames = get_buffer(available);
        const int64_t            n      = frames.size();
        const Vector2*           data   = frames.ptr();

        std::vector<float> mono(static_cast<std::size_t>(n));
        for (int64_t i = 0; i < n; ++i)
            mono[i] = data[i].x;

        detector->push_samples(mono.data(), mono.size());
    }

    // Drain ring → detectors, then return 7 rows:
    // 6 per-string rows plus a chord summary row at index 6.
    auto results = detector->process();
    apply_q_filters(results, static_cast<float>(min_periodicity));

    Array out;
    out.resize(7);
    for (const auto& r : results) {
        Dictionary d;
        d["band"]        = r.band;
        d["frequency"]   = static_cast<double>(r.raw_freq);
        d["periodicity"] = static_cast<double>(r.periodicity);
        d["midi_note"]   = r.midi_note;
        d["cents"]       = static_cast<double>(r.cents);
        out[r.band] = d;
    }
    out[6] = make_chord_row(results);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// get_latest_detection  – snapshot of the best pitch this frame
// ─────────────────────────────────────────────────────────────────────────────

Dictionary AudioEffectQEngine::get_latest_detection() const
{
    if (!detector)
        return frame_to_dict(DetectionFrame{});
    return frame_to_dict(detector->latest_frame());
}

// ─────────────────────────────────────────────────────────────────────────────
// pop_note_events  – drain onset event queue for chart matching
// ─────────────────────────────────────────────────────────────────────────────

Array AudioEffectQEngine::pop_note_events()
{
    Array out;
    if (!detector)
        return out;
    NoteEvent ev;
    while (detector->pop_event(ev))
        out.append(event_to_dict(ev));
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// get_frame_history  – recent frame list for sustain/bend/vibrato
// ─────────────────────────────────────────────────────────────────────────────

Array AudioEffectQEngine::get_frame_history(int count) const
{
    Array out;
    if (!detector || count <= 0)
        return out;
    const std::size_t n = static_cast<std::size_t>(count);
    std::vector<DetectionFrame> tmp(n);
    const std::size_t got = detector->get_frame_history(tmp.data(), n);
    for (std::size_t i = 0; i < got; ++i)
        out.append(frame_to_dict(tmp[i]));
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// pop_chord_frames  – drain per-string chord frame queue
// ─────────────────────────────────────────────────────────────────────────────

Array AudioEffectQEngine::pop_chord_frames()
{
    Array out;
    if (!detector)
        return out;
    ChordFrame cf;
    while (detector->pop_chord_frame(cf))
        out.append(chord_frame_to_dict(cf));
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
    std::array<BandRange, 6> ranges{};
    if (band_ranges.size() >= 12) {
        for (int i = 0; i < 6; ++i)
            ranges[i] = { band_ranges[i * 2], band_ranges[i * 2 + 1] };
    }
    return ranges;
}

// ─────────────────────────────────────────────────────────────────────────────
// ensure_detector (private)
// ─────────────────────────────────────────────────────────────────────────────

void AudioEffectQEngine::ensure_detector()
{
    if (band_ranges.size() < 12) {
        detector.reset();
        return;
    }

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
