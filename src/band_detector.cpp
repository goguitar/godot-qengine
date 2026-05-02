// band_detector.cpp – Multi-band guitar pitch detector implementation.

#include "band_detector.hpp"

#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
#include <q/support/unit.hpp>

#include <cassert>

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

BandDetector::BandDetector(float sample_rate, TuningId tuning_id, float threshold_db)
    : _sample_rate(sample_rate)
    , _tuning(get_tuning(tuning_id))
{
    for (int i = 0; i < 6; ++i) {
        const StringInfo& s = _tuning.strings[i];
        _bands[i].info = s;
        _bands[i].detector = std::make_unique<q::pitch_detector>(
            q::frequency{s.freq_min},
            q::frequency{s.freq_max},
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
        for (auto& band : _bands) {
            (*band.detector)(s);
        }
    }
    _buffer.clear();

    // Collect per-band results.
    std::vector<DetectionResult> results;
    results.reserve(6);

    for (int b = 0; b < 6; ++b) {
        const auto& band  = _bands[b];
        float freq = band.detector->get_frequency();
        float per  = band.detector->periodicity();

        std::optional<DetectedNote> note_opt;
        if (freq > 0.0f) {
            note_opt = DetectedNote::from_frequency(freq);
        }

        const StringInfo& si = band.info;
        results.push_back(DetectionResult{
            b,
            std::string(si.note_name) + std::to_string(si.octave),
            freq,
            per,
            note_opt
        });
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// reset
// ─────────────────────────────────────────────────────────────────────────────

void BandDetector::reset()
{
    for (auto& band : _bands) {
        band.detector->reset();
    }
    _buffer.clear();
}
