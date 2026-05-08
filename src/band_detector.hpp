// band_detector.hpp – Multi-band guitar pitch detector.
//
// BandDetector wraps six cycfi/Q pitch_detector instances (one per guitar
// string) and buffers incoming audio samples in a lock-free SPSC ring buffer
// until process() is called on the consumer thread.
//
// New in architecture v2: BandDetector also maintains a per-call detection
// snapshot (DetectionFrame), a fixed-size onset-event queue (NoteEvent), and
// a rolling frame-history buffer.  GDScript consumes these to implement
// chart-guided gameplay judgment separate from the audio analysis.
//
// All three data paths (audio samples, note events, frame history) use the
// same lock-free SPSC ring-buffer pattern with atomic head/tail indices, so
// the producer (analysis) and consumer (GDScript) can safely run on different
// threads.
//
// No Godot dependency – usable from tests without a Godot installation.
// Tuning data (per-band frequency ranges) and note-name mapping are the
// responsibility of the caller (e.g. GDScript in the Godot layer).

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

// Forward-declare the Q type to avoid pulling in heavy Q templates in the header.
namespace cycfi { namespace q { class pitch_detector; } }

// ─────────────────────────────────────────────────────────────────────────────
// BandRange – frequency bounds for one pitch-detector band
// ─────────────────────────────────────────────────────────────────────────────

struct BandRange {
    float freq_min; ///< lower frequency bound (Hz)
    float freq_max; ///< upper frequency bound (Hz)
};

// ─────────────────────────────────────────────────────────────────────────────
// DetectionResult – result for one string band (backwards-compatible)
// ─────────────────────────────────────────────────────────────────────────────

struct DetectionResult {
    int   band;        ///< 0 = lowest string
    float raw_freq;    ///< detected Hz (0 if none)
    float periodicity; ///< Q confidence [0, 1]
    int   midi_note;   ///< nearest MIDI note [0, 127]; -1 if no detection
    float cents;       ///< deviation from nearest semitone in cents [-50, +50]
};

// ─────────────────────────────────────────────────────────────────────────────
// DetectionFrame – real-time analysis snapshot for one process() call.
//
// Aggregates the "best" active band result across all 6 string bands.
// GDScript reads this for the tuner UI and for chart-guided judgment.
// ─────────────────────────────────────────────────────────────────────────────

struct DetectionFrame {
    double time_sec    = 0.0;   ///< elapsed audio time in seconds
    float  pitch_hz    = 0.0f;  ///< detected frequency (0 if none)
    float  midi_float  = -1.0f; ///< fractional MIDI (e.g. 57.35); -1 if none
    int    midi_note   = -1;    ///< nearest integer MIDI [0,127]; -1 if none
    float  confidence  = 0.0f;  ///< Q periodicity [0,1]
    float  level       = 0.0f;  ///< RMS signal level [0,1] for this block
    bool   onset       = false; ///< true on the frame where a new note attack was detected
    bool   pitch_valid = false; ///< true when pitch_hz and midi_note are reliable
};

// ─────────────────────────────────────────────────────────────────────────────
// NoteEvent – fired once per detected onset.
//
// Collected in a fixed-size SPSC ring; GDScript pops these each frame and
// compares them to chart notes inside the current hit window.
// ─────────────────────────────────────────────────────────────────────────────

struct NoteEvent {
    double time_sec   = 0.0;   ///< audio time of the onset
    float  pitch_hz   = 0.0f;  ///< detected frequency at onset
    int    midi_note  = -1;    ///< nearest integer MIDI at onset
    float  confidence = 0.0f;  ///< Q periodicity at onset
    float  level      = 0.0f;  ///< RMS level at onset
};

// ─────────────────────────────────────────────────────────────────────────────
// AudioRingBuffer – lock-free single-producer / single-consumer circular buffer
//                   for float audio samples.
//
// CAPACITY must be a power of 2.
// Producer thread: push() | Consumer thread: drain(), clear(), available()
// ─────────────────────────────────────────────────────────────────────────────

