// test_pitch_detection.cpp – Unit and integration tests for the C++ QEngine.
//
// Tests the same scenarios as the original test suite:
//   - All 6 open strings in E Standard tuning
//   - Fretted notes (1st fret and 12th fret on low E)
//   - Drop D / Drop C / DADGAD low strings
//   - Drop D high strings unchanged
//   - Em chord (per-band, exact pitch classes, note-class set)
//
// No Godot dependency – runs as a standalone executable.
// Tuning data and note-name mapping are defined inline here; in production
// they live in GDScript.

#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include "band_detector.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Inline tuning data  (mirrors the GDScript TUNING_DATA in the demo)
// Format: 6 BandRange entries per tuning (index 0 = lowest string).
// Bounds: half-semitone below open (×0.97153), two octaves above (×4.0).
// ─────────────────────────────────────────────────────────────────────────────

static constexpr std::array<BandRange, 6> STANDARD = {{
    { 80.11f,  329.64f },   // E2  82.41 Hz
    { 106.87f, 440.00f },   // A2  110.00 Hz
    { 142.65f, 587.32f },   // D3  146.83 Hz
    { 190.42f, 784.00f },   // G3  196.00 Hz
    { 239.91f, 987.76f },   // B3  246.94 Hz
    { 320.25f, 1318.52f },  // E4  329.63 Hz
}};

static constexpr std::array<BandRange, 6> DROP_D = {{
    { 71.33f,  293.68f },   // D2  73.42 Hz
    { 106.87f, 440.00f },   // A2  (unchanged)
    { 142.65f, 587.32f },   // D3
    { 190.42f, 784.00f },   // G3
    { 239.91f, 987.76f },   // B3
    { 320.25f, 1318.52f },  // E4
}};

static constexpr std::array<BandRange, 6> DROP_C = {{
    { 63.54f,  261.64f },   // C2  65.41 Hz
    { 95.21f,  392.00f },   // G2  98.00 Hz
    { 127.09f, 523.24f },   // C3  130.81 Hz
    { 169.64f, 698.44f },   // F3  174.61 Hz
    { 213.74f, 880.00f },   // A3  220.00 Hz
    { 285.30f, 1174.64f },  // D4  293.66 Hz
}};

static constexpr std::array<BandRange, 6> DADGAD = {{
    { 71.33f,  293.68f },   // D2  73.42 Hz
    { 106.87f, 440.00f },   // A2
    { 142.65f, 587.32f },   // D3
    { 190.42f, 784.00f },   // G3
    { 213.74f, 880.00f },   // A3  220.00 Hz
    { 285.30f, 1174.64f },  // D4  293.66 Hz
}};

