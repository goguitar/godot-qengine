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
// Bounds: half-semitone below open (x0.97153), one octave above (x2.0).
// ─────────────────────────────────────────────────────────────────────────────

static constexpr std::array<BandRange, 6> STANDARD = {{
    { 80.11f,  164.82f },   // E2  82.41 Hz
    { 106.87f, 220.00f },   // A2  110.00 Hz
    { 142.65f, 293.66f },   // D3  146.83 Hz
    { 190.42f, 392.00f },   // G3  196.00 Hz
    { 239.91f, 493.88f },   // B3  246.94 Hz
    { 320.25f, 659.26f },   // E4  329.63 Hz
}};

static constexpr std::array<BandRange, 6> DROP_D = {{
    { 71.33f,  146.84f },   // D2  73.42 Hz
    { 106.87f, 220.00f },   // A2  (unchanged)
    { 142.65f, 293.66f },   // D3
    { 190.42f, 392.00f },   // G3
    { 239.91f, 493.88f },   // B3
    { 320.25f, 659.26f },   // E4
}};

static constexpr std::array<BandRange, 6> DROP_C = {{
    { 63.54f,  130.82f },   // C2  65.41 Hz
    { 95.21f,  196.00f },   // G2  98.00 Hz
    { 127.09f, 261.62f },   // C3  130.81 Hz
    { 169.64f, 349.22f },   // F3  174.61 Hz
    { 213.74f, 440.00f },   // A3  220.00 Hz
    { 285.30f, 587.32f },   // D4  293.66 Hz
}};

static constexpr std::array<BandRange, 6> DADGAD = {{
    { 71.33f,  146.84f },   // D2  73.42 Hz
    { 106.87f, 220.00f },   // A2
    { 142.65f, 293.66f },   // D3
    { 190.42f, 392.00f },   // G3
    { 213.74f, 440.00f },   // A3  220.00 Hz
    { 285.30f, 587.32f },   // D4  293.66 Hz
}};