template<std::size_t CAPACITY>
struct AudioRingBuffer {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
                  "AudioRingBuffer CAPACITY must be a power of 2");
    static constexpr std::size_t MASK = CAPACITY - 1;

    AudioRingBuffer()                                      = default;
    AudioRingBuffer(const AudioRingBuffer&)                = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&)     = delete;
    AudioRingBuffer(AudioRingBuffer&&)                     = delete;
    AudioRingBuffer& operator=(AudioRingBuffer&&)          = delete;

    /// Producer: write up to count samples; silently drops samples that would
    /// overflow the buffer.  Returns the number of samples actually written.
    std::size_t push(const float* src, std::size_t count) noexcept
    {
        const std::size_t head   = _head.load(std::memory_order_relaxed);
        const std::size_t tail   = _tail.load(std::memory_order_acquire);
        const std::size_t free_n = CAPACITY - (head - tail);
        const std::size_t n      = std::min(count, free_n);
        for (std::size_t i = 0; i < n; ++i)
            _data[(head + i) & MASK] = src[i];
        _head.store(head + n, std::memory_order_release);
        return n;
    }

    /// Consumer: call fn(float) for every available sample in FIFO order,
    /// then advance the tail pointer.  Returns the number consumed.
    template<typename Fn>
    std::size_t drain(Fn&& fn) noexcept
    {
        const std::size_t head = _head.load(std::memory_order_acquire);
        const std::size_t tail = _tail.load(std::memory_order_relaxed);
        const std::size_t n    = head - tail;
        for (std::size_t i = 0; i < n; ++i)
            fn(_data[(tail + i) & MASK]);
        _tail.store(head, std::memory_order_release);
        return n;
    }

    void clear() noexcept
    {
        _tail.store(_head.load(std::memory_order_acquire),
                    std::memory_order_release);
    }

    std::size_t available() const noexcept
    {
        return _head.load(std::memory_order_acquire)
             - _tail.load(std::memory_order_acquire);
    }

private:
    alignas(64) std::atomic<std::size_t> _head{0};
    alignas(64) std::atomic<std::size_t> _tail{0};
    std::array<float, CAPACITY>          _data{};
};

// ─────────────────────────────────────────────────────────────────────────────
// SPSCEventQueue – lock-free single-producer / single-consumer FIFO queue
//                  for arbitrary value types.
//
// CAPACITY must be a power of 2.
//
// Producer thread: push() – drops silently when full (no blocking).
// Consumer thread: pop(), empty(), size(), clear().
//
// Used for the onset NoteEvent queue so GDScript can drain detected attacks
// each frame for chart-aware judgment without missing events between frames.
// ─────────────────────────────────────────────────────────────────────────────

template<typename T, std::size_t CAPACITY>
struct SPSCEventQueue {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
                  "SPSCEventQueue CAPACITY must be a power of 2");
    static constexpr std::size_t MASK = CAPACITY - 1;

    SPSCEventQueue()                                         = default;
    SPSCEventQueue(const SPSCEventQueue&)                    = delete;
    SPSCEventQueue& operator=(const SPSCEventQueue&)         = delete;
    SPSCEventQueue(SPSCEventQueue&&)                         = delete;
    SPSCEventQueue& operator=(SPSCEventQueue&&)              = delete;

    /// Producer: enqueue item.  Returns false and drops it if the queue is full.
    bool push(const T& item) noexcept
    {
        const std::size_t head = _head.load(std::memory_order_relaxed);
        const std::size_t tail = _tail.load(std::memory_order_acquire);
        if (head - tail >= CAPACITY)
            return false; // full – drop
        _data[head & MASK] = item;
        _head.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Consumer: dequeue oldest item into out.  Returns false if empty.
    bool pop(T& out) noexcept
    {
        const std::size_t head = _head.load(std::memory_order_acquire);
        const std::size_t tail = _tail.load(std::memory_order_relaxed);
        if (tail == head)
            return false;
        out = _data[tail & MASK];
        _tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool        empty() const noexcept
    {
        return _head.load(std::memory_order_acquire)
            == _tail.load(std::memory_order_acquire);
    }

    std::size_t size()  const noexcept
    {
        return _head.load(std::memory_order_acquire)
             - _tail.load(std::memory_order_acquire);
    }

    void clear() noexcept
    {
        _tail.store(_head.load(std::memory_order_acquire),
                    std::memory_order_release);
    }

private:
    alignas(64) std::atomic<std::size_t> _head{0};
    alignas(64) std::atomic<std::size_t> _tail{0};
    std::array<T, CAPACITY>              _data{};
};

// ─────────────────────────────────────────────────────────────────────────────
// SPSCFrameHistory – lock-free single-producer / single-consumer circular log.
//
// CAPACITY must be a power of 2.
//
// Producer thread: push() – always writes; oldest entry is implicitly
//   overwritten once the buffer is full (tail = max(0, head - CAPACITY)).
//   The producer ONLY advances head, ensuring true SPSC independence.
// Consumer thread: read_newest() – non-destructive, returns up to count frames
//   newest-first without advancing any pointer.
//
// Used to expose a rolling window of recent DetectionFrames so GDScript can
// analyse sustain, bends, and vibrato without consuming the data.
// ─────────────────────────────────────────────────────────────────────────────

template<typename T, std::size_t CAPACITY>
struct SPSCFrameHistory {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
                  "SPSCFrameHistory CAPACITY must be a power of 2");
    static constexpr std::size_t MASK = CAPACITY - 1;

    SPSCFrameHistory()                                           = default;
    SPSCFrameHistory(const SPSCFrameHistory&)                    = delete;
    SPSCFrameHistory& operator=(const SPSCFrameHistory&)         = delete;
    SPSCFrameHistory(SPSCFrameHistory&&)                         = delete;
    SPSCFrameHistory& operator=(SPSCFrameHistory&&)              = delete;

    /// Producer: append item; overwrites the oldest entry when full.
    /// Only advances _head – never touches _tail (true SPSC).
    void push(const T& item) noexcept
    {
        const std::size_t head = _head.load(std::memory_order_relaxed);
        _data[head & MASK] = item;
        _head.store(head + 1, std::memory_order_release);
    }

    /// Consumer: copy up to count items (newest first) into out.
    /// Non-destructive – does not consume any entries.
    /// Returns the number of items written to out.
    std::size_t read_newest(T* out, std::size_t count) const noexcept
    {
        const std::size_t head  = _head.load(std::memory_order_acquire);
        const std::size_t avail = std::min(head, CAPACITY); // never exceed buffer fill
        const std::size_t n     = std::min(count, avail);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = _data[(head - 1 - i) & MASK];
        return n;
    }

    std::size_t size() const noexcept
    {
        const std::size_t head = _head.load(std::memory_order_acquire);
        return std::min(head, CAPACITY);
    }

    void clear() noexcept
    {
        _head.store(0, std::memory_order_release);
    }

private:
    alignas(64) std::atomic<std::size_t> _head{0};
    std::array<T, CAPACITY>              _data{};
};

