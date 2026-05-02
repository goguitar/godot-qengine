// note.hpp – Musical note utilities: frequency → note name / MIDI mapping.
//
// Header-only; no Godot dependency.

#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string>

/// The 12 chromatic note names (C = index 0).
static constexpr std::array<const char*, 12> NOTE_NAMES = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/// A detected note with all relevant information.
struct DetectedNote {
    const char* name         = nullptr; ///< e.g. "E", "C#"
    int         octave       = 0;       ///< octave number (A4 = 4)
    int         midi         = 0;       ///< MIDI note (A4 = 69)
    float       exact_freq   = 0.0f;    ///< equal-temperament frequency
    float       detected_freq = 0.0f;  ///< raw detected frequency
    float       cents        = 0.0f;   ///< deviation in cents

    /// Human-readable label, e.g. "E2".
    std::string display() const {
        return std::string(name) + std::to_string(octave);
    }

    /// Build a DetectedNote from a raw detected frequency.
    /// Returns std::nullopt if freq is not a valid guitar pitch.
    static std::optional<DetectedNote> from_frequency(float freq) {
        if (freq <= 0.0f || !std::isfinite(freq)) {
            return std::nullopt;
        }
        // MIDI = 69 + 12·log₂(f / 440)
        float midi_f = 69.0f + 12.0f * std::log2(freq / 440.0f);
        int   midi   = static_cast<int>(std::round(midi_f));
        if (midi < 0 || midi > 127) {
            return std::nullopt;
        }
        int   note_idx = ((midi % 12) + 12) % 12;
        int   octave   = midi / 12 - 1;
        float exact    = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
        float cents    = (midi_f - static_cast<float>(midi)) * 100.0f;
        return DetectedNote{NOTE_NAMES[note_idx], octave, midi, exact, freq, cents};
    }
};

/// Convert a MIDI note number to its equal-temperament frequency.
inline float midi_to_freq(int midi) {
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

/// Convert a frequency to the nearest MIDI note number.
inline std::optional<int> freq_to_midi(float freq) {
    if (freq <= 0.0f || !std::isfinite(freq)) return std::nullopt;
    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(freq / 440.0f)));
}
