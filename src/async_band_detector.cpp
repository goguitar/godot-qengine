#include "async_band_detector.hpp"

#include <algorithm>
#include <chrono>

std::mutex AsyncBandDetector::_registry_mutex;
std::vector<AsyncBandDetector*> AsyncBandDetector::_registry;

AsyncBandDetector::AsyncBandDetector(float sample_rate,
                                     const std::array<BandRange, 6>& ranges,
                                     float threshold_db,
                                     float min_periodicity)
{
    _detector = std::make_unique<BandDetector>(sample_rate, ranges, threshold_db);
    _detector->set_min_periodicity(min_periodicity);

    {
        std::lock_guard<std::mutex> lock(_registry_mutex);
        _registry.push_back(this);
    }

    _running.store(true, std::memory_order_release);
    _worker = std::thread(&AsyncBandDetector::worker_loop, this);
}

AsyncBandDetector::~AsyncBandDetector()
{
    stop_worker();
    {
        std::lock_guard<std::mutex> lock(_registry_mutex);
        auto it = std::find(_registry.begin(), _registry.end(), this);
        if (it != _registry.end()) {
            _registry.erase(it);
        }
    }
}

void AsyncBandDetector::push_samples(const float* samples, std::size_t count)
{
    if (!_detector || count == 0) {
        return;
    }
    _detector->push_samples(samples, count);
    _cv.notify_one();
}

void AsyncBandDetector::request_reset()
{
    if (!_detector) {
        return;
    }
    _reset_requested.store(true, std::memory_order_release);
    _cv.notify_one();
}

void AsyncBandDetector::set_min_periodicity(float v)
{
    if (_detector) {
        _detector->set_min_periodicity(v);
    }
}

DetectionFrame AsyncBandDetector::latest_frame() const
{
    if (!_detector) {
        return DetectionFrame{};
    }
    return _detector->latest_frame();
}

ChordFrame AsyncBandDetector::latest_chord_frame() const
{
    if (!_detector) {
        return ChordFrame{};
    }
    return _detector->latest_chord_frame();
}

bool AsyncBandDetector::pop_event(NoteEvent& out) noexcept
{
    return _detector ? _detector->pop_event(out) : false;
}

bool AsyncBandDetector::pop_chord_frame(ChordFrame& out) noexcept
{
    return _detector ? _detector->pop_chord_frame(out) : false;
}

std::size_t AsyncBandDetector::get_frame_history(DetectionFrame* out, std::size_t count) const noexcept
{
    return _detector ? _detector->get_frame_history(out, count) : 0;
}

bool AsyncBandDetector::has_events() const noexcept
{
    return _detector ? _detector->has_events() : false;
}

bool AsyncBandDetector::has_chord_frames() const noexcept
{
    return _detector ? _detector->has_chord_frames() : false;
}

void AsyncBandDetector::shutdown_all()
{
    std::vector<AsyncBandDetector*> snapshot;
    {
        std::lock_guard<std::mutex> lock(_registry_mutex);
        snapshot = _registry;
    }

    for (auto* detector : snapshot) {
        if (detector) {
            detector->stop_worker();
        }
    }
}

void AsyncBandDetector::worker_loop()
{
    while (_running.load(std::memory_order_acquire)) {
        if (_reset_requested.exchange(false, std::memory_order_acq_rel) && _detector) {
            _detector->reset();
        }

        if (!_detector) {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait_for(lock, std::chrono::milliseconds(2));
            continue;
        }

        if (_detector->available_samples() == 0) {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait_for(lock, std::chrono::milliseconds(2), [&]() {
                return !_running.load(std::memory_order_acquire)
                    || _reset_requested.load(std::memory_order_acquire)
                    || _detector->available_samples() > 0;
            });
            continue;
        }

        _detector->process();
    }
}

void AsyncBandDetector::stop_worker()
{
    bool expected = false;
    if (!_stopped.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    _running.store(false, std::memory_order_release);
    _cv.notify_one();
    if (_worker.joinable()) {
        _worker.join();
    }
}
