//! Safe Rust wrappers over the raw C FFI.
//!
//! These types manage the C++ object lifetimes (RAII drop) and expose
//! idiomatic Rust APIs.  They are the only interface that higher-level crates
//! (`qengine-core`, `godot-qengine`) should use.

use std::ffi::c_void;

use crate::{
    q_guitar_detector_free, q_guitar_detector_new, q_guitar_detector_process_buffer,
    q_guitar_detector_reset, q_pitch_detector_free, q_pitch_detector_get_frequency,
    q_pitch_detector_new, q_pitch_detector_periodicity, q_pitch_detector_process,
    q_pitch_detector_process_buffer, q_pitch_detector_reset,
};

/* -------------------------------------------------------------------------
 * Send safety
 *
 * The underlying C++ objects are not thread-safe (no interior locking), but
 * they ARE safe to *send* to another thread as long as only one thread uses
 * them at a time.  We model this the same way `Box<T>` is Send when T: Send.
 * ---------------------------------------------------------------------- */

/// Wraps a raw `*mut c_void` handle and marks it as `Send`.
///
/// Safety: callers must ensure exclusive access (single producer, single
/// consumer pattern – see `AudioEffectInstanceQEngine`).
struct SendPtr(*mut c_void);

unsafe impl Send for SendPtr {}

/* -------------------------------------------------------------------------
 * Single-band pitch detector
 * ---------------------------------------------------------------------- */

/// Safe wrapper for a single `q::pitch_detector` instance.
///
/// # Thread safety
/// `PitchDetector` is `Send` (can be transferred to the audio thread) but not
/// `Sync` (must not be shared between threads concurrently).
pub struct PitchDetector {
    inner: SendPtr,
}

impl PitchDetector {
    /// Create a new detector for the frequency band `[min_hz, max_hz]`.
    ///
    /// `threshold_db` is the noise-floor in dB (negative, e.g. `-45.0`).
    pub fn new(min_hz: f32, max_hz: f32, sample_rate: f32, threshold_db: f32) -> Self {
        let ptr = unsafe { q_pitch_detector_new(min_hz, max_hz, sample_rate, threshold_db) };
        assert!(!ptr.is_null(), "q_pitch_detector_new returned null");
        PitchDetector { inner: SendPtr(ptr) }
    }

    /// Feed one mono sample.  Returns `true` when the detector has updated
    /// its frequency estimate.
    #[inline]
    pub fn process(&mut self, sample: f32) -> bool {
        unsafe { q_pitch_detector_process(self.inner.0, sample) }
    }

    /// Latest detected frequency in Hz.  Returns `0.0` when no pitch is found.
    #[inline]
    pub fn frequency(&self) -> f32 {
        unsafe { q_pitch_detector_get_frequency(self.inner.0) }
    }

    /// Periodicity / confidence in `[0, 1]`.
    #[inline]
    pub fn periodicity(&self) -> f32 {
        unsafe { q_pitch_detector_periodicity(self.inner.0) }
    }

    /// Reset internal state.
    #[inline]
    pub fn reset(&mut self) {
        unsafe { q_pitch_detector_reset(self.inner.0) }
    }

    /// Process a slice of samples and return the final frequency estimate.
    pub fn process_buffer(&mut self, samples: &[f32]) -> f32 {
        if samples.is_empty() {
            return self.frequency();
        }
        unsafe {
            q_pitch_detector_process_buffer(
                self.inner.0,
                samples.as_ptr(),
                samples.len() as i32,
            )
        }
    }
}

impl Drop for PitchDetector {
    fn drop(&mut self) {
        unsafe { q_pitch_detector_free(self.inner.0) }
    }
}

/* -------------------------------------------------------------------------
 * Multi-band (6-string) guitar detector
 * ---------------------------------------------------------------------- */

/// Safe wrapper for a 6-band `QGuitarDetector` covering all guitar strings.
pub struct GuitarDetector {
    inner: SendPtr,
}

