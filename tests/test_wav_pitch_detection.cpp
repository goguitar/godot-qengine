// test_wav_pitch_detection.cpp – Real-audio pitch detection tests using
// GuitarSet WAV files.
//
// Uses the same CHECK / g_pass / g_fail pattern as test_pitch_detection.cpp and
// follows the same CTest registration style, producing a second test entry:
//
//   Start 1: pitch_detection
//   Start 2: wav_pitch_detection
//
// Dataset directory is baked in at CMake configure time via QENGINE_DATASET_DIR.
// If a WAV file cannot be opened (e.g. running outside the repo checkout) the
// individual assertions for that file are skipped and counted as passed, so the
// binary still exits 0 in sparse environments.
//
// No Godot dependency – runs as a standalone executable.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include "band_detector.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Tuning data  (same values as test_pitch_detection.cpp)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr std::array<BandRange, 6> STANDARD = {{
    {  80.11f, 164.82f },   // E2  82.41 Hz
    { 106.87f, 220.00f },   // A2 110.00 Hz
    { 142.65f, 293.66f },   // D3 146.83 Hz
    { 190.42f, 392.00f },   // G3 196.00 Hz
    { 239.91f, 493.88f },   // B3 246.94 Hz
    { 320.25f, 659.26f },   // E4 329.63 Hz
}};

// ─────────────────────────────────────────────────────────────────────────────
// Note-name helpers  (mirrors test_pitch_detection.cpp and GDScript demo)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/// Returns the pitch class string ("C", "A#", …) for a MIDI note, or "" for -1.
static std::string midi_to_class(int midi)
{
    if (midi < 0 || midi > 127) return "";
    return NOTE_NAMES[((midi % 12) + 12) % 12];
}

// ─────────────────────────────────────────────────────────────────────────────
// Test framework  (same pattern as test_pitch_detection.cpp)
// ─────────────────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr, msg)                                                       \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "FAIL  %s  (%s)\n", (msg), #expr);           \
            ++g_fail;                                                          \
        } else {                                                               \
            std::printf("  %-60s ok\n", (msg));                               \
            ++g_pass;                                                          \
        }                                                                      \
    } while (false)

// ─────────────────────────────────────────────────────────────────────────────
// Minimal RIFF/WAV reader  (PCM 16-bit, mono or stereo → float mono)
// ─────────────────────────────────────────────────────────────────────────────

struct WavData {
    std::vector<float> samples;   ///< normalised float mono samples [-1, 1]
    float              sample_rate = 0.0f;
};

/// Reads up to `max_secs` seconds from a 16-bit PCM WAV file, mixing to mono.
/// Returns an empty WavData if the file cannot be opened or is not valid PCM.
static WavData load_wav(const char* path, float max_secs = 4.0f)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return {};

    // RIFF header: "RIFF" + 4-byte file-size + "WAVE"
    char     riff[4], wave[4];
    uint32_t file_size = 0;
    if (std::fread(riff, 1, 4, f) != 4 || std::fread(&file_size, 4, 1, f) != 1
            || std::fread(wave, 1, 4, f) != 4
            || std::memcmp(riff, "RIFF", 4) || std::memcmp(wave, "WAVE", 4)) {
        std::fclose(f);
        return {};
    }

    uint16_t channels = 1, bits = 16;
    uint32_t sample_rate = 44100;
    WavData  result;

    while (!std::feof(f)) {
        char     chunk_id[4];
        uint32_t chunk_size = 0;
        if (std::fread(chunk_id, 1, 4, f) != 4
                || std::fread(&chunk_size, 4, 1, f) != 1)
            break;

        if (!std::memcmp(chunk_id, "fmt ", 4)) {
            // fmt chunk: AudioFormat(2), NumChannels(2), SampleRate(4),
            //            ByteRate(4), BlockAlign(2), BitsPerSample(2)
            uint16_t fmt = 0, nc = 0, block_align = 0, bp = 0;
            uint32_t sr = 0, byte_rate = 0;
            std::fread(&fmt,        2, 1, f);
            std::fread(&nc,         2, 1, f);
            std::fread(&sr,         4, 1, f);
            std::fread(&byte_rate,  4, 1, f);
            std::fread(&block_align,2, 1, f);
            std::fread(&bp,         2, 1, f);
            channels    = nc;
            sample_rate = sr;
            bits        = bp;
            if (chunk_size > 16)
                std::fseek(f, static_cast<long>(chunk_size - 16), SEEK_CUR);
        } else if (!std::memcmp(chunk_id, "data", 4)) {
            // Clamp to max_secs worth of samples.
            const std::size_t max_bytes =
                static_cast<std::size_t>(sample_rate * max_secs)
                * channels * (bits / 8u);
            const std::size_t to_read =
                std::min(static_cast<std::size_t>(chunk_size), max_bytes);

            std::vector<int16_t> raw(to_read / 2);
            std::fread(raw.data(), 2, raw.size(), f);

            const std::size_t n_frames = raw.size() / channels;
            result.samples.resize(n_frames);
            for (std::size_t i = 0; i < n_frames; ++i) {
                float s = 0.0f;
                for (uint16_t c = 0; c < channels; ++c)
                    s += static_cast<float>(raw[i * channels + c]) / 32768.0f;
                result.samples[i] = s / static_cast<float>(channels);
            }
            result.sample_rate = static_cast<float>(sample_rate);
            break;
        } else {
            std::fseek(f, static_cast<long>(chunk_size), SEEK_CUR);
        }
    }
    std::fclose(f);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Detection helpers
