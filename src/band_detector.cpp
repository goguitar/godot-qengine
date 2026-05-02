// band_detector.cpp – Multi-band guitar pitch detector implementation.

#include "band_detector.hpp"

#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
#include <q/support/pitch.hpp>
#include <q/support/unit.hpp>

#include <cassert>
#include <cmath>

namespace q = cycfi::q;
using namespace q::literals;

// Convert a plain float dB value to cycfi::q::decibel without any lin→dB
// conversion (the value IS already in dB).
static inline q::decibel db_from_float(float val)
{
    return q::decibel{static_cast<double>(val), q::direct_unit};
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

BandDetector::BandDetector(float                           sample_rate,
                           const std::array<BandRange, 6>& ranges,
                           float                           threshold_db)
{
    for (int i = 0; i < 6; ++i) {
        _detectors[i] = std::make_unique<q::pitch_detector>(
            q::frequency{ranges[i].freq_min},
            q::frequency{ranges[i].freq_max},
            sample_rate,
            db_from_float(threshold_db)
        );
    }
}

BandDetector::~BandDetector() = default;

// ─────────────────────────────────────────────────────────────────────────────
// push_samples
// ─────────────────────────────────────────────────────────────────────────────

void BandDetector::push_samples(const float* samples, std::size_t count)
{
    _buffer.insert(_buffer.end(), samples, samples + count);
}

// ─────────────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────────────

std::vector<DetectionResult> BandDetector::process()
{
    // Feed every buffered sample into all 6 band detectors simultaneously.
    for (float s : _buffer) {
        for (auto& pd : _detectors) {
            (*pd)(s);
        }
    }
    _buffer.clear();

    // Collect per-band results using Q's pitch type for MIDI note and cents.
    std::vector<DetectionResult> results;
    results.reserve(6);

    for (int b = 0; b < 6; ++b) {
        const float raw  = _detectors[b]->get_frequency();
        const float per  = _detectors[b]->periodicity();

        int   midi  = -1;
        float cents = 0.0f;

        if (raw > 0.0f) {
            // q::pitch is the canonical Q type for MIDI-mapped pitch values.
            // Constructing it from a frequency gives rep = MIDI note (as double).
            // as_int(round(p)) → nearest integer MIDI note.
            // (p.rep - round(p.rep)) * 100 → cents deviation [-50, +50].
            q::pitch p{q::frequency{raw}};
            if (p.valid()) {
                const double rounded = std::round(p.rep);
                const int    note    = static_cast<int>(rounded);
                if (note >= 0 && note <= 127) {
                    midi  = note;
                    cents = static_cast<float>((p.rep - rounded) * 100.0);
                }
            }
        }

        results.push_back(DetectionResult{b, raw, per, midi, cents});
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// reset
// ─────────────────────────────────────────────────────────────────────────────

void BandDetector::reset()
{
    for (auto& pd : _detectors) {
        pd->reset();
    }
    _buffer.clear();
}
