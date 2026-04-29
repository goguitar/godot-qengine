//! `AudioEffectQEngine` – custom Godot AudioEffect based on
//! `AudioEffectCapture` for safe runtime capture + pitch detection.

use godot::classes::AudioEffectCapture;
use godot::prelude::*;
use rtrb::Producer;

use qengine_core::ring_buffer::DEFAULT_CAPACITY;
use qengine_core::tuning::TuningId;
use qengine_core::{BandDetector, DetectionResult};

#[derive(GodotClass)]
#[class(base = AudioEffectCapture, init)]
pub struct AudioEffectQEngine {
    #[init(val = 44100.0)]
    #[export]
    sample_rate: f64,
    #[init(val = -45.0)]
    #[export]
    threshold_db: f64,
    #[init(val = GString::from("Standard"))]
    #[export]
    tuning: GString,
    #[init(val = 0.8)]
    #[export]
    min_periodicity: f64,

    detector: Option<BandDetector>,
    producer: Option<Producer<f32>>,
    cfg_sample_rate: f64,
    cfg_threshold_db: f64,
    cfg_tuning: GString,
    cfg_min_periodicity: f64,

    base: Base<AudioEffectCapture>,
}

#[godot_api]
impl AudioEffectQEngine {
    #[func]
    pub fn poll_notes(&mut self) -> Array<Variant> {
        self.ensure_detector();

        let available = self.base().get_frames_available();
        if available > 0 {
            let frames = self.base().get_buffer(available);
            if let Some(ref mut prod) = self.producer {
                for frame in frames.as_slice() {
                    let mono = (frame.x + frame.y) * 0.5;
                    let _ = prod.push(mono);
                }
            }
        }

        let Some(ref mut det) = self.detector else {
            return Array::new();
        };

        let min_p = self.min_periodicity as f32;
        let results: Vec<DetectionResult> = det.process();
        let filtered: Vec<DetectionResult> = results
            .into_iter()
            .filter(|r| r.raw_freq > 0.0 && r.periodicity >= min_p)
            .collect();

        detection_results_to_array(filtered)
    }

    #[func]
    pub fn reset(&mut self) {
        if let Some(ref mut det) = self.detector {
            det.reset();
        }
        self.base_mut().clear_buffer();
    }
}

impl AudioEffectQEngine {
    fn ensure_detector(&mut self) {
        let needs_rebuild = self.detector.is_none()
            || self.cfg_sample_rate != self.sample_rate
            || self.cfg_threshold_db != self.threshold_db
            || self.cfg_min_periodicity != self.min_periodicity
            || self.cfg_tuning != self.tuning;

        if !needs_rebuild {
            return;
        }

        let tuning_id = TuningId::from_str(self.tuning.to_string().as_str())
            .unwrap_or(TuningId::Standard);

        let (mut det, prod) = BandDetector::with_config(
            self.sample_rate as f32,
            tuning_id,
            DEFAULT_CAPACITY,
            self.threshold_db as f32,
        );
        det.set_min_periodicity(self.min_periodicity as f32);

        self.detector = Some(det);
        self.producer = Some(prod);
        self.cfg_sample_rate = self.sample_rate;
        self.cfg_threshold_db = self.threshold_db;
        self.cfg_tuning = self.tuning.clone();
        self.cfg_min_periodicity = self.min_periodicity;
        self.base_mut().clear_buffer();
    }
}

pub(crate) fn detection_results_to_array(results: Vec<DetectionResult>) -> Array<Variant> {
    let mut out: Array<Variant> = Array::new();
    for r in results {
        let mut d: VarDictionary = Dictionary::new();
        d.set("band", r.band as i64);
        let string_label = GString::from(&r.string_label);
        d.set("string", &string_label);
        d.set("frequency", r.raw_freq as f64);
        d.set("periodicity", r.periodicity as f64);
        if let Some(ref note) = r.note {
            let note_str = GString::from(note.display().as_str());
            d.set("note", &note_str);
            d.set("cents", note.cents as f64);
        } else {
            let empty = GString::new();
            d.set("note", &empty);
            d.set("cents", 0.0f64);
        }
        out.push(&d.to_variant());
    }
    out
}
