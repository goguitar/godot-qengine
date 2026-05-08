#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "band_detector.hpp"

class AsyncBandDetector {
public:
    AsyncBandDetector(float sample_rate,
                      const std::array<BandRange, 6>& ranges,
                      float threshold_db,
                      float min_periodicity);
    ~AsyncBandDetector();

    AsyncBandDetector(const AsyncBandDetector&)            = delete;
    AsyncBandDetector& operator=(const AsyncBandDetector&) = delete;
    AsyncBandDetector(AsyncBandDetector&&)                 = delete;
    AsyncBandDetector& operator=(AsyncBandDetector&&)      = delete;

    void push_samples(const float* samples, std::size_t count);
    void request_reset();
    void set_min_periodicity(float v);

    DetectionFrame latest_frame() const;
    ChordFrame latest_chord_frame() const;

    bool pop_event(NoteEvent& out) noexcept;
    bool pop_chord_frame(ChordFrame& out) noexcept;

    std::size_t get_frame_history(DetectionFrame* out, std::size_t count) const noexcept;

    bool has_events() const noexcept;
    bool has_chord_frames() const noexcept;

    static void shutdown_all();

private:
    void worker_loop();
    void stop_worker();

    static std::mutex _registry_mutex;
    static std::vector<AsyncBandDetector*> _registry;

    std::unique_ptr<BandDetector> _detector;
    std::atomic<bool>             _running{false};
    std::atomic<bool>             _stopped{false};
    std::atomic<bool>             _reset_requested{false};
    std::thread                   _worker;
    std::mutex                    _mutex;
    std::condition_variable       _cv;
};