// ─────────────────────────────────────────────────────────────────────────────
// BandDetector
// ─────────────────────────────────────────────────────────────────────────────

class BandDetector {
public:
    static constexpr float       DEFAULT_MIN_PERIODICITY = 0.8f;
    /// Audio SPSC ring buffer: ~1.5 s of mono audio at 44100 Hz.
    static constexpr std::size_t RING_CAPACITY           = 65536;
    /// Frame history depth: last 128 process() frames (~1–2 s at typical rates).
    static constexpr std::size_t FRAME_HISTORY_SIZE      = 128;
    /// Onset event queue: up to 32 pending events before oldest are dropped.
    static constexpr std::size_t EVENT_QUEUE_SIZE        = 32;
    /// Minimum samples between consecutive onset events (~46 ms at 44100 Hz).
    static constexpr std::size_t ONSET_COOLDOWN_SAMPLES  = 2048;

    BandDetector(float                           sample_rate,
                 const std::array<BandRange, 6>& ranges,
                 float                           threshold_db = -45.0f);
    ~BandDetector();

    BandDetector(const BandDetector&)            = delete;
    BandDetector& operator=(const BandDetector&) = delete;
    BandDetector(BandDetector&&)                 = delete;
    BandDetector& operator=(BandDetector&&)      = delete;

    // ── Audio path (audio or main thread → main thread) ───────────────────

    /// Write mono PCM samples into the audio SPSC ring buffer.
    void push_samples(const float* samples, std::size_t count);

    /// Drain all buffered samples through all 6 Q detectors, update the
    /// DetectionFrame snapshot, onset-event queue, and frame history.
    /// Always returns exactly 6 DetectionResults (legacy per-band API).
    std::vector<DetectionResult> process();

    /// Reset all Q detectors and all SPSC buffers.
    void reset();

    // ── New API: real-time analysis state ────────────────────────────────

    /// Latest analysis snapshot written by the most recent process() call.
    const DetectionFrame& latest_frame() const { return _latest_frame; }

    /// Pop the oldest pending NoteEvent (SPSC FIFO).
    /// Returns false when the queue is empty.  Call in a loop each frame.
    bool pop_event(NoteEvent& out) noexcept { return _event_queue.pop(out); }

    bool has_events() const noexcept { return !_event_queue.empty(); }

    /// Copy up to count recent DetectionFrames (newest first) into out.
    /// Non-destructive – same data is readable on every call.
    std::size_t get_frame_history(DetectionFrame* out, std::size_t count) const noexcept
    {
        return _frame_history.read_newest(out, count);
    }

    // ── Configuration ─────────────────────────────────────────────────────

    void  set_min_periodicity(float v) { _min_periodicity = std::max(0.0f, std::min(1.0f, v)); }
    float min_periodicity()  const     { return _min_periodicity; }

private:
    std::array<std::unique_ptr<cycfi::q::pitch_detector>, 6> _detectors;
    AudioRingBuffer<RING_CAPACITY>                            _ring;

    float         _sample_rate        = 44100.0f;
    float         _min_periodicity    = DEFAULT_MIN_PERIODICITY;
    std::size_t   _sample_count       = 0;
    std::size_t   _onset_cooldown_left = 0;

    DetectionFrame _latest_frame;

    SPSCFrameHistory<DetectionFrame, FRAME_HISTORY_SIZE> _frame_history;
    SPSCEventQueue<NoteEvent,        EVENT_QUEUE_SIZE>    _event_queue;
};
