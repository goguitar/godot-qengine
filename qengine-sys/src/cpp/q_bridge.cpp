/*
 * q_bridge.cpp  –  Thin C wrapper around cycfi/Q pitch detection.
 *
 * Compiled with C++20 (required by cycfi/Q).  Exposes a plain-C API so
 * that Rust can call it through `extern "C"` without knowing anything
 * about C++ templates or the Q type system.
 */

#include "q_bridge.h"

#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
#include <q/support/unit.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace q = cycfi::q;
using namespace q::literals;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

// Construct a decibel from a plain float.
// decibel(double) is deleted in cycfi/Q to prevent confusion with the old
// lin-to-dB semantics, so we use the `direct_unit` back-door that bypasses
// any conversion – the value IS already in dB.
static inline q::decibel db_from_float(float val)
{
    return q::decibel{ static_cast<double>(val), q::direct_unit };
}

/* -------------------------------------------------------------------------
 * Per-band pitch detector wrapper
 * ---------------------------------------------------------------------- */

struct QPitchDetector
{
    q::pitch_detector pd;

    QPitchDetector(float min_hz, float max_hz, float sps, float threshold_db)
        : pd(q::frequency{ min_hz },
             q::frequency{ max_hz },
             sps,
             db_from_float(threshold_db))
    {}
};

/* -------------------------------------------------------------------------
 * Guitar tuning definitions  (open-string frequencies for 6 strings)
 * Each entry: { string_name, open_hz, freq_min_hz, freq_max_hz }
 * freq_min  = half-semitone below open note
 * freq_max  = 24 frets above open note (= 2 octaves = 4× open)
 * ---------------------------------------------------------------------- */

struct StringDef
{
    const char* name;
    float       open_hz;
    float       freq_min;
    float       freq_max;
};

// Convenience: compute half-semitone below and 24-fret ceiling
static constexpr float half_semi_below(float f) { return f * 0.97153f; }  // 2^(-0.5/12)
static constexpr float two_octaves_above(float f) { return f * 4.0f; }

static const StringDef TUNINGS[5][6] = {
    // 0 – Standard E:  E2  A2  D3  G3  B3  E4
    {
        { "E2",  82.41f, half_semi_below(82.41f),  two_octaves_above(82.41f)  },
        { "A2", 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f) },
        { "D3", 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f) },
        { "G3", 196.00f, half_semi_below(196.00f), two_octaves_above(196.00f) },
        { "B3", 246.94f, half_semi_below(246.94f), two_octaves_above(246.94f) },
        { "E4", 329.63f, half_semi_below(329.63f), two_octaves_above(329.63f) },
    },
    // 1 – Drop D:  D2  A2  D3  G3  B3  E4
    {
        { "D2",  73.42f, half_semi_below(73.42f),  two_octaves_above(73.42f)  },
        { "A2", 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f) },
        { "D3", 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f) },
        { "G3", 196.00f, half_semi_below(196.00f), two_octaves_above(196.00f) },
        { "B3", 246.94f, half_semi_below(246.94f), two_octaves_above(246.94f) },
        { "E4", 329.63f, half_semi_below(329.63f), two_octaves_above(329.63f) },
    },
    // 2 – Open D:  D2  A2  D3  F#3  A3  D4
    {
        { "D2",  73.42f, half_semi_below(73.42f),  two_octaves_above(73.42f)  },
        { "A2", 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f) },
        { "D3", 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f) },
        { "F#3",185.00f, half_semi_below(185.00f), two_octaves_above(185.00f) },
        { "A3", 220.00f, half_semi_below(220.00f), two_octaves_above(220.00f) },
        { "D4", 293.66f, half_semi_below(293.66f), two_octaves_above(293.66f) },
    },
    // 3 – Drop C:  C2  G2  C3  F3  A3  D4
    {
        { "C2",  65.41f, half_semi_below(65.41f),  two_octaves_above(65.41f)  },
        { "G2",  98.00f, half_semi_below(98.00f),  two_octaves_above(98.00f)  },
        { "C3", 130.81f, half_semi_below(130.81f), two_octaves_above(130.81f) },
        { "F3", 174.61f, half_semi_below(174.61f), two_octaves_above(174.61f) },
        { "A3", 220.00f, half_semi_below(220.00f), two_octaves_above(220.00f) },
        { "D4", 293.66f, half_semi_below(293.66f), two_octaves_above(293.66f) },
    },
    // 4 – DADGAD:  D2  A2  D3  G3  A3  D4
    {
        { "D2",  73.42f, half_semi_below(73.42f),  two_octaves_above(73.42f)  },
        { "A2", 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f) },
        { "D3", 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f) },
        { "G3", 196.00f, half_semi_below(196.00f), two_octaves_above(196.00f) },
        { "A3", 220.00f, half_semi_below(220.00f), two_octaves_above(220.00f) },
        { "D4", 293.66f, half_semi_below(293.66f), two_octaves_above(293.66f) },
    },
};