impl GuitarDetector {
    /// Create a detector for the given tuning.
    ///
    /// `tuning_id` must be one of the `Q_TUNING_*` constants from the crate
    /// root (e.g. [`crate::Q_TUNING_STANDARD`]).
    pub fn new(tuning_id: i32, sample_rate: f32, threshold_db: f32) -> Self {
        let ptr = unsafe { q_guitar_detector_new(tuning_id, sample_rate, threshold_db) };
        assert!(!ptr.is_null(), "q_guitar_detector_new returned null");
        GuitarDetector { inner: SendPtr(ptr) }
    }

    /// Process a mono sample buffer through all 6 bands simultaneously.
    ///
    /// Returns `([frequencies; 6], [periodicities; 6])`.
    pub fn process_buffer(&mut self, samples: &[f32]) -> ([f32; 6], [f32; 6]) {
        let mut freqs = [0.0f32; 6];
        let mut periods = [0.0f32; 6];
        if !samples.is_empty() {
            unsafe {
                q_guitar_detector_process_buffer(
                    self.inner.0,
                    samples.as_ptr(),
                    samples.len() as i32,
                    freqs.as_mut_ptr(),
                    periods.as_mut_ptr(),
                );
            }
        }
        (freqs, periods)
    }

    /// Reset all 6 band detectors.
    pub fn reset(&mut self) {
        unsafe { q_guitar_detector_reset(self.inner.0) }
    }
}

impl Drop for GuitarDetector {
    fn drop(&mut self) {
        unsafe { q_guitar_detector_free(self.inner.0) }
    }
}

/* =========================================================================
 * Tests
 * ====================================================================== */

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::PI;

    fn make_sine(freq: f32, sr: f32, seconds: f32) -> Vec<f32> {
        let n = (sr * seconds) as usize;
        (0..n)
            .map(|i| (2.0 * PI * freq * i as f32 / sr).sin())
            .collect()
    }

    #[test]
    fn single_band_detects_a4() {
        let mut det = PitchDetector::new(400.0, 500.0, 44100.0, -45.0);
        let buf = make_sine(440.0, 44100.0, 1.0);
        let freq = det.process_buffer(&buf);
        assert!(
            (freq - 440.0).abs() < 1.0,
            "expected ~440 Hz, got {freq:.2} Hz"
        );
    }

    #[test]
    fn guitar_detector_standard_tuning_e2() {
        let mut gd = GuitarDetector::new(
            crate::Q_TUNING_STANDARD,
            44100.0,
            -45.0,
        );
        let buf = make_sine(82.41, 44100.0, 1.0);
        let (freqs, _) = gd.process_buffer(&buf);
        // Band 0 = E2 (82.41 Hz)
        assert!(
            (freqs[0] - 82.41).abs() < 1.0,
            "expected ~82.41 Hz on band 0, got {:.2}", freqs[0]
        );
    }

    #[test]
    fn guitar_detector_all_standard_strings() {
        let sr = 44100.0;
        let expected = [82.41f32, 110.0, 146.83, 196.0, 246.94, 329.63];
        for (band, &freq) in expected.iter().enumerate() {
            let mut gd = GuitarDetector::new(crate::Q_TUNING_STANDARD, sr, -45.0);
            let buf = make_sine(freq, sr, 1.0);
            let (freqs, _) = gd.process_buffer(&buf);
            assert!(
                (freqs[band] - freq).abs() < 2.0,
                "band {band}: expected ~{freq:.2} Hz, got {:.2}", freqs[band]
            );
        }
    }

    #[test]
    fn guitar_detector_drop_d_low_string() {
        let mut gd = GuitarDetector::new(crate::Q_TUNING_DROP_D, 44100.0, -45.0);
        let buf = make_sine(73.42, 44100.0, 1.0);
        let (freqs, _) = gd.process_buffer(&buf);
        assert!(
            (freqs[0] - 73.42).abs() < 1.0,
            "expected ~73.42 Hz on band 0 (Drop D), got {:.2}", freqs[0]
        );
    }
}
