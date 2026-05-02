// detector_node.hpp – QEngineDetectorNode: a Godot Node for manual audio
// routing.  Receives PCM frames via push_samples() and emits the
// "notes_detected" signal (or returns notes from poll_notes()).

#pragma once

#include <memory>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "band_detector.hpp"

namespace godot {

class QEngineDetectorNode : public Node {
    GDCLASS(QEngineDetectorNode, Node)

public:
    static void _bind_methods();

    // ── Node callbacks ────────────────────────────────────────────────────
    void _ready()                override;
    void _process(double delta)  override;

    // ── GDScript API ──────────────────────────────────────────────────────

    /// (Re)build the internal BandDetector using the current property values.
    void  init_detector();

    /// Buffer PCM samples.  Call from GDScript (or AudioEffectCapture glue).
    void  push_samples(const PackedFloat32Array& samples);

    /// Process buffered samples, emit "notes_detected", and return the Array.
    Array poll_notes();

    /// Reset all Q detectors.
    void  reset();

    // ── Exported properties ───────────────────────────────────────────────

    void   set_sample_rate(double v)      { sample_rate = v; }
    double get_sample_rate()      const   { return sample_rate; }

    void   set_threshold_db(double v)     { threshold_db = v; }
    double get_threshold_db()     const   { return threshold_db; }

    void   set_tuning(const String& v)    { tuning = v; }
    String get_tuning()           const   { return tuning; }

    void   set_min_periodicity(double v)  { min_periodicity = v; }
    double get_min_periodicity()  const   { return min_periodicity; }

    void   set_auto_poll(bool v)          { auto_poll = v; }
    bool   get_auto_poll()        const   { return auto_poll; }

private:
    double sample_rate     = 44100.0;
    double threshold_db    = -45.0;
    String tuning          = "Standard";
    double min_periodicity = 0.8;
    bool   auto_poll       = true;

    std::unique_ptr<BandDetector> detector;

    Array poll_notes_internal();
};

} // namespace godot
