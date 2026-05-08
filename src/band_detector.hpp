// band_detector.hpp – Multi-band guitar pitch detector.
//
// BandDetector wraps six cycfi/Q pitch_detector instances (one per guitar
// string) and buffers incoming audio samples in a lock-free SPSC ring buffer
// until process() is called on the consumer thread.
//
// No Godot dependency – usable from tests without a Godot installation.
// Tuning data (per-band frequency ranges) and note-name mapping are the
// responsibility of the caller (e.g. GDScript in the Godot layer).

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
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
// DetectionResult – result for one string band
// ─────────────────────────────────────────────────────────────────────────────

struct DetectionResult {
    int   band;        ///< 0 = lowest string
    float raw_freq;    ///< detected Hz (0 if none)
    float periodicity; ///< Q confidence [0, 1]
    int   midi_note;   ///< nearest MIDI note [0, 127]; -1 if no detection
    float cents;       ///< deviation from nearest semitone in cents [-50, +50]
};

// ─────────────────────────────────────────────────────────────────────────────
// AudioRingBuffer – lock-free single-producer / single-consumer circular buffer
//
// CAPACITY must be a power of 2.
//
// Producer thread: push()
// Consumer thread: drain(), clear(), available()
// ─────────────────────────────────────────────────────────────────────────────

template<std::size_t CAPACITY>
struct AudioRingBuffer {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
                  "AudioRingBuffer CAPACITY must be a power of 2");
    static constexpr std::size_t MASK = CAPACITY - 1;

    // Non-copyable and non-movable (std::atomic members cannot be moved).
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
    /// then advance the tail pointer (consume them).
    /// Returns the number of samples consumed.
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

    /// Consumer: discard all pending samples.
    void clear() noexcept
    {
        _tail.store(_head.load(std::memory_order_acquire),
                    std::memory_order_release);
    }

    /// Number of samples currently available to the consumer.
    std::size_t available() const noexcept
    {
        return _head.load(std::memory_order_acquire)
             - _tail.load(std::memory_order_acquire);
    }

private:
    // Cache-line-aligned to prevent false sharing between producer and consumer.
    alignas(64) std::atomic<std::size_t> _head{0};
    alignas(64) std::atomic<std::size_t> _tail{0};
    std::array<float, CAPACITY>          _data{};
};

// ─────────────────────────────────────────────────────────────────────────────
// BandDetector
// ─────────────────────────────────────────────────────────────────────────────

class BandDetector {
public:
    /// Minimum periodicity (confidence) threshold used by default.
    static constexpr float       DEFAULT_MIN_PERIODICITY = 0.8f;
    /// Ring buffer capacity: ~1.5 s of mono audio at 44100 Hz.
    static constexpr std::size_t RING_CAPACITY           = 65536;

    /// Construct a 6-band detector.
    /// @param sample_rate   Audio sample rate (Hz).
    /// @param ranges        Per-band frequency bounds (6 entries, index 0 = lowest string).
    /// @param threshold_db  Noise-floor threshold in dB (negative, e.g. -45).
    BandDetector(float                           sample_rate,
                 const std::array<BandRange, 6>& ranges,
                 float                           threshold_db = -45.0f);

    ~BandDetector();

    // Non-copyable and non-movable (AudioRingBuffer contains std::atomic).
    BandDetector(const BandDetector&)            = delete;
    BandDetector& operator=(const BandDetector&) = delete;
    BandDetector(BandDetector&&)                 = delete;
    BandDetector& operator=(BandDetector&&)      = delete;

    /// Producer thread: write mono PCM samples into the SPSC ring buffer.
    void push_samples(const float* samples, std::size_t count);

    /// Consumer thread: drain all buffered samples through all 6 Q detectors,
    /// then return one DetectionResult per band.  Always returns exactly 6 results.
    std::vector<DetectionResult> process();

    /// Reset all Q detectors and discard any buffered samples.
    void reset();

    void  set_min_periodicity(float v) { _min_periodicity = std::max(0.0f, std::min(1.0f, v)); }
    float min_periodicity()  const     { return _min_periodicity; }

private:
    std::array<std::unique_ptr<cycfi::q::pitch_detector>, 6> _detectors;
    AudioRingBuffer<RING_CAPACITY>                            _ring;
    float                                                     _min_periodicity = DEFAULT_MIN_PERIODICITY;
};
