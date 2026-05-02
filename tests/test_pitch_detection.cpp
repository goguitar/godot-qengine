// test_pitch_detection.cpp – Unit and integration tests for the C++ QEngine.
//
// Tests the same scenarios as the original Rust qengine-core test suite:
//   - Note identification (A4, E2, MIDI round-trip)
//   - All 6 open strings in E Standard tuning
//   - Fretted notes (1st fret and 12th fret on low E)
//   - Drop D / Drop C / DADGAD low strings
//   - Drop D high strings unchanged
//   - Em chord (per-band, exact notes, note-class set)
//
// No Godot dependency – runs as a standalone executable.

#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "note.hpp"
#include "tuning.hpp"
#include "band_detector.hpp"

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

/// Feed one second of a sine at `freq` through band `band_idx` and return
/// the frequency + note string.
static void run_detection(float   freq,
                          TuningId tuning,
                          int     band_idx,
                          float&       out_freq,
                          std::string& out_note)
{
    BandDetector det(44100.0f, tuning);
    auto buf = sine_wave(freq);
    det.push_samples(buf.data(), buf.size());
    auto results = det.process();
    out_freq = results[band_idx].raw_freq;
    if (results[band_idx].note) {
        out_note = results[band_idx].note->display();
    } else {
        out_note.clear();
    }
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
// Note identification
// ─────────────────────────────────────────────────────────────────────────────

static void test_note_identification()
{
    // A4 round-trip
    {
        auto note = DetectedNote::from_frequency(440.0f);
        CHECK(note.has_value(),           "a4_identification: has value");
        CHECK(std::string(note->name) == "A", "a4_identification: name");
        CHECK(note->octave == 4,          "a4_identification: octave");
        CHECK(note->midi   == 69,         "a4_identification: midi");
        CHECK(std::abs(note->cents) < 0.01f, "a4_round_trip: cents near 0");
    }

    // E2 identification
    {
        auto note = DetectedNote::from_frequency(82.41f);
        CHECK(note.has_value(),               "e2_identification: has value");
        CHECK(std::string(note->name) == "E", "e2_identification: name");
        CHECK(note->octave == 2,              "e2_identification: octave");
    }

    // MIDI/freq round-trip for the guitar range
    {
        bool all_ok = true;
        for (int midi = 24; midi <= 96; ++midi) {
            float freq = midi_to_freq(midi);
            auto  back = freq_to_midi(freq);
            if (!back.has_value() || *back != midi) {
                all_ok = false;
                break;
            }
        }
        CHECK(all_ok, "midi_freq_round_trip");
    }
}

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
        float freq; std::string note;
        run_detection(cases[i].freq, TuningId::Standard, cases[i].band, freq, note);
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
        float freq; std::string note;
        run_detection(87.31f, TuningId::Standard, 0, freq, note);
        CHECK(std::abs(freq - 87.31f) < 2.0f, "standard_first_fret_e2_string_f2");
        CHECK(note == "F2",                    "standard_first_fret_e2_string_f2_note");
    }
    // 12th fret on E2 string → E3 (164.81 Hz)
    {
        float freq; std::string note;
        run_detection(164.81f, TuningId::Standard, 0, freq, note);
        CHECK(std::abs(freq - 164.81f) < 2.0f, "standard_twelfth_fret_e2_string_e3");
        CHECK(note == "E3",                     "standard_twelfth_fret_e2_string_e3_note");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Alternative tunings
// ─────────────────────────────────────────────────────────────────────────────

static void test_alternative_tunings()
{
    // Drop D: low string is D2 (73.42 Hz)
    {
        float freq; std::string note;
        run_detection(73.42f, TuningId::DropD, 0, freq, note);
        CHECK(std::abs(freq - 73.42f) < 2.0f, "drop_d_low_string");
        CHECK(note == "D2",                    "drop_d_low_string_note");
    }
    // Drop D: higher strings (1-5) are unchanged from Standard
    {
        float freq; std::string note;
        run_detection(329.63f, TuningId::DropD, 5, freq, note);
        CHECK(std::abs(freq - 329.63f) < 2.0f, "drop_d_high_strings_unchanged");
    }
    // Drop C: low string is C2 (65.41 Hz)
    {
        float freq; std::string note;
        run_detection(65.41f, TuningId::DropC, 0, freq, note);
        CHECK(std::abs(freq - 65.41f) < 2.0f, "drop_c_low_string_c2");
        CHECK(note == "C2",                    "drop_c_low_string_c2_note");
    }
    // DADGAD: low string is D2 (73.42 Hz)
    {
        float freq; std::string note;
        run_detection(73.42f, TuningId::Dadgad, 0, freq, note);
        CHECK(std::abs(freq - 73.42f) < 2.0f, "dadgad_low_d2");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Em chord tests (mirrors em_chord_* tests from Rust qengine-core)
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
            float freq; std::string note;
            run_detection(tc.freq, TuningId::Standard, tc.band, freq, note);
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
            float freq; std::string note;
            run_detection(tc.freq, TuningId::Standard, tc.band, freq, note);
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
            float freq; std::string note;
            run_detection(tc.freq, TuningId::Standard, tc.band, freq, note);
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

    test_note_identification();
    test_standard_strings();
    test_fretted_notes();
    test_alternative_tunings();
    test_em_chord();

    std::printf("\ntest result: %s.  %d passed; %d failed\n",
        g_fail == 0 ? "ok" : "FAILED",
        g_pass, g_fail);

    return g_fail == 0 ? 0 : 1;
}
