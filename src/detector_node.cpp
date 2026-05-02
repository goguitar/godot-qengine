// detector_node.cpp – QEngineDetectorNode implementation.

#include "detector_node.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

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

    // Drain ring → detectors, then build one Dictionary per band/string.
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
