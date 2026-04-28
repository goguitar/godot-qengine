//! Multi-band guitar pitch detector.
//!
//! `BandDetector` wraps the Q FFI `GuitarDetector` and adds:
//! - SPSC ring buffer for lock-free audio ingestion
//! - Note identification via [`crate::note`]
//! - Per-band `DetectionResult` with confidence filtering

use rtrb::{Consumer, Producer};

use crate::note::DetectedNote;
use crate::ring_buffer::{ConsumerExt, DEFAULT_CAPACITY};
use crate::tuning::{Tuning, TuningId};
use qengine_sys::bridge::GuitarDetector;

/// Detection result for a single guitar string band.
#[derive(Debug, Clone)]
pub struct DetectionResult {
    /// Band index (0 = lowest string, 5 = highest).
    pub band: usize,
    /// Open-string label for this band, e.g. `"E2"`.
    pub string_label: String,
    /// Raw frequency from Q (0.0 if no pitch detected).
    pub raw_freq: f32,
    /// Periodicity / confidence in `[0, 1]`.
    pub periodicity: f32,
    /// Identified note (if `raw_freq > 0` and within bounds).
    pub note: Option<DetectedNote>,
}

impl DetectionResult {
    /// Returns `true` if a note was detected above `min_periodicity`.
    pub fn is_active(&self, min_periodicity: f32) -> bool {
        self.note.is_some() && self.periodicity >= min_periodicity
    }
}

/// Minimum periodicity (confidence) required to report a note.
const DEFAULT_MIN_PERIODICITY: f32 = 0.8;

/// Multi-band detector: wraps a `GuitarDetector` (via Q FFI) and a SPSC
/// sample ring-buffer.
///
/// # Usage
///
/// ```text
/// let (mut detector, producer) = BandDetector::new(44100.0, TuningId::Standard);
/// // Audio thread: producer.push_samples(&block);
/// // Main  thread: let results = detector.process();
/// ```
pub struct BandDetector {
    q:               GuitarDetector,
    consumer:        Consumer<f32>,
    tuning:          Tuning,
    work_buf:        Vec<f32>,
    min_periodicity: f32,
}

impl BandDetector {
    /// Create a new `BandDetector` and return the `Producer` end.
    ///
    /// The returned producer must be used **exclusively** from the audio
    /// thread; the detector itself (consumer + Q detector) should live on the
    /// main thread.
    pub fn new(sample_rate: f32, tuning_id: TuningId) -> (Self, Producer<f32>) {
        Self::with_config(sample_rate, tuning_id, DEFAULT_CAPACITY, -45.0)
    }

    /// Like `new` but allows custom ring-buffer capacity and dB threshold.
    pub fn with_config(
        sample_rate:  f32,
        tuning_id:    TuningId,
        buf_capacity: usize,
        threshold_db: f32,
    ) -> (Self, Producer<f32>) {
        let (producer, consumer) = rtrb::RingBuffer::new(buf_capacity);
        let q = GuitarDetector::new(tuning_id.to_c_id(), sample_rate, threshold_db);
        let tuning = Tuning::get(tuning_id);
        let det = BandDetector {
            q,
            consumer,
            tuning,
            work_buf: Vec::with_capacity(4096),
            min_periodicity: DEFAULT_MIN_PERIODICITY,
        };
        (det, producer)
    }

    /// Set the minimum periodicity (confidence) threshold.  Values in `[0, 1]`.
    pub fn set_min_periodicity(&mut self, v: f32) {
        self.min_periodicity = v.clamp(0.0, 1.0);
    }

    /// Drain available samples from the ring buffer and run Q detection.
    ///
    /// Returns one `DetectionResult` per string band (always 6 elements).
    /// Call this from the **main thread** each game frame.
    pub fn process(&mut self) -> Vec<DetectionResult> {
        // Drain ring buffer into work buffer.
        self.consumer.drain_into(&mut self.work_buf);

        if self.work_buf.is_empty() {
            return self.empty_results();
        }

        // Run Q pitch detection on all accumulated samples.
        let (freqs, periods) = self.q.process_buffer(&self.work_buf);
        self.work_buf.clear();

        // Convert raw frequencies to DetectionResult per band.
        (0..6)
            .map(|b| {
                let s     = &self.tuning.strings[b];
                let freq  = freqs[b];
                let per   = periods[b];
                let note  = if freq > 0.0 {
                    DetectedNote::from_frequency(freq)
                } else {
                    None
                };
                DetectionResult {
                    band:         b,
                    string_label: format!("{}{}", s.note_name, s.octave),
                    raw_freq:     freq,
                    periodicity:  per,
                    note,
                }
            })
            .collect()
    }

    /// Reset all Q band detectors and clear the work buffer.
    pub fn reset(&mut self) {
        self.q.reset();
        self.work_buf.clear();
        // Drain and discard any buffered samples.
        let n = self.consumer.slots();
        for _ in 0..n {
            let _ = self.consumer.pop();
        }
    }

    pub fn tuning(&self) -> &Tuning {
        &self.tuning
    }

    fn empty_results(&self) -> Vec<DetectionResult> {
        (0..6)
            .map(|b| {
                let s = &self.tuning.strings[b];
                DetectionResult {
                    band:         b,
                    string_label: format!("{}{}", s.note_name, s.octave),
                    raw_freq:     0.0,
                    periodicity:  0.0,
                    note:         None,
                }
            })
            .collect()
    }
}

/* =========================================================================
 * Tests
 * ====================================================================== */

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ring_buffer::ProducerExt;
    use std::f32::consts::PI;

    fn sine(freq: f32, sr: f32, secs: f32) -> Vec<f32> {
        let n = (sr * secs) as usize;
        (0..n).map(|i| (2.0 * PI * freq * i as f32 / sr).sin()).collect()
    }

    #[test]
    fn band_detector_e2_standard() {
        let (mut det, mut prod) = BandDetector::new(44100.0, TuningId::Standard);
        let buf = sine(82.41, 44100.0, 1.0);
        prod.push_samples(&buf);
        let results = det.process();
        let b0 = &results[0]; // E2 band
        assert!(b0.raw_freq > 0.0, "E2 band should detect a frequency");
        assert!(
            (b0.raw_freq - 82.41).abs() < 2.0,
            "expected ~82.41 Hz, got {:.2}", b0.raw_freq
        );
        let note = b0.note.as_ref().expect("should identify a note");
        assert_eq!(note.name, "E");
        assert_eq!(note.octave, 2);
    }

    #[test]
    fn band_detector_drop_d_low_string() {
        let (mut det, mut prod) = BandDetector::new(44100.0, TuningId::DropD);
        let buf = sine(73.42, 44100.0, 1.0);
        prod.push_samples(&buf);
        let results = det.process();
        let b0 = &results[0];
        assert!(
            (b0.raw_freq - 73.42).abs() < 2.0,
            "expected ~73.42 Hz for Drop D low string, got {:.2}", b0.raw_freq
        );
    }

    #[test]
    fn band_detector_returns_six_bands() {
        let (mut det, _prod) = BandDetector::new(44100.0, TuningId::Standard);
        let results = det.process();
        assert_eq!(results.len(), 6);
    }
}
