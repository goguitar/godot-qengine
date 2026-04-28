//! `QEngineDetectorNode` – a Godot 4.5 `Node` for easy GDScript integration.

use godot::classes::{Node, INode};
use godot::prelude::*;
use rtrb::Producer;

use qengine_core::ring_buffer::{ProducerExt, DEFAULT_CAPACITY};
use qengine_core::tuning::TuningId;
use qengine_core::{BandDetector, DetectionResult};

use crate::audio_effect_qengine::detection_results_to_array;

#[derive(GodotClass)]
#[class(base = Node)]
pub struct QEngineDetectorNode {
    #[export]
    sample_rate: f64,
    #[export]
    threshold_db: f64,
    #[export]
    tuning: GString,
    #[export]
    min_periodicity: f64,
    #[export]
    auto_poll: bool,

    detector: Option<BandDetector>,
    producer: Option<Producer<f32>>,

    base: Base<Node>,
}

#[godot_api]
impl INode for QEngineDetectorNode {
    fn init(base: Base<Node>) -> Self {
        QEngineDetectorNode {
            sample_rate:     44100.0,
            threshold_db:    -45.0,
            tuning:          "Standard".into(),
            min_periodicity: 0.8,
            auto_poll:       true,
            detector:        None,
            producer:        None,
            base,
        }
    }

    fn ready(&mut self) {
        self.init_detector();
    }

    fn process(&mut self, _delta: f64) {
        if self.auto_poll {
            let notes = self.poll_notes_internal();
            self.base_mut()
                .emit_signal("notes_detected", &[notes.to_variant()]);
        }
    }
}

#[godot_api]
impl QEngineDetectorNode {
    #[signal]
    fn notes_detected(notes: Array<Variant>);

    #[func]
    pub fn init_detector(&mut self) {
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
    }

    #[func]
    pub fn push_samples(&mut self, samples: PackedFloat32Array) {
        if let Some(ref mut prod) = self.producer {
            prod.push_samples(samples.as_slice());
        }
    }

    #[func]
    pub fn poll_notes(&mut self) -> Array<Variant> {
        let notes = self.poll_notes_internal();
        self.base_mut()
            .emit_signal("notes_detected", &[notes.to_variant()]);
        notes
    }

    #[func]
    pub fn reset(&mut self) {
        if let Some(ref mut det) = self.detector {
            det.reset();
        }
    }

    fn poll_notes_internal(&mut self) -> Array<Variant> {
        let Some(ref mut det) = self.detector else {
            return Array::new();
        };
        let min_p = self.min_periodicity as f32;
        let results: Vec<DetectionResult> = det.process();
        let filtered: Vec<DetectionResult> = results
            .into_iter()
            .filter(|r| r.raw_freq > 0.0 || r.periodicity >= min_p)
            .collect();
        detection_results_to_array(filtered)
    }
}