// ─────────────────────────────────────────────────────────────────────────────
// Inline note-name helper  (mirrors freq_to_note_display() in GDScript)
// Returns e.g. "E2", "G#3".  Empty string for out-of-range frequencies.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static std::string freq_to_note_display(float freq)
{
    if (freq <= 0.0f || !std::isfinite(freq)) return "";
    int midi = static_cast<int>(std::round(69.0f + 12.0f * std::log2(freq / 440.0f)));
    if (midi < 0 || midi > 127) return "";
    int octave = midi / 12 - 1;
    return std::string(NOTE_NAMES[((midi % 12) + 12) % 12]) + std::to_string(octave);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Generate a pure mono sine wave.
static std::vector<float> sine_wave(float freq, float sr = 44100.0f, float secs = 1.0f)
{
    std::size_t n = static_cast<std::size_t>(sr * secs);
    std::vector<float> buf(n);
    for (std::size_t i = 0; i < n; ++i) {
        buf[i] = std::sin(
            2.0f * std::numbers::pi_v<float> * freq * static_cast<float>(i) / sr);
    }
    return buf;
}

/// Feed one second of a sine at `freq_in` through band `band_idx` and return
/// the raw detected frequency.  Note-name lookup is done by the caller.
static float run_detection(float                            freq_in,
                           const std::array<BandRange, 6>& ranges,
                           int                             band_idx)
{
    BandDetector det(44100.0f, ranges);
    auto buf = sine_wave(freq_in);
    det.push_samples(buf.data(), buf.size());
    return det.process()[band_idx].raw_freq;
}

static int g_pass = 0;
static int g_fail = 0;

/// Lightweight assertion helper – keeps going after failures.
#define CHECK(expr, msg)  \
    do {                  \
        if (!(expr)) {    \
            std::fprintf(stderr, "FAIL  %s  (%s)\n", (msg), #expr); \
            ++g_fail;     \
        } else {          \
            std::printf("  %-60s ok\n", (msg)); \
            ++g_pass;     \
        }                 \
    } while(false)

// ─────────────────────────────────────────────────────────────────────────────
// E Standard open strings
// ─────────────────────────────────────────────────────────────────────────────

static void test_standard_strings()
{
    struct Case { float freq; int band; const char* name; };
    constexpr Case cases[] = {
        {  82.41f, 0, "standard_e2_open_string" },
        { 110.00f, 1, "standard_a2_open_string" },
        { 146.83f, 2, "standard_d3_open_string" },
        { 196.00f, 3, "standard_g3_open_string" },
        { 246.94f, 4, "standard_b3_open_string" },
        { 329.63f, 5, "standard_e4_open_string" },
    };
    const char* expected[] = { "E2", "A2", "D3", "G3", "B3", "E4" };

    for (int i = 0; i < 6; ++i) {
        float freq = run_detection(cases[i].freq, STANDARD, cases[i].band);
        std::string note = freq_to_note_display(freq);
        CHECK(std::abs(freq - cases[i].freq) < 2.0f, cases[i].name);
        CHECK(note == expected[i],                     (std::string(cases[i].name) + "_note").c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Fretted notes on the E2 string
// ─────────────────────────────────────────────────────────────────────────────

static void test_fretted_notes()
{
    // 1st fret on E2 string → F2 (87.31 Hz)
    {
        float freq = run_detection(87.31f, STANDARD, 0);
        CHECK(std::abs(freq - 87.31f) < 2.0f,         "standard_first_fret_e2_string_f2");
        CHECK(freq_to_note_display(freq) == "F2",      "standard_first_fret_e2_string_f2_note");
    }
    // 12th fret on E2 string → E3 (164.81 Hz)
    {
        float freq = run_detection(164.81f, STANDARD, 0);
        CHECK(std::abs(freq - 164.81f) < 2.0f,        "standard_twelfth_fret_e2_string_e3");
        CHECK(freq_to_note_display(freq) == "E3",      "standard_twelfth_fret_e2_string_e3_note");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Alternative tunings
// ─────────────────────────────────────────────────────────────────────────────

static void test_alternative_tunings()
{
    // Drop D: low string is D2 (73.42 Hz)
    {
        float freq = run_detection(73.42f, DROP_D, 0);
        CHECK(std::abs(freq - 73.42f) < 2.0f,         "drop_d_low_string");
        CHECK(freq_to_note_display(freq) == "D2",      "drop_d_low_string_note");
    }
    // Drop D: high string (string 1 = index 5) unchanged from Standard
    {
        float freq = run_detection(329.63f, DROP_D, 5);
        CHECK(std::abs(freq - 329.63f) < 2.0f,        "drop_d_high_strings_unchanged");
    }
    // Drop C: low string is C2 (65.41 Hz)
    {
        float freq = run_detection(65.41f, DROP_C, 0);
        CHECK(std::abs(freq - 65.41f) < 2.0f,         "drop_c_low_string_c2");
        CHECK(freq_to_note_display(freq) == "C2",      "drop_c_low_string_c2_note");
    }
    // DADGAD: low string is D2 (73.42 Hz)
    {
        float freq = run_detection(73.42f, DADGAD, 0);
        CHECK(std::abs(freq - 73.42f) < 2.0f,         "dadgad_low_d2");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Em chord tests  (mirrors em_chord_* tests from prior Rust test suite)
// Note-name lookup uses the inline freq_to_note_display() helper above,
// mirroring the GDScript note-name function used in production.
// ─────────────────────────────────────────────────────────────────────────────

static void test_em_chord()
{
    // ── em_chord_three_pitch_classes_detected ─────────────────────────────
    // Feed E2 → band 0, G3 → band 3, B3 → band 4 and confirm all three
    // pitch classes are reported.
    {
        std::set<std::string> classes;
        struct TestCase { float freq; int band; };
        const TestCase cases[] = {
            { 82.41f,  0 },   // E2
            { 196.00f, 3 },   // G3
            { 246.94f, 4 },   // B3
        };
        for (auto& tc : cases) {
            float freq = run_detection(tc.freq, STANDARD, tc.band);
            std::string note = freq_to_note_display(freq);
            if (!note.empty()) {
                // Extract pitch class (letters before the first digit)
                std::size_t pos = 0;
                while (pos < note.size() && !std::isdigit(static_cast<unsigned char>(note[pos]))) ++pos;
                classes.insert(note.substr(0, pos));
            }
        }
        CHECK(classes.count("E") > 0, "em_chord_three_pitch_classes_detected: E");
        CHECK(classes.count("G") > 0, "em_chord_three_pitch_classes_detected: G");
        CHECK(classes.count("B") > 0, "em_chord_three_pitch_classes_detected: B");
    }

    // ── em_chord_open_six_strings_per_band ────────────────────────────────
    // Open Em voicing: E2 B2 E3 G3 B3 E4 through their respective bands.
    {
        struct TestCase { float freq; int band; const char* expected; };
        const TestCase cases[] = {
            {  82.41f, 0, "E2" },
            { 123.47f, 1, "B2" },
            { 164.81f, 2, "E3" },
            { 196.00f, 3, "G3" },
            { 246.94f, 4, "B3" },
            { 329.63f, 5, "E4" },
        };
        for (auto& tc : cases) {
            float freq = run_detection(tc.freq, STANDARD, tc.band);
            std::string note = freq_to_note_display(freq);
            std::string tag = std::string("em_chord_open_six_strings_per_band: ") + tc.expected;
            CHECK(note == tc.expected, tag.c_str());
        }
    }

    // ── em_chord_note_classes_are_exactly_e_g_b ───────────────────────────
    // All 6 bands for the open Em chord → note-class set == {E, G, B}.
    {
        struct TestCase { float freq; int band; };
        const TestCase cases[] = {
            {  82.41f, 0 },
            { 123.47f, 1 },
            { 164.81f, 2 },
            { 196.00f, 3 },
            { 246.94f, 4 },
            { 329.63f, 5 },
        };
        std::set<std::string> pitch_classes;
        for (auto& tc : cases) {
            float freq = run_detection(tc.freq, STANDARD, tc.band);
            std::string note = freq_to_note_display(freq);
            if (!note.empty()) {
                std::size_t pos = 0;
                while (pos < note.size() && !std::isdigit(static_cast<unsigned char>(note[pos]))) ++pos;
                pitch_classes.insert(note.substr(0, pos));
            }
        }
        std::set<std::string> expected_classes = {"E", "G", "B"};
        CHECK(pitch_classes == expected_classes, "em_chord_note_classes_are_exactly_e_g_b");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    std::printf("running tests\n\n");

    test_standard_strings();
    test_fretted_notes();
    test_alternative_tunings();
    test_em_chord();

    std::printf("\ntest result: %s.  %d passed; %d failed\n",
        g_fail == 0 ? "ok" : "FAILED",
        g_pass, g_fail);

    return g_fail == 0 ? 0 : 1;
}
