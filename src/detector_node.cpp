// detector_node.cpp – QEngineDetectorNode implementation.

#include "detector_node.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// ─────────────────────────────────────────────────────────────────────────────
// E Standard default band ranges (half-semitone below open, 2 octaves above)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr BandRange STANDARD_RANGES[6] = {
    { 80.11f,  329.64f },
    { 106.87f, 440.00f },
    { 142.65f, 587.32f },
    { 190.42f, 784.00f },
    { 239.91f, 987.76f },
    { 320.25f, 1318.52f },
};

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
    if (auto_poll) {
        Array notes = poll_notes_internal();
        emit_signal("notes_detected", notes);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// current_ranges (private) – build array from band_ranges or Standard defaults
// ─────────────────────────────────────────────────────────────────────────────

std::array<BandRange, 6> QEngineDetectorNode::current_ranges() const
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
// GDScript API
// ─────────────────────────────────────────────────────────────────────────────

void QEngineDetectorNode::init_detector()
{
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
    Array notes = poll_notes_internal();
    emit_signal("notes_detected", notes);
    return notes;
}

void QEngineDetectorNode::reset()
{
    if (detector) {
        detector->reset();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// poll_notes_internal (private)
// ─────────────────────────────────────────────────────────────────────────────

Array QEngineDetectorNode::poll_notes_internal()
{
    if (!detector) {
        return Array();
    }

    auto  results = detector->process();
    float min_p   = static_cast<float>(min_periodicity);

    Array out;
    for (const auto& r : results) {
        if (r.raw_freq <= 0.0f || r.periodicity < min_p) {
            continue;
        }
        Dictionary d;
        d["band"]        = r.band;
        d["frequency"]   = static_cast<double>(r.raw_freq);
        d["periodicity"] = static_cast<double>(r.periodicity);
        d["midi_note"]   = r.midi_note;
        d["cents"]       = static_cast<double>(r.cents);
        out.push_back(d);
    }
    return out;
}

} // namespace godot
