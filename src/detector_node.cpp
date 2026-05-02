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
    ClassDB::bind_method(D_METHOD("set_tuning",           "v"), &QEngineDetectorNode::set_tuning);
    ClassDB::bind_method(D_METHOD("get_tuning"),                &QEngineDetectorNode::get_tuning);
    ClassDB::bind_method(D_METHOD("set_min_periodicity",  "v"), &QEngineDetectorNode::set_min_periodicity);
    ClassDB::bind_method(D_METHOD("get_min_periodicity"),       &QEngineDetectorNode::get_min_periodicity);
    ClassDB::bind_method(D_METHOD("set_auto_poll",        "v"), &QEngineDetectorNode::set_auto_poll);
    ClassDB::bind_method(D_METHOD("get_auto_poll"),             &QEngineDetectorNode::get_auto_poll);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "sample_rate"),     "set_sample_rate",     "get_sample_rate");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "threshold_db"),    "set_threshold_db",    "get_threshold_db");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "tuning"),          "set_tuning",          "get_tuning");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,  "min_periodicity"), "set_min_periodicity", "get_min_periodicity");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,   "auto_poll"),       "set_auto_poll",       "get_auto_poll");

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
// GDScript API
// ─────────────────────────────────────────────────────────────────────────────

void QEngineDetectorNode::init_detector()
{
    TuningId tid = tuning_from_string(tuning.utf8().get_data());
    detector = std::make_unique<BandDetector>(
        static_cast<float>(sample_rate),
        tid,
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

} // namespace godot
