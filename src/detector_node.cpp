// detector_node.cpp – QEngineDetectorNode implementation.

#include "detector_node.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
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
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// _bind_methods
// ─────────────────────────────────────────────────────────────────────────────

void QEngineDetectorNode::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("init_detector"),             &QEngineDetectorNode::init_detector);
    ClassDB::bind_method(D_METHOD("push_samples", "samples"),   &QEngineDetectorNode::push_samples);
    ClassDB::bind_method(D_METHOD("poll_notes"),                &QEngineDetectorNode::poll_notes);
    ClassDB::bind_method(D_METHOD("reset"),                     &QEngineDetectorNode::reset);

    ClassDB::bind_method(D_METHOD("set_sample_rate",      "v"), &QEngineDetectorNode::set_sample_rate);
    ClassDB::bind_method(D_METHOD("get_sample_rate"),           &QEngineDetectorNode::get_sample_rate);
    ClassDB::bind_method(D_METHOD("set_threshold_db",     "v"), &QEngineDetectorNode::set_threshold_db);
    ClassDB::bind_method(D_METHOD("get_threshold_db"),          &QEngineDetectorNode::get_threshold_db);
    ClassDB::bind_method(D_METHOD("set_min_periodicity",  "v"), &QEngineDetectorNode::set_min_periodicity);
    ClassDB::bind_method(D_METHOD("get_min_periodicity"),       &QEngineDetectorNode::get_min_periodicity);
    ClassDB::bind_method(D_METHOD("set_band_ranges",      "v"), &QEngineDetectorNode::set_band_ranges);
    ClassDB::bind_method(D_METHOD("get_band_ranges"),           &QEngineDetectorNode::get_band_ranges);
    ClassDB::bind_method(D_METHOD("set_auto_poll",        "v"), &QEngineDetectorNode::set_auto_poll);
    ClassDB::bind_method(D_METHOD("get_auto_poll"),             &QEngineDetectorNode::get_auto_poll);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,                "sample_rate"),     "set_sample_rate",     "get_sample_rate");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,                "threshold_db"),    "set_threshold_db",    "get_threshold_db");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,                "min_periodicity"), "set_min_periodicity", "get_min_periodicity");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "band_ranges"),     "set_band_ranges",     "get_band_ranges");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,                 "auto_poll"),       "set_auto_poll",       "get_auto_poll");

    ADD_SIGNAL(MethodInfo("notes_detected",
        PropertyInfo(Variant::ARRAY, "notes")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Node callbacks
// ─────────────────────────────────────────────────────────────────────────────

void QEngineDetectorNode::_ready()
{
    init_detector();
}

void QEngineDetectorNode::_process(double /*delta*/)
{
    if (auto_poll)
        poll_notes(); // emits "notes_detected" internally
}

// ─────────────────────────────────────────────────────────────────────────────
// current_ranges (private) – build array from band_ranges or Standard defaults
// ─────────────────────────────────────────────────────────────────────────────

std::array<BandRange, 6> QEngineDetectorNode::current_ranges() const
{
    std::array<BandRange, 6> ranges{};
    if (band_ranges.size() >= 12) {
        for (int i = 0; i < 6; ++i)
            ranges[i] = { band_ranges[i * 2], band_ranges[i * 2 + 1] };
    }
    return ranges;
}

// ─────────────────────────────────────────────────────────────────────────────
// GDScript API
// ─────────────────────────────────────────────────────────────────────────────

void QEngineDetectorNode::init_detector()
{
    // band_ranges must be configured from GDScript before init is useful.
    if (band_ranges.size() < 12)
        return;

    detector = std::make_unique<BandDetector>(
        static_cast<float>(sample_rate),
        current_ranges(),
        static_cast<float>(threshold_db)
    );
    detector->set_min_periodicity(static_cast<float>(min_periodicity));
}

void QEngineDetectorNode::push_samples(const PackedFloat32Array& samples)
{
    if (detector && samples.size() > 0) {
        detector->push_samples(samples.ptr(), static_cast<std::size_t>(samples.size()));
    }
}

Array QEngineDetectorNode::poll_notes()
{
    if (!detector)
        return Array();

    // Drain ring → detectors, then build 7 rows:
    // 6 per-string rows plus one chord summary row.
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

    emit_signal("notes_detected", out);
    return out;
}

void QEngineDetectorNode::reset()
{
    if (detector) {
        detector->reset();
    }
}

} // namespace godot
