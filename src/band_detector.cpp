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
    : _sample_rate(sample_rate)
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
    _ring.push(samples, count);
    _sample_count += count;
}

// ─────────────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────────────

std::vector<DetectionResult> BandDetector::process()
{
    // Drain every buffered sample from the SPSC ring into all 6 band detectors,
    // accumulating RMS level for this block along the way.
    float sum_sq   = 0.0f;
    std::size_t n_drained = 0;

    _ring.drain([&](float s) {
        sum_sq += s * s;
        ++n_drained;
        for (auto& pd : _detectors)
            (*pd)(s);
    });

    const float rms_level = (n_drained > 0)
        ? std::sqrt(sum_sq / static_cast<float>(n_drained))
        : 0.0f;

    // Collect per-band results using Q's pitch type for MIDI note and cents.
    // Always returns exactly 6 results — one per string — so callers can index
    // by band number without filtering.  Bands with no active detection have
    // raw_freq == 0, midi_note == -1, and cents == 0.
    std::vector<DetectionResult> results;
    results.reserve(6);

    for (int b = 0; b < 6; ++b) {
        const float raw = _detectors[b]->get_frequency();
        const float per = _detectors[b]->periodicity();

        int   midi       = -1;
        float cents      = 0.0f;
        float midi_float = -1.0f;

        if (raw > 0.0f) {
            // q::pitch is the canonical Q type for MIDI-mapped pitch values.
            // Constructing it from a frequency gives rep = MIDI note (as double).
            // (p.rep - round(p.rep)) * 100 → cents deviation in [-50, +50].
            q::pitch p{q::frequency{raw}};
            if (p.valid()) {
                midi_float         = static_cast<float>(p.rep);
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

    // ── Build the aggregate DetectionFrame (best valid band) ─────────────────
    const double time_sec = (_sample_rate > 0.0f)
        ? static_cast<double>(_sample_count) / static_cast<double>(_sample_rate)
        : 0.0;

    DetectionFrame frame;
    frame.time_sec = time_sec;
    frame.level    = rms_level;

    // Pick the band with the highest periodicity that also meets the threshold.
    int   best_band = -1;
    float best_per  = 0.0f;
    for (int b = 0; b < 6; ++b) {
        if (results[b].midi_note >= 0 && results[b].raw_freq > 0.0f
                && results[b].periodicity >= _min_periodicity
                && results[b].periodicity > best_per) {
            best_per  = results[b].periodicity;
            best_band = b;
        }
    }

    if (best_band >= 0) {
        const auto& r   = results[best_band];
        q::pitch p{q::frequency{r.raw_freq}};
        frame.pitch_hz   = r.raw_freq;
        frame.midi_note  = r.midi_note;
        frame.midi_float = p.valid() ? static_cast<float>(p.rep) : static_cast<float>(r.midi_note);
        frame.confidence = r.periodicity;
        frame.pitch_valid = true;
    }

    // ── Onset detection ───────────────────────────────────────────────────────
    // An onset fires when a new valid pitch appears after a gap (or after the
    // cooldown has expired), and the RMS level is not negligible.
    if (_onset_cooldown_left > 0) {
        _onset_cooldown_left = (_onset_cooldown_left > n_drained)
            ? _onset_cooldown_left - n_drained : 0;
    }

    const bool was_valid = _latest_frame.pitch_valid;
    if (frame.pitch_valid && !was_valid && _onset_cooldown_left == 0
            && rms_level > 0.01f) {
        frame.onset = true;
        _onset_cooldown_left = ONSET_COOLDOWN_SAMPLES;

        NoteEvent ev;
        ev.time_sec   = time_sec;
        ev.pitch_hz   = frame.pitch_hz;
        ev.midi_note  = frame.midi_note;
        ev.confidence = frame.confidence;
        ev.level      = rms_level;
        _event_queue.push(ev);
    }

    _latest_frame = frame;
    _frame_history.push(frame);

    // ── Build and push ChordFrame (per-string snapshot) ───────────────────────
    // Every process() call pushes one ChordFrame so GDScript can inspect
    // which strings were active (per-string chord detection).
    ChordFrame chord;
    chord.time_sec = time_sec;
    chord.level    = rms_level;

    float best_chord_per = 0.0f;
    for (int b = 0; b < 6; ++b) {
        const auto& r = results[b];
        chord.strings[b].band       = b;
        chord.strings[b].pitch_hz   = r.raw_freq;
        chord.strings[b].midi_note  = r.midi_note;
        chord.strings[b].confidence = r.periodicity;
        chord.strings[b].cents      = r.cents;
        chord.strings[b].active     = (r.midi_note >= 0 && r.raw_freq > 0.0f
                                       && r.periodicity >= _min_periodicity);

        // Compute fractional MIDI for the string component.
        if (r.raw_freq > 0.0f) {
            q::pitch p{q::frequency{r.raw_freq}};
            chord.strings[b].midi_float = p.valid() ? static_cast<float>(p.rep) : -1.0f;
        }

        if (chord.strings[b].active) {
            ++chord.active_count;
            if (r.periodicity > best_chord_per) {
                best_chord_per              = r.periodicity;
                chord.dominant_band         = b;
                chord.dominant_midi         = r.midi_note;
                chord.dominant_pitch_hz     = r.raw_freq;
                chord.dominant_confidence   = r.periodicity;
            }
        }
    }
    _chord_queue.push(chord);

    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// reset
// ─────────────────────────────────────────────────────────────────────────────

void BandDetector::reset()
{
    for (auto& pd : _detectors)
        pd->reset();
    _ring.clear();
    _sample_count        = 0;
    _onset_cooldown_left = 0;
    _latest_frame        = DetectionFrame{};
    _frame_history.clear();
    _event_queue.clear();
    _chord_queue.clear();
}