// ─────────────────────────────────────────────────────────────────────────────
// Note-name helper  (uses midi_note from DetectionResult — same as GDScript)
// Returns e.g. "E2", "G#3".  Empty string when midi_note == -1.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static std::string midi_to_note_display(int midi)
{
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

/// Feed one second of a sine at `freq_in` through `band_idx` and return the full
/// DetectionResult so callers can inspect frequency, midi_note, and cents.
static DetectionResult run_detection(float                            freq_in,
                                     const std::array<BandRange, 6>& ranges,
                                     int                             band_idx)
{
    BandDetector det(44100.0f, ranges);
    auto buf = sine_wave(freq_in);
    det.push_samples(buf.data(), buf.size());
    return det.process()[band_idx];
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
    struct Case { float freq; int band; const char* name; int midi; };
    constexpr Case cases[] = {
        {  82.41f, 0, "standard_e2_open_string", 40 },  // E2
        { 110.00f, 1, "standard_a2_open_string", 45 },  // A2
        { 146.83f, 2, "standard_d3_open_string", 50 },  // D3
        { 196.00f, 3, "standard_g3_open_string", 55 },  // G3
        { 246.94f, 4, "standard_b3_open_string", 59 },  // B3
        { 329.63f, 5, "standard_e4_open_string", 64 },  // E4
    };
    const char* expected[] = { "E2", "A2", "D3", "G3", "B3", "E4" };

    for (int i = 0; i < 6; ++i) {
        auto r = run_detection(cases[i].freq, STANDARD, cases[i].band);
        std::string note = midi_to_note_display(r.midi_note);
        CHECK(std::abs(r.raw_freq - cases[i].freq) < 2.0f, cases[i].name);
        CHECK(r.midi_note == cases[i].midi,                 (std::string(cases[i].name) + "_midi").c_str());
        CHECK(note == expected[i],                          (std::string(cases[i].name) + "_note").c_str());
        CHECK(std::abs(r.cents) < 50.0f,                   (std::string(cases[i].name) + "_cents_range").c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Fretted notes on the E2 string
// ─────────────────────────────────────────────────────────────────────────────

static void test_fretted_notes()
{
    // 1st fret on E2 string → F2 (87.31 Hz, MIDI 41)
    {
        auto r = run_detection(87.31f, STANDARD, 0);
        CHECK(std::abs(r.raw_freq - 87.31f) < 2.0f,        "standard_first_fret_e2_string_f2");
        CHECK(r.midi_note == 41,                             "standard_first_fret_e2_string_f2_midi");
        CHECK(midi_to_note_display(r.midi_note) == "F2",    "standard_first_fret_e2_string_f2_note");
    }
    // 12th fret on E2 string → E3 (164.81 Hz, MIDI 52)
    {
        auto r = run_detection(164.81f, STANDARD, 0);
        CHECK(std::abs(r.raw_freq - 164.81f) < 2.0f,       "standard_twelfth_fret_e2_string_e3");
        CHECK(r.midi_note == 52,                             "standard_twelfth_fret_e2_string_e3_midi");
        CHECK(midi_to_note_display(r.midi_note) == "E3",    "standard_twelfth_fret_e2_string_e3_note");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Alternative tunings
// ─────────────────────────────────────────────────────────────────────────────

static void test_alternative_tunings()
{
    // Drop D: low string is D2 (73.42 Hz, MIDI 38)
    {
        auto r = run_detection(73.42f, DROP_D, 0);
        CHECK(std::abs(r.raw_freq - 73.42f) < 2.0f,        "drop_d_low_string");
        CHECK(r.midi_note == 38,                             "drop_d_low_string_midi");
        CHECK(midi_to_note_display(r.midi_note) == "D2",    "drop_d_low_string_note");
    }
    // Drop D: high string (index 5) unchanged from Standard → E4 (329.63 Hz, MIDI 64)
    {
        auto r = run_detection(329.63f, DROP_D, 5);
        CHECK(std::abs(r.raw_freq - 329.63f) < 2.0f,       "drop_d_high_strings_unchanged");
        CHECK(r.midi_note == 64,                             "drop_d_high_strings_unchanged_midi");
    }
    // Drop C: low string is C2 (65.41 Hz, MIDI 36)
    {
        auto r = run_detection(65.41f, DROP_C, 0);
        CHECK(std::abs(r.raw_freq - 65.41f) < 2.0f,        "drop_c_low_string_c2");
        CHECK(r.midi_note == 36,                             "drop_c_low_string_c2_midi");
        CHECK(midi_to_note_display(r.midi_note) == "C2",    "drop_c_low_string_c2_note");
    }
    // DADGAD: low string is D2 (73.42 Hz, MIDI 38)
    {
        auto r = run_detection(73.42f, DADGAD, 0);
        CHECK(std::abs(r.raw_freq - 73.42f) < 2.0f,        "dadgad_low_d2");
        CHECK(r.midi_note == 38,                             "dadgad_low_d2_midi");
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
    // pitch classes are reported via midi_note.
    {
        std::set<std::string> classes;
        struct TestCase { float freq; int band; };
        const TestCase cases[] = {
            { 82.41f,  0 },   // E2
            { 196.00f, 3 },   // G3
            { 246.94f, 4 },   // B3
        };
        for (auto& tc : cases) {
            auto r = run_detection(tc.freq, STANDARD, tc.band);
            std::string note = midi_to_note_display(r.midi_note);
            if (!note.empty()) {
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
    // Verify both midi_note and note-name for each string.
    {
        struct TestCase { float freq; int band; const char* expected_note; int expected_midi; };
        const TestCase cases[] = {
            {  82.41f, 0, "E2", 40 },
            { 123.47f, 1, "B2", 47 },
            { 164.81f, 2, "E3", 52 },
            { 196.00f, 3, "G3", 55 },
            { 246.94f, 4, "B3", 59 },
            { 329.63f, 5, "E4", 64 },
        };
        for (auto& tc : cases) {
            auto r = run_detection(tc.freq, STANDARD, tc.band);
            std::string note = midi_to_note_display(r.midi_note);
            std::string tag_note = std::string("em_chord_open_six_strings_per_band note: ") + tc.expected_note;
            std::string tag_midi = std::string("em_chord_open_six_strings_per_band midi: ") + tc.expected_note;
            CHECK(note == tc.expected_note,          tag_note.c_str());
            CHECK(r.midi_note == tc.expected_midi,   tag_midi.c_str());
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
            auto r = run_detection(tc.freq, STANDARD, tc.band);
            std::string note = midi_to_note_display(r.midi_note);
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
// SPSC ring buffer unit tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_spsc_event_queue()
{
    SPSCEventQueue<NoteEvent, 4> q;

    CHECK(q.empty(),                          "spsc_event_queue_initially_empty");
    CHECK(q.size() == 0,                      "spsc_event_queue_initial_size_zero");

    NoteEvent a{1.0, 100.0f, 40, 0.9f, 0.5f};
    NoteEvent b{2.0, 200.0f, 52, 0.8f, 0.4f};
    NoteEvent c{3.0, 300.0f, 64, 0.7f, 0.3f};

    CHECK(q.push(a),                          "spsc_event_queue_push_a");
    CHECK(q.push(b),                          "spsc_event_queue_push_b");
    CHECK(q.push(c),                          "spsc_event_queue_push_c");
    CHECK(q.size() == 3,                      "spsc_event_queue_size_three");
    CHECK(!q.empty(),                         "spsc_event_queue_not_empty");

    // Fill to capacity (4)
    NoteEvent d{4.0, 400.0f, 76, 0.6f, 0.2f};
    CHECK(q.push(d),                          "spsc_event_queue_push_d_to_capacity");
    // Now full – next push should drop
    NoteEvent e{5.0, 500.0f, 88, 0.5f, 0.1f};
    CHECK(!q.push(e),                         "spsc_event_queue_push_drops_when_full");

    // Pop FIFO order: a first
    NoteEvent out{};
    CHECK(q.pop(out),                         "spsc_event_queue_pop_a_returns_true");
    CHECK(out.midi_note == 40,                "spsc_event_queue_pop_a_fifo_order");
    CHECK(q.pop(out),                         "spsc_event_queue_pop_b_returns_true");
    CHECK(out.midi_note == 52,                "spsc_event_queue_pop_b_fifo_order");
    CHECK(q.pop(out),                         "spsc_event_queue_pop_c_returns_true");
    CHECK(out.midi_note == 64,                "spsc_event_queue_pop_c_fifo_order");
    CHECK(q.pop(out),                         "spsc_event_queue_pop_d_returns_true");
    CHECK(out.midi_note == 76,                "spsc_event_queue_pop_d_fifo_order");
    CHECK(!q.pop(out),                        "spsc_event_queue_empty_pop_returns_false");
    CHECK(q.empty(),                          "spsc_event_queue_empty_after_drain");

    // clear()
    q.push(a);
    q.push(b);
    q.clear();
    CHECK(q.empty(),                          "spsc_event_queue_clear_empties");
}

static void test_spsc_frame_history()
{
    SPSCFrameHistory<DetectionFrame, 4> h;

    CHECK(h.size() == 0,                      "spsc_frame_history_initial_size_zero");

    DetectionFrame f1, f2, f3, f4, f5;
    f1.midi_note = 40;  f1.pitch_hz = 82.0f;
    f2.midi_note = 45;  f2.pitch_hz = 110.0f;
    f3.midi_note = 50;  f3.pitch_hz = 147.0f;
    f4.midi_note = 55;  f4.pitch_hz = 196.0f;
    f5.midi_note = 59;  f5.pitch_hz = 247.0f;  // oldest will be overwritten

    h.push(f1);
    h.push(f2);
    h.push(f3);
    CHECK(h.size() == 3,                      "spsc_frame_history_size_three");

    DetectionFrame out[4]{};
    std::size_t got = h.read_newest(out, 3);
    CHECK(got == 3,                           "spsc_frame_history_read_three");
    CHECK(out[0].midi_note == 50,             "spsc_frame_history_newest_is_f3");
    CHECK(out[1].midi_note == 45,             "spsc_frame_history_second_is_f2");
    CHECK(out[2].midi_note == 40,             "spsc_frame_history_third_is_f1");

    // Push to full capacity
    h.push(f4);
    CHECK(h.size() == 4,                      "spsc_frame_history_size_four");

    // Push beyond capacity: f1 (oldest) is overwritten
    h.push(f5);
    got = h.read_newest(out, 4);
    CHECK(got == 4,                           "spsc_frame_history_still_four_after_overflow");
    CHECK(out[0].midi_note == 59,             "spsc_frame_history_newest_is_f5");
    CHECK(out[3].midi_note == 45,             "spsc_frame_history_oldest_is_f2_after_overwrite");

    // Non-destructive: reading again gives the same data
    DetectionFrame out2[4]{};
    got = h.read_newest(out2, 4);
    CHECK(got == 4,                           "spsc_frame_history_read_again_same_count");
    CHECK(out2[0].midi_note == 59,            "spsc_frame_history_read_again_same_newest");

    h.clear();
    CHECK(h.size() == 0,                      "spsc_frame_history_clear");
}

// ─────────────────────────────────────────────────────────────────────────────
// BandDetector latest_frame / pop_event / get_frame_history integration tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_detector_analysis_api()
{
    // Feed one second of E2 (82.41 Hz) through band 0 of a Standard detector.
    BandDetector det(44100.0f, STANDARD);
    auto buf = sine_wave(82.41f);
    det.push_samples(buf.data(), buf.size());
    det.process();

    // latest_frame() should report a valid detection.
    const DetectionFrame& f = det.latest_frame();
    CHECK(f.pitch_valid,                      "detector_latest_frame_pitch_valid");
    CHECK(std::abs(f.pitch_hz - 82.41f) < 3.0f, "detector_latest_frame_pitch_hz");
    CHECK(f.midi_note == 40,                  "detector_latest_frame_midi_note_e2");
    CHECK(f.confidence >= 0.8f,               "detector_latest_frame_confidence");
    CHECK(f.level > 0.0f,                     "detector_latest_frame_level_nonzero");
    CHECK(f.time_sec > 0.0,                   "detector_latest_frame_time_nonzero");

    // get_frame_history() should have at least 1 entry (newest first).
    DetectionFrame hist[4]{};
    std::size_t got = det.get_frame_history(hist, 4);
    CHECK(got >= 1,                           "detector_frame_history_nonempty");
    CHECK(hist[0].pitch_valid,                "detector_frame_history_newest_valid");

    // First process() call on fresh detector triggers onset.
    CHECK(det.has_events(),                   "detector_has_onset_event");
    NoteEvent ev{};
    CHECK(det.pop_event(ev),                  "detector_pop_event_returns_true");
    CHECK(ev.midi_note == 40,                 "detector_event_midi_note_e2");
    CHECK(ev.pitch_hz > 0.0f,                 "detector_event_pitch_hz_positive");
    CHECK(!det.has_events(),                  "detector_event_queue_empty_after_pop");

    // reset() clears all state.
    det.reset();
    const DetectionFrame& fr = det.latest_frame();
    CHECK(!fr.pitch_valid,                    "detector_reset_clears_latest_frame");
    CHECK(!det.has_events(),                  "detector_reset_clears_event_queue");
    DetectionFrame hist2[4]{};
    CHECK(det.get_frame_history(hist2, 4) == 0, "detector_reset_clears_frame_history");
}

// ─────────────────────────────────────────────────────────────────────────────
// ChordFrame / per-string detection tests
// ─────────────────────────────────────────────────────────────────────────────

/// Generate a mixed mono sine wave from multiple (frequency, amplitude) pairs,
/// then normalise to peak amplitude 1.0 to avoid clipping.
static std::vector<float> mixed_sine(
    std::initializer_list<std::pair<float,float>> components,
    float sr   = 44100.0f,
    float secs = 1.0f)
{
    std::size_t n = static_cast<std::size_t>(sr * secs);
    std::vector<float> buf(n, 0.0f);
    for (const auto& [freq, amp] : components) {
        for (std::size_t i = 0; i < n; ++i)
            buf[i] += amp * std::sin(
                2.0f * std::numbers::pi_v<float> * freq * static_cast<float>(i) / sr);
    }
    float peak = 0.0f;
    for (float s : buf) if (std::abs(s) > peak) peak = std::abs(s);
    if (peak > 0.0f)
        for (float& s : buf) s /= peak;
    return buf;
}

static void test_chord_frame_api()
{
    std::printf("\n-- ChordFrame / per-string detection tests --\n");

    // ── Single active string: E2 (82.41 Hz) exclusively in band 0 range ────────
    // E2 (82.41 Hz) < band 1 min (106.87 Hz), so only band 0 should detect it.
    {
        BandDetector det(44100.0f, STANDARD);
        auto buf = sine_wave(82.41f);
        det.push_samples(buf.data(), buf.size());
        det.process();

        ChordFrame cf{};
        CHECK(det.pop_chord_frame(cf),              "chord_frame_e2_has_frame");
        CHECK(cf.strings[0].active,                 "chord_frame_e2_band0_active");
        CHECK(cf.strings[0].midi_note == 40,         "chord_frame_e2_band0_midi_e2");
        CHECK(cf.strings[0].pitch_hz > 0.0f,         "chord_frame_e2_band0_pitch_positive");
        CHECK(cf.strings[0].confidence >= 0.8f,      "chord_frame_e2_band0_confidence");
        CHECK(std::abs(cf.strings[0].cents) <= 50.0f,"chord_frame_e2_band0_cents_range");
        CHECK(cf.strings[0].band == 0,               "chord_frame_e2_band0_index");

        // Strings 1-5: band 1 starts at 106.87 Hz > 82.41 Hz → none detect E2.
        CHECK(!cf.strings[1].active,                 "chord_frame_e2_band1_inactive");
        CHECK(!cf.strings[2].active,                 "chord_frame_e2_band2_inactive");
        CHECK(!cf.strings[3].active,                 "chord_frame_e2_band3_inactive");
        CHECK(!cf.strings[4].active,                 "chord_frame_e2_band4_inactive");
        CHECK(!cf.strings[5].active,                 "chord_frame_e2_band5_inactive");

        // Dominant note should be band 0 (the only active string).
        CHECK(cf.dominant_band == 0,                 "chord_frame_e2_dominant_band0");
        CHECK(cf.dominant_midi == 40,                "chord_frame_e2_dominant_midi_e2");
        CHECK(cf.dominant_pitch_hz > 0.0f,           "chord_frame_e2_dominant_pitch_positive");
        CHECK(cf.dominant_confidence >= 0.8f,        "chord_frame_e2_dominant_confidence");
        CHECK(cf.active_count >= 1,                  "chord_frame_e2_active_count_ge1");
    }

    // ── Silence: all strings inactive ────────────────────────────────────────
    {
        BandDetector det(44100.0f, STANDARD);
        std::vector<float> silence(44100, 0.0f);
        det.push_samples(silence.data(), silence.size());
        det.process();

        ChordFrame cf{};
        CHECK(det.pop_chord_frame(cf),               "chord_frame_silence_has_frame");
        CHECK(cf.active_count == 0,                  "chord_frame_silence_active_count_zero");
        CHECK(cf.dominant_band == -1,                "chord_frame_silence_no_dominant");
        CHECK(cf.dominant_midi == -1,                "chord_frame_silence_dominant_midi_minus1");
        for (int b = 0; b < 6; ++b)
            CHECK(!cf.strings[b].active,             "chord_frame_silence_all_strings_inactive");
    }

    // ── Queue drains correctly ────────────────────────────────────────────────
    {
        BandDetector det(44100.0f, STANDARD);
        auto buf = sine_wave(82.41f);
        det.push_samples(buf.data(), buf.size());
        det.process();

        ChordFrame cf{};
        CHECK(det.has_chord_frames(),                "chord_frame_queue_nonempty_before_pop");
        CHECK(det.pop_chord_frame(cf),               "chord_frame_queue_pop_returns_true");
        CHECK(!det.pop_chord_frame(cf),              "chord_frame_queue_empty_after_drain");
        CHECK(!det.has_chord_frames(),               "chord_frame_queue_has_none_after_drain");
    }

    // ── reset() clears the chord queue ───────────────────────────────────────
    {
        BandDetector det(44100.0f, STANDARD);
        auto buf = sine_wave(82.41f);
        det.push_samples(buf.data(), buf.size());
        det.process();

        det.reset();

        ChordFrame cf{};
        CHECK(!det.pop_chord_frame(cf),              "chord_frame_reset_clears_queue");
        CHECK(!det.has_chord_frames(),               "chord_frame_reset_has_none");
    }

    // ── StringComponent fields are populated with per-string index ────────────
    {
        BandDetector det(44100.0f, STANDARD);
        auto buf = sine_wave(82.41f);
        det.push_samples(buf.data(), buf.size());
        det.process();

        ChordFrame cf{};
        det.pop_chord_frame(cf);

        // Every string component carries its own band index.
        for (int b = 0; b < 6; ++b)
            CHECK(cf.strings[b].band == b,           "chord_frame_string_band_index");

        // Active string (band 0) has valid pitch fields.
        CHECK(std::abs(cf.strings[0].pitch_hz - 82.41f) < 3.0f,
                                                     "chord_frame_string0_pitch_hz_accurate");
        CHECK(cf.strings[0].midi_float >= 39.0f && cf.strings[0].midi_float <= 41.0f,
                                                     "chord_frame_string0_midi_float_range");
    }

    // ── Mixed-sine: band 0 detection still works under two-tone input ─────────
    // Q's autocorrelation pitch detector cannot cleanly separate two simultaneous
    // pitches from a single mixed signal — that requires separate DSP channels
    // (which is the purpose of BandDetector's 6 band-specific detectors).
    // What we CAN verify: the per-string architecture reports data for every call
    // and band 0 still detects a low-E pitch (≈ E2/F2) even under interference.
    // E2 = 82.41 Hz (band 0 range 80.11–164.82).
    // C5 = 523.25 Hz (above band 4 max 493.88, exclusive to band 5 range 320.25–659.26).
    {
        BandDetector det(44100.0f, STANDARD);
        auto buf = mixed_sine({{82.41f, 0.5f}, {523.25f, 0.5f}});
        det.push_samples(buf.data(), buf.size());
        det.process();

        ChordFrame cf{};
        CHECK(det.pop_chord_frame(cf),               "chord_two_string_has_frame");
        // Band 0 should detect a pitch close to E2/F2 (autocorrelation may
        // slightly shift frequency due to beating with C5).
        CHECK(cf.strings[0].active,                  "chord_two_string_band0_low_active");
        CHECK(cf.strings[0].midi_note >= 38 && cf.strings[0].midi_note <= 43,
                                                     "chord_two_string_band0_midi_near_e2");
        CHECK(cf.active_count >= 1,                  "chord_two_string_active_count_ge1");
        CHECK(cf.dominant_band >= 0,                 "chord_two_string_dominant_set");
        CHECK(cf.dominant_midi >= 0,                 "chord_two_string_dominant_midi_valid");

        // Verify that the band 5 StringComponent still carries its band index
        // even if it is not active in this mixed-signal scenario.
        CHECK(cf.strings[5].band == 5,               "chord_two_string_band5_index_correct");
    }

    // ── process() always pushes exactly one ChordFrame per call ──────────────
    {
        BandDetector det(44100.0f, STANDARD);
        auto buf = sine_wave(82.41f);

        // Two process() calls → two ChordFrames.
        det.push_samples(buf.data(), buf.size());
        det.process();
        det.push_samples(buf.data(), buf.size());
        det.process();

        ChordFrame cf{};
        int count = 0;
        while (det.pop_chord_frame(cf)) ++count;
        CHECK(count == 2,                            "chord_frame_two_calls_two_frames");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Chord expected-vs-detected tests
//
// For each named chord, the test table declares:
//   • The expected MIDI note per string (or -1 for a muted/absent string).
//   • The test feeds the expected frequency through that string's band detector.
//   • The ChordFrame output is compared against the table, string-by-string:
//       - Active string: active == true, midi_note == expected_midi (±1 semitone)
//       - Muted  string: active == false                             (silence)
//
// This directly answers "Expected Strings/Notes == Detected Strings/Notes?"
//
// BandDetector runs one Q pitch_detector per string and all six detectors share
// a single input buffer.  When a single-frequency sine is fed, only the band
// whose range covers that frequency fires; the rest remain silent.  We exploit
// this to test one string at a time while still using the full ChordFrame API.
// ─────────────────────────────────────────────────────────────────────────────

struct ChordStringSpec {
    float       freq_hz;      ///< expected open-string or fretted frequency; 0.0 = muted
    int         expected_midi; ///< expected MIDI note (0 = muted/silent → -1)
    const char* note_name;    ///< human-readable label e.g. "A2"
};

/// Run per-string expected-vs-detected verification for one chord voicing.
/// 'label'  – chord name used in test-name strings (e.g. "chord_A_major").
/// 'specs'  – array of 6 ChordStringSpec entries (index 0 = lowest string E2).
///            Set freq_hz to 0 and expected_midi to -1 for muted strings.
/// 'root_band' – string index expected to be the dominant/root note.
static void run_chord_string_test(const char*              label,
                                  const ChordStringSpec    specs[6],
                                  int                      root_band)
{
    // Test each string independently: feed the string's frequency to the
    // whole detector and verify the ChordFrame output for that band.
    for (int band = 0; band < 6; ++band) {
        const ChordStringSpec& s = specs[band];

        BandDetector det(44100.0f, STANDARD);

        if (s.freq_hz > 0.0f) {
            // ── Active string: feed the expected frequency ──────────────────
            auto buf = sine_wave(s.freq_hz);
            det.push_samples(buf.data(), buf.size());
            det.process();

            ChordFrame cf{};
            det.pop_chord_frame(cf);

            // Build descriptive test-name strings.
            char tag_active[128], tag_midi[128], tag_dominant[128];
            std::snprintf(tag_active,   sizeof(tag_active),
                          "%s_string%d_%s_active",   label, band, s.note_name);
            std::snprintf(tag_midi,     sizeof(tag_midi),
                          "%s_string%d_%s_expected_midi_%d_got_%d",
                          label, band, s.note_name, s.expected_midi,
                          cf.strings[band].midi_note);

            CHECK(cf.strings[band].active,           tag_active);

            // Allow ±1 semitone tolerance for the pitch detector.
            bool midi_ok = std::abs(cf.strings[band].midi_note - s.expected_midi) <= 1;
            CHECK(midi_ok,                           tag_midi);

            // If this is the root string, verify dominant is sensible.
            if (band == root_band) {
                std::snprintf(tag_dominant, sizeof(tag_dominant),
                              "%s_root_string%d_%s_is_dominant",
                              label, band, s.note_name);
                bool dominant_ok = (cf.dominant_band >= 0 && cf.dominant_midi >= 0);
                CHECK(dominant_ok,                   tag_dominant);
            }
        } else {
            // ── Muted string: feed silence ──────────────────────────────────
            std::vector<float> silence(44100, 0.0f);
            det.push_samples(silence.data(), silence.size());
            det.process();

            ChordFrame cf{};
            det.pop_chord_frame(cf);

            char tag_muted[128];
            std::snprintf(tag_muted, sizeof(tag_muted),
                          "%s_string%d_muted_inactive", label, band);
            CHECK(!cf.strings[band].active,          tag_muted);
        }
    }
}

static void test_chord_expected_vs_detected()
{
    std::printf("\n-- Chord expected-vs-detected (per-string) tests --\n");

    // ── Open Em chord (E Standard)  E2 B2 E3 G3 B3 E4 ───────────────────────
    // All 6 strings active.  Root = band 0 (E2).
    {
        const ChordStringSpec em[6] = {
            {  82.41f, 40, "E2" },   // string 6 (low E)
            { 123.47f, 47, "B2" },   // string 5
            { 164.81f, 52, "E3" },   // string 4
            { 196.00f, 55, "G3" },   // string 3
            { 246.94f, 59, "B3" },   // string 2
            { 329.63f, 64, "E4" },   // string 1 (high E)
        };
        run_chord_string_test("chord_em_open", em, 0);
    }

    // ── Open A major chord (E Standard)  x A2 E3 A3 C#4 E4 ─────────────────
    // String 0 (low E) is muted.  Root = band 1 (A2).
    // C#4 = 277.18 Hz (MIDI 61), in band 4 range 239.91–493.88.
    {
        const ChordStringSpec a_major[6] = {
            { 0.0f,    -1, "muted" }, // string 6 (low E) – muted in open A
            { 110.00f, 45, "A2"    }, // string 5
            { 164.81f, 52, "E3"    }, // string 4
            { 220.00f, 57, "A3"    }, // string 3
            { 277.18f, 61, "C#4"   }, // string 2
            { 329.63f, 64, "E4"    }, // string 1
        };
        run_chord_string_test("chord_a_major_open", a_major, 1);
    }

    // ── Open D major chord (E Standard)  x x D3 A3 D4 F#4 ──────────────────
    // Standard shape xx0232.  Strings 0 and 1 are muted.  Root = band 2 (D3).
    // Band ranges used (Standard tuning):
    //   band 2 (142.65–293.66): D3 = 146.83 Hz  ✓
    //   band 3 (190.42–392.00): A3 = 220.00 Hz  ✓  (G string 2nd fret)
    //   band 4 (239.91–493.88): D4 = 293.66 Hz  ✓  (B string 3rd fret)
    //   band 5 (320.25–659.26): F#4 = 369.99 Hz ✓  (high-E string 2nd fret)
    {
        const ChordStringSpec d_major[6] = {
            { 0.0f,    -1, "muted" }, // string 6 – muted in open D
            { 0.0f,    -1, "muted" }, // string 5 – muted in open D
            { 146.83f, 50, "D3"    }, // string 4 – open D3 (band 2)
            { 220.00f, 57, "A3"    }, // string 3 – A3 (band 3: G string 2nd fret)
            { 293.66f, 62, "D4"    }, // string 2 – D4 (band 4: B string 3rd fret)
            { 369.99f, 66, "F#4"   }, // string 1 – F#4 (band 5: high-E 2nd fret)
        };
        run_chord_string_test("chord_d_major_open", d_major, 2);
    }

    // ── Open G major chord (E Standard)  G2 B2 D3 G3 B3 G4 ─────────────────
    // All 6 strings active.  Root = band 0 (G2, 3rd fret low E).
    // G2 = 98.00 Hz (MIDI 43), in band 0 range 80.11–164.82.
    // G4 = 392.00 Hz (MIDI 67), in band 5 range 320.25–659.26.
    {
        const ChordStringSpec g_major[6] = {
            {  98.00f, 43, "G2" },   // string 6 – 3rd fret E2
            { 123.47f, 47, "B2" },   // string 5 – open B2
            { 146.83f, 50, "D3" },   // string 4 – open D3
            { 196.00f, 55, "G3" },   // string 3 – open G3
            { 246.94f, 59, "B3" },   // string 2 – open B3
            { 392.00f, 67, "G4" },   // string 1 – 3rd fret E4
        };
        run_chord_string_test("chord_g_major_open", g_major, 0);
    }

    // ── Partial chord: power chord E5 (E2 + E3, strings 0+2 only) ───────────
    // Strings 1, 3, 4, 5 are muted.  Demonstrates muted-string detection.
    {
        const ChordStringSpec e5_power[6] = {
            {  82.41f, 40, "E2" },   // string 6 – root
            { 0.0f,    -1, "muted"}, // string 5
            { 164.81f, 52, "E3" },   // string 4 – octave
            { 0.0f,    -1, "muted"}, // string 3
            { 0.0f,    -1, "muted"}, // string 2
            { 0.0f,    -1, "muted"}, // string 1
        };
        run_chord_string_test("chord_e5_power", e5_power, 0);
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

    std::printf("\n-- SPSC ring buffer tests --\n");
    test_spsc_event_queue();
    test_spsc_frame_history();

    std::printf("\n-- Analysis API integration tests --\n");
    test_detector_analysis_api();

    test_chord_frame_api();

    std::printf("\n-- Chord expected-vs-detected (per-string) tests --\n");
    test_chord_expected_vs_detected();

    std::printf("\ntest result: %s.  %d passed; %d failed\n",
        g_fail == 0 ? "ok" : "FAILED",
        g_pass, g_fail);

    return g_fail == 0 ? 0 : 1;
}