/* -------------------------------------------------------------------------
 * 6-band guitar detector
 * ---------------------------------------------------------------------- */

struct QGuitarDetector
{
    static constexpr int BANDS = 6;

    std::array<std::unique_ptr<QPitchDetector>, BANDS> detectors;

    QGuitarDetector(int tuning_id, float sps, float threshold_db)
    {
        if (tuning_id < 0 || tuning_id > 4) tuning_id = 0;
        const auto& t = TUNINGS[tuning_id];
        for (int i = 0; i < BANDS; ++i)
        {
            detectors[i] = std::make_unique<QPitchDetector>(
                t[i].freq_min, t[i].freq_max, sps, threshold_db);
        }
    }
};

/* =========================================================================
 * C API – single band
 * ====================================================================== */

extern "C"
{

void* q_pitch_detector_new(float min_hz,
                           float max_hz,
                           float sample_rate,
                           float threshold_db)
{
    return static_cast<void*>(
        new QPitchDetector(min_hz, max_hz, sample_rate, threshold_db));
}

void q_pitch_detector_free(void* handle)
{
    delete static_cast<QPitchDetector*>(handle);
}

bool q_pitch_detector_process(void* handle, float sample)
{
    return static_cast<QPitchDetector*>(handle)->pd(sample);
}

float q_pitch_detector_get_frequency(void* handle)
{
    return static_cast<QPitchDetector*>(handle)->pd.get_frequency();
}

float q_pitch_detector_periodicity(void* handle)
{
    return static_cast<QPitchDetector*>(handle)->pd.periodicity();
}

void q_pitch_detector_reset(void* handle)
{
    static_cast<QPitchDetector*>(handle)->pd.reset();
}

float q_pitch_detector_process_buffer(void*        handle,
                                      const float* samples,
                                      int          count)
{
    auto* det = static_cast<QPitchDetector*>(handle);
    for (int i = 0; i < count; ++i)
        det->pd(samples[i]);
    return det->pd.get_frequency();
}

/* =========================================================================
 * C API – multi-band guitar detector
 * ====================================================================== */

void* q_guitar_detector_new(int   tuning_id,
                            float sample_rate,
                            float threshold_db)
{
    return static_cast<void*>(
        new QGuitarDetector(tuning_id, sample_rate, threshold_db));
}

void q_guitar_detector_free(void* handle)
{
    delete static_cast<QGuitarDetector*>(handle);
}

void q_guitar_detector_process(void*  handle,
                               float  sample,
                               float* out_frequencies,
                               float* out_periodicities)
{
    auto* gd = static_cast<QGuitarDetector*>(handle);
    for (int i = 0; i < QGuitarDetector::BANDS; ++i)
    {
        gd->detectors[i]->pd(sample);
        out_frequencies[i]   = gd->detectors[i]->pd.get_frequency();
        out_periodicities[i] = gd->detectors[i]->pd.periodicity();
    }
}

void q_guitar_detector_process_buffer(void*        handle,
                                      const float* samples,
                                      int          count,
                                      float*       out_frequencies,
                                      float*       out_periodicities)
{
    auto* gd = static_cast<QGuitarDetector*>(handle);
    for (int s = 0; s < count; ++s)
        for (int b = 0; b < QGuitarDetector::BANDS; ++b)
            gd->detectors[b]->pd(samples[s]);
    for (int b = 0; b < QGuitarDetector::BANDS; ++b)
    {
        out_frequencies[b]   = gd->detectors[b]->pd.get_frequency();
        out_periodicities[b] = gd->detectors[b]->pd.periodicity();
    }
}

void q_guitar_detector_reset(void* handle)
{
    auto* gd = static_cast<QGuitarDetector*>(handle);
    for (auto& d : gd->detectors)
        d->pd.reset();
}

} // extern "C"