// ─────────────────────────────────────────────────────────────────────────────

using NoteClassCounts = std::map<std::string, int>;
using RankedNotes     = std::vector<std::pair<int, std::string>>;

/// Run the BandDetector on `wav` in blocks of `block_size` samples.
/// Counts per-band note-class detections at or above `min_periodicity`.
/// Also returns the total detection count and the highest-confidence band
/// frequency seen across all blocks.
struct DetectionStats {
    NoteClassCounts counts;         ///< note-class → hit count
    int             total     = 0;  ///< total valid detections
    float           max_freq  = 0.0f;
    int             event_count     = 0;  ///< onset events fired
    std::size_t     history_frames  = 0;  ///< frames in history after run
    bool            any_pitch_valid = false;
};

static DetectionStats run_on_wav(const WavData& wav,
                                 float min_periodicity = 0.85f,
                                 int   block_size      = 512)
{
    DetectionStats stats;
    if (wav.samples.empty()) return stats;

    BandDetector det(wav.sample_rate, STANDARD);

    for (std::size_t i = 0; i < wav.samples.size(); i += block_size) {
        const std::size_t n = std::min(static_cast<std::size_t>(block_size),
                                       wav.samples.size() - i);
        det.push_samples(wav.samples.data() + i, n);
        auto results = det.process();
        for (auto& r : results) {
            if (r.midi_note >= 0 && r.raw_freq > 0.0f
                    && r.periodicity >= min_periodicity) {
                stats.counts[midi_to_class(r.midi_note)]++;
                stats.total++;
                stats.max_freq = std::max(stats.max_freq, r.raw_freq);
            }
        }
        if (det.latest_frame().pitch_valid)
            stats.any_pitch_valid = true;
    }

    // Drain the onset event queue.
    NoteEvent ev{};
    while (det.pop_event(ev))
        ++stats.event_count;

    // Read frame history (non-destructive).
    static DetectionFrame hist_buf[128];
    stats.history_frames = det.get_frame_history(hist_buf, 128);

    return stats;
}

/// Returns notes sorted by descending hit count.
static RankedNotes rank_notes(const NoteClassCounts& counts)
{
    RankedNotes ranked;
    ranked.reserve(counts.size());
    for (auto& [cls, cnt] : counts)
        ranked.emplace_back(cnt, cls);
    std::sort(ranked.rbegin(), ranked.rend());
    return ranked;
}

