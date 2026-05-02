// tuning.hpp – Guitar tuning definitions.
//
// Maps TuningId to the 6-string open frequencies and detector range.
// Header-only; no Godot dependency.

#pragma once

#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// TuningId
// ─────────────────────────────────────────────────────────────────────────────

enum class TuningId {
    Standard = 0,  ///< E A D G B E
    DropD    = 1,  ///< D A D G B E
    OpenD    = 2,  ///< D A D F# A D
    DropC    = 3,  ///< C G C F A D
    Dadgad   = 4,  ///< D A D G A D
};

/// Parse a tuning name string (case-insensitive, ignores '-' '_' ' ').
inline TuningId tuning_from_string(const std::string& s) {
    // Build a normalised lowercase key
    std::string key;
    key.reserve(s.size());
    for (char c : s) {
        if (c == '-' || c == '_' || c == ' ') continue;
        key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (key == "dropd" || key == "droped") return TuningId::DropD;
    if (key == "opend")                    return TuningId::OpenD;
    if (key == "dropc")                    return TuningId::DropC;
    if (key == "dadgad")                   return TuningId::Dadgad;
    return TuningId::Standard; // "standard", "estandard", or unknown
}

// ─────────────────────────────────────────────────────────────────────────────
// StringInfo – per-string metadata
// ─────────────────────────────────────────────────────────────────────────────

struct StringInfo {
    int         number;    ///< 1-based (1 = highest pitch, 6 = lowest)
    const char* note_name; ///< open-string note, e.g. "E", "F#"
    int         octave;    ///< open-string octave
    float       open_hz;   ///< open-string frequency in Hz
    float       freq_min;  ///< detector lower bound
    float       freq_max;  ///< detector upper bound (24 frets above open)
};

// ─────────────────────────────────────────────────────────────────────────────
// Tuning – complete 6-string descriptor
// ─────────────────────────────────────────────────────────────────────────────

struct Tuning {
    TuningId                   id;
    std::array<StringInfo, 6>  strings;
};

// ─────────────────────────────────────────────────────────────────────────────
// Frequency-range helpers (same logic as q_bridge.cpp)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float half_semi_below(float f)  { return f * 0.97153f; }  // 2^(-0.5/12)
static constexpr float two_octaves_above(float f) { return f * 4.0f; }

// ─────────────────────────────────────────────────────────────────────────────
// get_tuning() factory
// ─────────────────────────────────────────────────────────────────────────────

inline Tuning get_tuning(TuningId id) {
    switch (id) {

    case TuningId::Standard:
        return {id, {{
            {6, "E",  2,  82.41f, half_semi_below( 82.41f), two_octaves_above( 82.41f)},
            {5, "A",  2, 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f)},
            {4, "D",  3, 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f)},
            {3, "G",  3, 196.00f, half_semi_below(196.00f), two_octaves_above(196.00f)},
            {2, "B",  3, 246.94f, half_semi_below(246.94f), two_octaves_above(246.94f)},
            {1, "E",  4, 329.63f, half_semi_below(329.63f), two_octaves_above(329.63f)},
        }}};

    case TuningId::DropD:
        return {id, {{
            {6, "D",  2,  73.42f, half_semi_below( 73.42f), two_octaves_above( 73.42f)},
            {5, "A",  2, 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f)},
            {4, "D",  3, 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f)},
            {3, "G",  3, 196.00f, half_semi_below(196.00f), two_octaves_above(196.00f)},
            {2, "B",  3, 246.94f, half_semi_below(246.94f), two_octaves_above(246.94f)},
            {1, "E",  4, 329.63f, half_semi_below(329.63f), two_octaves_above(329.63f)},
        }}};

    case TuningId::OpenD:
        return {id, {{
            {6, "D",  2,  73.42f, half_semi_below( 73.42f), two_octaves_above( 73.42f)},
            {5, "A",  2, 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f)},
            {4, "D",  3, 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f)},
            {3, "F#", 3, 185.00f, half_semi_below(185.00f), two_octaves_above(185.00f)},
            {2, "A",  3, 220.00f, half_semi_below(220.00f), two_octaves_above(220.00f)},
            {1, "D",  4, 293.66f, half_semi_below(293.66f), two_octaves_above(293.66f)},
        }}};

    case TuningId::DropC:
        return {id, {{
            {6, "C",  2,  65.41f, half_semi_below( 65.41f), two_octaves_above( 65.41f)},
            {5, "G",  2,  98.00f, half_semi_below( 98.00f), two_octaves_above( 98.00f)},
            {4, "C",  3, 130.81f, half_semi_below(130.81f), two_octaves_above(130.81f)},
            {3, "F",  3, 174.61f, half_semi_below(174.61f), two_octaves_above(174.61f)},
            {2, "A",  3, 220.00f, half_semi_below(220.00f), two_octaves_above(220.00f)},
            {1, "D",  4, 293.66f, half_semi_below(293.66f), two_octaves_above(293.66f)},
        }}};

    case TuningId::Dadgad:
        return {id, {{
            {6, "D",  2,  73.42f, half_semi_below( 73.42f), two_octaves_above( 73.42f)},
            {5, "A",  2, 110.00f, half_semi_below(110.00f), two_octaves_above(110.00f)},
            {4, "D",  3, 146.83f, half_semi_below(146.83f), two_octaves_above(146.83f)},
            {3, "G",  3, 196.00f, half_semi_below(196.00f), two_octaves_above(196.00f)},
            {2, "A",  3, 220.00f, half_semi_below(220.00f), two_octaves_above(220.00f)},
            {1, "D",  4, 293.66f, half_semi_below(293.66f), two_octaves_above(293.66f)},
        }}};
    }
    return get_tuning(TuningId::Standard); // unreachable fallback
}
