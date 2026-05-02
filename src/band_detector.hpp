// band_detector.hpp – Multi-band guitar pitch detector.
//
// BandDetector wraps six cycfi/Q pitch_detector instances (one per guitar
// string) and buffers incoming audio samples until process() is called.
//
// No Godot dependency – usable from tests without a Godot installation.

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "note.hpp"
#include "tuning.hpp"

// Forward-declare the Q type to avoid pulling in heavy Q templates in the header.
namespace cycfi { namespace q { class pitch_detector; } }

// ─────────────────────────────────────────────────────────────────────────────
// DetectionResult – result for one string band
// ─────────────────────────────────────────────────────────────────────────────

struct DetectionResult {
    int                     band;         ///< 0 = lowest string
    std::string             string_label; ///< e.g. "E2"
    float                   raw_freq;     ///< detected Hz (0 if none)
    float                   periodicity;  ///< confidence [0, 1]
    std::optional<DetectedNote> note;     ///< identified note, if any
};

// ─────────────────────────────────────────────────────────────────────────────
// BandDetector
// ─────────────────────────────────────────────────────────────────────────────

class BandDetector {
public:
    /// Minimum periodicity (confidence) used by default.
    static constexpr float DEFAULT_MIN_PERIODICITY = 0.8f;

    /// Construct a 6-band detector.
    /// @param sample_rate   Audio sample rate (Hz).
    /// @param tuning_id     Which guitar tuning to use.
    /// @param threshold_db  Noise-floor threshold in dB (negative, e.g. -45).
    BandDetector(float    sample_rate,
                 TuningId tuning_id,
                 float    threshold_db = -45.0f);

    ~BandDetector();

    // Non-copyable / movable
    BandDetector(const BandDetector&)            = delete;
    BandDetector& operator=(const BandDetector&) = delete;
    BandDetector(BandDetector&&)                 = default;
    BandDetector& operator=(BandDetector&&)      = default;

    /// Buffer audio samples (safe to call from any thread).
    void push_samples(const float* samples, std::size_t count);

    /// Process all buffered samples through all 6 band detectors and return
    /// one DetectionResult per band.  Always returns exactly 6 results.
    std::vector<DetectionResult> process();

    /// Reset all Q detectors and clear the pending sample buffer.
    void reset();

    void  set_min_periodicity(float v) { _min_periodicity = std::max(0.0f, std::min(1.0f, v)); }
    float min_periodicity()  const     { return _min_periodicity; }
    const Tuning& tuning()   const     { return _tuning; }

private:
    struct Band {
        std::unique_ptr<cycfi::q::pitch_detector> detector;
        StringInfo                                 info;
    };

    float                    _sample_rate;
    Tuning                   _tuning;
    std::array<Band, 6>      _bands;
    std::vector<float>       _buffer;
    float                    _min_periodicity = DEFAULT_MIN_PERIODICITY;
};