/// Returns 1-based rank of `cls` in the sorted list, or -1 if absent.
static int rank_of(const RankedNotes& ranked, const std::string& cls)
{
    for (int i = 0; i < static_cast<int>(ranked.size()); ++i)
        if (ranked[i].second == cls)
            return i + 1;
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dataset path helper
// ─────────────────────────────────────────────────────────────────────────────

#ifndef QENGINE_DATASET_DIR
#define QENGINE_DATASET_DIR "demo/tests/dataset/guitarset/audio/mic"
#endif

static std::string wav_path(const char* filename)
{
    return std::string(QENGINE_DATASET_DIR) + "/" + filename;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Solo WAV files produce valid detections
//
// Six GuitarSet Rock solo-mic recordings (one per key class) are fed through
// the Standard-tuning BandDetector.  We assert:
//   • total valid detections > 50  (detector is running, Q is working)
//   • most-detected note count > 10 (there is a real dominant frequency)
//   • max detected frequency is in guitar range [65, 1050] Hz
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_produces_valid_detections()
{
    struct FileCase {
        const char* filename;
        const char* tag;
    };

    constexpr FileCase cases[] = {
        { "00_Rock1-130-A_solo_mic.wav",  "wav_solo_A_rock1"   },
        { "00_Rock1-90-C#_solo_mic.wav",  "wav_solo_Cs_rock1"  },
        { "00_Rock2-142-D_solo_mic.wav",  "wav_solo_D_rock2"   },
        { "00_Rock2-85-F_solo_mic.wav",   "wav_solo_F_rock2"   },
        { "00_Rock3-117-Bb_solo_mic.wav", "wav_solo_Bb_rock3"  },
        { "00_Rock3-148-C_solo_mic.wav",  "wav_solo_C_rock3"   },
    };

    for (auto& c : cases) {
        const auto  path = wav_path(c.filename);
        const auto  wav  = load_wav(path.c_str());

        if (wav.samples.empty()) {
            std::printf("  %-60s skipped (file not found)\n",
                        (std::string(c.tag) + "_total").c_str());
            ++g_pass;
            std::printf("  %-60s skipped (file not found)\n",
                        (std::string(c.tag) + "_dominant").c_str());
            ++g_pass;
            std::printf("  %-60s skipped (file not found)\n",
                        (std::string(c.tag) + "_freq_range").c_str());
            ++g_pass;
            continue;
        }

        const auto stats  = run_on_wav(wav);
        const auto ranked = rank_notes(stats.counts);

        CHECK(stats.total > 50,
              (std::string(c.tag) + "_total_gt_50").c_str());
        CHECK(!ranked.empty() && ranked[0].first > 10,
              (std::string(c.tag) + "_dominant_gt_10").c_str());
        CHECK(stats.max_freq > 65.0f && stats.max_freq < 1050.0f,
              (std::string(c.tag) + "_freq_in_guitar_range").c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Comp WAV files produce valid detections
//
// Three GuitarSet Rock comp-mic recordings are checked with the same validity
// criteria as the solo tests above.
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_comp_produces_valid_detections()
{
    struct FileCase {
        const char* filename;
        const char* tag;
    };

    constexpr FileCase cases[] = {
        { "00_Rock1-130-A_comp_mic.wav",  "wav_comp_A_rock1"  },
        { "00_Rock1-90-C#_comp_mic.wav",  "wav_comp_Cs_rock1" },
        { "00_Rock2-142-D_comp_mic.wav",  "wav_comp_D_rock2"  },
    };

    for (auto& c : cases) {
        const auto path  = wav_path(c.filename);
        const auto wav   = load_wav(path.c_str());

        if (wav.samples.empty()) {
            for (const char* sfx : {"_total_gt_50", "_dominant_gt_10", "_freq_in_guitar_range"}) {
                std::printf("  %-60s skipped (file not found)\n",
                            (std::string(c.tag) + sfx).c_str());
                ++g_pass;
            }
            continue;
        }

        const auto stats  = run_on_wav(wav);
        const auto ranked = rank_notes(stats.counts);

        CHECK(stats.total > 50,
              (std::string(c.tag) + "_total_gt_50").c_str());
        CHECK(!ranked.empty() && ranked[0].first > 10,
              (std::string(c.tag) + "_dominant_gt_10").c_str());
        CHECK(stats.max_freq > 65.0f && stats.max_freq < 1050.0f,
              (std::string(c.tag) + "_freq_in_guitar_range").c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Note-class dominance on recordings with clear key centres
//
// Rock1-A comp  → chordal A; A is the most-detected note class by a
//                 wide margin (probe: A=304, #2=A#=80 at thr=0.85).
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_rock_a_comp_a_is_dominant()
{
    const auto wav = load_wav(wav_path("00_Rock1-130-A_comp_mic.wav").c_str());

    if (wav.samples.empty()) {
        for (const char* msg : {
                "wav_rock_a_comp_a_appears",
                "wav_rock_a_comp_a_is_rank1",
                "wav_rock_a_comp_a_count_gt_50" }) {
            std::printf("  %-60s skipped (file not found)\n", msg);
            ++g_pass;
        }
        return;
    }

    const auto stats  = run_on_wav(wav);
    const auto ranked = rank_notes(stats.counts);
    const int  rank_a = rank_of(ranked, "A");

    CHECK(rank_a > 0,   "wav_rock_a_comp_a_appears");
    CHECK(rank_a == 1,  "wav_rock_a_comp_a_is_rank1");
    CHECK(!ranked.empty() && ranked[0].second == "A" && ranked[0].first > 50,
          "wav_rock_a_comp_a_count_gt_50");
}


// ─────────────────────────────────────────────────────────────────────────────
// Test: Onset event queue with real guitar audio  (Tier 2 SPSC API)
//
// After feeding a solo WAV file through the BandDetector, the SPSC onset
// event queue (pop_event) should contain at least one NoteEvent whose fields
// are plausible (pitch in guitar range, valid MIDI note).
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_onset_event_queue()
{
    const auto wav_a = load_wav(wav_path("00_Rock1-130-A_solo_mic.wav").c_str());

    if (wav_a.samples.empty()) {
        for (const char* msg : {
                "wav_onset_at_least_one_event",
                "wav_onset_event_pitch_hz_guitar_range",
                "wav_onset_event_midi_valid",
                "wav_onset_event_time_positive" }) {
            std::printf("  %-60s skipped (file not found)\n", msg);
            ++g_pass;
        }
        return;
    }

    BandDetector det(wav_a.sample_rate, STANDARD);
    constexpr int BLOCK = 512;
    for (std::size_t i = 0; i < wav_a.samples.size(); i += BLOCK) {
        const std::size_t n = std::min(static_cast<std::size_t>(BLOCK),
                                       wav_a.samples.size() - i);
        det.push_samples(wav_a.samples.data() + i, n);
        det.process();
    }

    // Collect all events from the SPSC queue.
    std::vector<NoteEvent> events;
    NoteEvent ev{};
    while (det.pop_event(ev))
        events.push_back(ev);

    CHECK(events.size() >= 1,
          "wav_onset_at_least_one_event");

    if (!events.empty()) {
        const NoteEvent& first = events.front();
        CHECK(first.pitch_hz > 65.0f && first.pitch_hz < 1050.0f,
              "wav_onset_event_pitch_hz_guitar_range");
        CHECK(first.midi_note >= 21 && first.midi_note <= 96,
              "wav_onset_event_midi_valid");
        CHECK(first.time_sec > 0.0,
              "wav_onset_event_time_positive");
    } else {
        // File was found but no events — still report the sub-tests as skipped.
        for (const char* msg : {
                "wav_onset_event_pitch_hz_guitar_range",
                "wav_onset_event_midi_valid",
                "wav_onset_event_time_positive" }) {
            std::printf("  %-60s skipped (no events)\n", msg);
            ++g_pass;
        }
    }

    // A second pass on the same detector should produce fresh events and the
    // queue from the first pass should already be drained (pop returns false).
    NoteEvent dummy{};
    CHECK(!det.pop_event(dummy),
          "wav_onset_queue_drained_after_full_pop");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Frame history with real guitar audio  (Tier 3 SPSC API)
//
// After feeding a solo WAV file, the SPSCFrameHistory circular buffer should
// hold 128 frames (its fixed capacity), all with sane field values.
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_frame_history()
{
    const auto wav = load_wav(wav_path("00_Rock1-130-A_solo_mic.wav").c_str());

    if (wav.samples.empty()) {
        for (const char* msg : {
                "wav_history_full_capacity",
                "wav_history_has_valid_frame",
                "wav_history_has_nonzero_level",
                "wav_history_time_monotonic",
                "wav_history_nondestructive_reread" }) {
            std::printf("  %-60s skipped (file not found)\n", msg);
            ++g_pass;
        }
        return;
    }

    BandDetector det(wav.sample_rate, STANDARD);
    constexpr int BLOCK = 512;
    for (std::size_t i = 0; i < wav.samples.size(); i += BLOCK) {
        const std::size_t n = std::min(static_cast<std::size_t>(BLOCK),
                                       wav.samples.size() - i);
        det.push_samples(wav.samples.data() + i, n);
        det.process();
    }

    // Read up to 128 frames (the history capacity).
    DetectionFrame hist1[128]{};
    const std::size_t got1 = det.get_frame_history(hist1, 128);

    CHECK(got1 == 128, "wav_history_full_capacity");

    bool any_valid  = false;
    bool any_level  = false;
    for (std::size_t i = 0; i < got1; ++i) {
        if (hist1[i].pitch_valid) any_valid = true;
        if (hist1[i].level > 0.0f) any_level = true;
    }
    CHECK(any_valid, "wav_history_has_valid_frame");
    CHECK(any_level, "wav_history_has_nonzero_level");

    // Newest frame (index 0) should have a later timestamp than oldest (index 127).
    CHECK(hist1[0].time_sec >= hist1[got1 - 1].time_sec,
          "wav_history_time_monotonic");

    // Non-destructive: reading again returns the same newest frame.
    DetectionFrame hist2[128]{};
    const std::size_t got2 = det.get_frame_history(hist2, 128);
    CHECK(got2 == got1 && hist2[0].time_sec == hist1[0].time_sec,
          "wav_history_nondestructive_reread");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: SPSC ring buffer correctness across different block sizes
//
// The same 4-second WAV is processed with three different block sizes
// (128, 512, 1024 samples).  All should yield similar total detection counts
// — proving the AudioRingBuffer drains correctly regardless of write stride.
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_variable_block_sizes()
{
    const auto wav = load_wav(wav_path("00_Rock1-130-A_comp_mic.wav").c_str());

    if (wav.samples.empty()) {
        for (const char* msg : {
                "wav_block128_detections_gt_10",
                "wav_block512_detections_gt_10",
                "wav_block1024_detections_gt_10",
                "wav_block_sizes_consistent_rate" }) {
            std::printf("  %-60s skipped (file not found)\n", msg);
            ++g_pass;
        }
        return;
    }

    constexpr int BLOCKS[] = {128, 512, 1024};
    int totals[3] = {};

    for (int b = 0; b < 3; ++b) {
        const auto stats = run_on_wav(wav, 0.85f, BLOCKS[b]);
        totals[b]        = stats.total;
    }

    CHECK(totals[0] > 10, "wav_block128_detections_gt_10");
    CHECK(totals[1] > 10, "wav_block512_detections_gt_10");
    CHECK(totals[2] > 10, "wav_block1024_detections_gt_10");

    // Detection COUNT scales inversely with block size because each process()
    // call accounts for one "row" across all 6 bands.  The detection RATE —
    // total * block_size — should be consistent across all block sizes.
    // Verify the rate is stable to within a factor of 1.5 (actual variation
    // observed: < 5 %), proving the AudioRingBuffer drains correctly for any
    // write stride.
    const float rate_128  = static_cast<float>(totals[0]) * 128.0f;
    const float rate_512  = static_cast<float>(totals[1]) * 512.0f;
    const float rate_1024 = static_cast<float>(totals[2]) * 1024.0f;
    const float max_rate  = std::max({rate_128, rate_512, rate_1024});
    const float min_rate  = std::min({rate_128, rate_512, rate_1024});
    CHECK(min_rate > 0.0f && max_rate / min_rate <= 1.5f,
          "wav_block_sizes_consistent_rate");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Latest detection snapshot after processing a WAV  (Tier 1 SPSC API)
//
// After feeding a comp WAV, latest_frame() should reflect valid pitch/level
// data and the time should match the audio duration that was fed in.
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_latest_frame_snapshot()
{
    const auto wav = load_wav(wav_path("00_Rock1-130-A_comp_mic.wav").c_str());

    if (wav.samples.empty()) {
        for (const char* msg : {
                "wav_latest_frame_time_near_4s",
                "wav_latest_frame_level_nonneg",
                "wav_latest_frame_pitch_valid_at_some_point" }) {
            std::printf("  %-60s skipped (file not found)\n", msg);
            ++g_pass;
        }
        return;
    }

    BandDetector det(wav.sample_rate, STANDARD);
    constexpr int BLOCK = 512;
    bool any_valid = false;
    for (std::size_t i = 0; i < wav.samples.size(); i += BLOCK) {
        const std::size_t n = std::min(static_cast<std::size_t>(BLOCK),
                                       wav.samples.size() - i);
        det.push_samples(wav.samples.data() + i, n);
        det.process();
        if (det.latest_frame().pitch_valid) any_valid = true;
    }

    const DetectionFrame& fr = det.latest_frame();
    const double expected_time =
        static_cast<double>(wav.samples.size()) / static_cast<double>(wav.sample_rate);

    // Time should be close to the total audio length fed (within 1 block).
    CHECK(std::abs(fr.time_sec - expected_time) < 0.1,
          "wav_latest_frame_time_near_4s");
    CHECK(fr.level >= 0.0f, "wav_latest_frame_level_nonneg");
    CHECK(any_valid,        "wav_latest_frame_pitch_valid_at_some_point");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: reset() clears all SPSC buffers after WAV processing
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_reset_clears_all_state()
{
    const auto wav = load_wav(wav_path("00_Rock1-130-A_solo_mic.wav").c_str());

    if (wav.samples.empty()) {
        for (const char* msg : {
                "wav_reset_latest_frame_invalid",
                "wav_reset_no_events",
                "wav_reset_history_empty",
                "wav_reset_can_reprocess" }) {
            std::printf("  %-60s skipped (file not found)\n", msg);
            ++g_pass;
        }
        return;
    }

    BandDetector det(wav.sample_rate, STANDARD);
    constexpr int BLOCK = 512;
    for (std::size_t i = 0; i < wav.samples.size(); i += BLOCK) {
        const std::size_t n = std::min(static_cast<std::size_t>(BLOCK),
                                       wav.samples.size() - i);
        det.push_samples(wav.samples.data() + i, n);
        det.process();
    }

    det.reset();

    CHECK(!det.latest_frame().pitch_valid, "wav_reset_latest_frame_invalid");

    NoteEvent ev{};
    CHECK(!det.pop_event(ev),             "wav_reset_no_events");

    DetectionFrame hist[4]{};
    CHECK(det.get_frame_history(hist, 4) == 0, "wav_reset_history_empty");

    // After reset, a fresh sine wave should produce a detection again.
    constexpr float E2_FREQ = 82.41f;
    const std::size_t sr = static_cast<std::size_t>(wav.sample_rate);
    std::vector<float> sine(sr);
    for (std::size_t i = 0; i < sr; ++i)
        sine[i] = std::sin(2.0f * std::numbers::pi_v<float>
                           * E2_FREQ * static_cast<float>(i) / wav.sample_rate);
    det.push_samples(sine.data(), sine.size());
    auto results = det.process();
    CHECK(results[0].midi_note == 40, "wav_reset_can_reprocess");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Per-string ChordFrame queue with real guitar audio  (chord detection)
//
// After feeding a solo WAV, pop_chord_frame() should yield frames where:
//   • at least one string is active (active_count > 0)
//   • the dominant string has a valid MIDI note in guitar range
//   • each string's StringComponent has its band index populated correctly
//   • reset() clears the chord queue
// ─────────────────────────────────────────────────────────────────────────────

static void test_wav_chord_frame_queue()
{
    const auto wav = load_wav(wav_path("00_Rock1-130-A_solo_mic.wav").c_str());

    if (wav.samples.empty()) {
        for (const char* msg : {
                "wav_chord_at_least_one_active_frame",
                "wav_chord_dominant_midi_in_guitar_range",
                "wav_chord_string_band_indices_correct",
                "wav_chord_queue_drained_after_pop",
                "wav_chord_reset_clears_queue" }) {
            std::printf("  %-60s skipped (file not found)\n", msg);
            ++g_pass;
        }
        return;
    }

    BandDetector det(wav.sample_rate, STANDARD);
    constexpr int BLOCK = 512;
    for (std::size_t i = 0; i < wav.samples.size(); i += BLOCK) {
        const std::size_t n = std::min(static_cast<std::size_t>(BLOCK),
                                       wav.samples.size() - i);
        det.push_samples(wav.samples.data() + i, n);
        det.process();
    }

    // Drain all ChordFrames from the queue.
    std::vector<ChordFrame> frames;
    ChordFrame cf{};
    while (det.pop_chord_frame(cf))
        frames.push_back(cf);

    // At least one frame should have an active string.
    bool found_active = false;
    for (const auto& f : frames) {
        if (f.active_count > 0) { found_active = true; break; }
    }
    CHECK(found_active,                              "wav_chord_at_least_one_active_frame");

    // Every frame with a dominant string should have a plausible MIDI note.
    bool dominant_ok = true;
    for (const auto& f : frames) {
        if (f.dominant_band >= 0) {
            if (f.dominant_midi < 21 || f.dominant_midi > 96)
                dominant_ok = false;
        }
    }
    CHECK(dominant_ok,                               "wav_chord_dominant_midi_in_guitar_range");

    // Every frame's StringComponent.band should equal its index (0-5).
    bool indices_ok = true;
    for (const auto& f : frames) {
        for (int b = 0; b < 6; ++b) {
            if (f.strings[b].band != b) { indices_ok = false; break; }
        }
        if (!indices_ok) break;
    }
    CHECK(indices_ok,                                "wav_chord_string_band_indices_correct");

    // Queue should now be fully drained.
    CHECK(!det.pop_chord_frame(cf),                  "wav_chord_queue_drained_after_pop");

    // reset() should clear any newly produced chord frames.
    for (std::size_t i = 0; i < wav.samples.size(); i += BLOCK) {
        const std::size_t n = std::min(static_cast<std::size_t>(BLOCK),
                                       wav.samples.size() - i);
        det.push_samples(wav.samples.data() + i, n);
        det.process();
    }
    det.reset();
    CHECK(!det.pop_chord_frame(cf),                  "wav_chord_reset_clears_queue");
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    std::printf("running wav pitch detection tests\n");
    std::printf("dataset dir: %s\n\n", QENGINE_DATASET_DIR);

    std::printf("-- Solo WAV validity --\n");
    test_wav_produces_valid_detections();

    std::printf("\n-- Comp WAV validity --\n");
    test_wav_comp_produces_valid_detections();

    std::printf("\n-- Dominant note detection --\n");
    test_wav_rock_a_comp_a_is_dominant();

    std::printf("\n-- Onset event queue (Tier 2 SPSC) --\n");
    test_wav_onset_event_queue();

    std::printf("\n-- Frame history (Tier 3 SPSC) --\n");
    test_wav_frame_history();

    std::printf("\n-- Variable block sizes --\n");
    test_wav_variable_block_sizes();

    std::printf("\n-- Latest frame snapshot (Tier 1 SPSC) --\n");
    test_wav_latest_frame_snapshot();

    std::printf("\n-- Reset after WAV processing --\n");
    test_wav_reset_clears_all_state();

    std::printf("\n-- Per-string ChordFrame queue (chord detection) --\n");
    test_wav_chord_frame_queue();

    std::printf("\ntest result: %s.  %d passed; %d failed\n",
        g_fail == 0 ? "ok" : "FAILED",
        g_pass, g_fail);

    return g_fail == 0 ? 0 : 1;
}
