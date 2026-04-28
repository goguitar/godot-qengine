//! `AudioEffectQEngine` and `AudioEffectInstanceQEngine` – Godot 4.5
//! GDExtension classes that plug into the Godot audio bus and expose
//! per-string guitar pitch detection powered by the cycfi/Q library.
//!
//! # Audio-thread safety
//!
//! Godot calls `_process_rawptr` on the **audio thread**.  Rust's ownership
//! rules and the `rtrb` SPSC ring buffer ensure that:
//!
//! - The `rtrb::Producer<f32>` lives exclusively in `AudioEffectInstanceQEngine`
//!   and is touched only from the audio thread.
//! - The `rtrb::Consumer<f32>` and `GuitarDetector` live in
//!   `AudioEffectQEngine` and are touched only from the main thread.
//! - No mutex is held on the audio-thread hot path.

use std::ffi::c_void;
use std::sync::{Arc, Mutex};

use godot::classes::native::AudioFrame;
use godot::classes::{AudioEffect, AudioEffectInstance, IAudioEffect, IAudioEffectInstance};
use godot::meta::conv::RawPtr;
use godot::prelude::*;
use rtrb::Producer;

use qengine_core::ring_buffer::{DEFAULT_CAPACITY};
use qengine_core::tuning::TuningId;
use qengine_core::{BandDetector, DetectionResult};

/* -------------------------------------------------------------------------
 * Transfer cell for the SPSC producer
 * ---------------------------------------------------------------------- */

type ProducerSlot = Arc<Mutex<Option<Producer<f32>>>>;

/* =========================================================================
 * AudioEffectQEngine  (extends AudioEffect / Resource)
 * ====================================================================== */

/// Godot AudioEffect resource.
///
/// Add this to an audio bus in the Godot inspector, then call
/// [`AudioEffectQEngine::poll_notes`] each frame from GDScript.
#[derive(GodotClass)]
#[class(base = AudioEffect)]
pub struct AudioEffectQEngine {
    #[export]
    sample_rate: f64,
    #[export]
    threshold_db: f64,
    #[export]
    tuning: GString,

    detector:      Option<BandDetector>,
    producer_slot: Option<ProducerSlot>,

    base: Base<AudioEffect>,
}

#[godot_api]
impl IAudioEffect for AudioEffectQEngine {
    fn init(base: Base<AudioEffect>) -> Self {
        AudioEffectQEngine {
            sample_rate:   44100.0,
            threshold_db:  -45.0,
            tuning:        "Standard".into(),
            detector:      None,
            producer_slot: None,
            base,
        }
    }

    fn instantiate(&mut self) -> Option<Gd<AudioEffectInstance>> {
        let tuning_id = TuningId::from_str(self.tuning.to_string().as_str())
            .unwrap_or(TuningId::Standard);

        let (det, producer) = BandDetector::with_config(
            self.sample_rate as f32,
            tuning_id,
            DEFAULT_CAPACITY,
            self.threshold_db as f32,
        );

        let slot: ProducerSlot = Arc::new(Mutex::new(Some(producer)));
        self.detector      = Some(det);
        self.producer_slot = Some(Arc::clone(&slot));

        let instance = AudioEffectInstanceQEngine::new_instance(slot);
        Some(instance.upcast())
    }
}

#[godot_api]
impl AudioEffectQEngine {
    /// Drain buffered audio samples, run Q pitch detection, and return an
    /// `Array` of `Dictionary` objects (one per guitar string band).
    ///
    /// Dictionary keys:
    /// - `"band"` : `int`
    /// - `"string"` : `String`   (e.g. `"E2"`)
    /// - `"frequency"` : `float` (Hz, 0 if not detected)
    /// - `"periodicity"` : `float` (Q confidence, 0–1)
    /// - `"note"` : `String`     (e.g. `"E2"`, `""` if none)
    /// - `"cents"` : `float`     (deviation from equal temperament)
    #[func]
    pub fn poll_notes(&mut self) -> Array<Variant> {
        let Some(ref mut det) = self.detector else {
            return Array::new();
        };
        detection_results_to_array(det.process())
    }

    /// Reset all Q detectors and flush the ring buffer.
    #[func]
    pub fn reset(&mut self) {
        if let Some(ref mut det) = self.detector {
            det.reset();
        }
    }
}

/* =========================================================================
 * AudioEffectInstanceQEngine  (extends AudioEffectInstance)
 * ====================================================================== */

/// Per-instance audio processor created by [`AudioEffectQEngine::instantiate`].
#[derive(GodotClass)]
#[class(base = AudioEffectInstance)]
pub struct AudioEffectInstanceQEngine {
    producer: Option<Producer<f32>>,
    slot:     ProducerSlot,
    base:     Base<AudioEffectInstance>,
}

impl AudioEffectInstanceQEngine {
    pub fn new_instance(slot: ProducerSlot) -> Gd<Self> {
        Gd::from_init_fn(|base| AudioEffectInstanceQEngine {
            producer: None,
            slot,
            base,
        })
    }
}

#[godot_api]
impl IAudioEffectInstance for AudioEffectInstanceQEngine {
    fn init(base: Base<AudioEffectInstance>) -> Self {
        AudioEffectInstanceQEngine {
            producer: None,
            slot:     Arc::new(Mutex::new(None)),
            base,
        }
    }

    /// Called by Godot's audio thread for every audio block.
    ///
    /// # Safety
    /// `src_buffer` is valid for `frame_count` consecutive `AudioFrame` values.
    unsafe fn process_rawptr(
        &mut self,
        src_buffer: RawPtr<*const c_void>,
        dst_buffer: RawPtr<*mut AudioFrame>,
        frame_count: i32,
    ) {
        let count = frame_count as usize;
        let src = src_buffer.ptr() as *const AudioFrame;
        let dst = dst_buffer.ptr();

        // One-time: take the producer out of the transfer slot (lock-free
        // after this first acquisition).
        if self.producer.is_none() {
            if let Ok(mut guard) = self.slot.try_lock() {
                self.producer = guard.take();
            }
        }

        // Push mono-mixed samples (lock-free SPSC).
        if let Some(ref mut prod) = self.producer {
            for i in 0..count {
                let frame = unsafe { (*src.add(i)).clone() };
                let mono  = (frame.left + frame.right) * 0.5;
                let _     = prod.push(mono);
            }
        }

        // Pass audio through unchanged (capture-only effect).
        unsafe {
            std::ptr::copy_nonoverlapping(src, dst, count);
        }
    }

    fn process_silence(&self) -> bool {
        true
    }
}

/* =========================================================================
 * Shared helper
 * ====================================================================== */

pub(crate) fn detection_results_to_array(results: Vec<DetectionResult>) -> Array<Variant> {
    let mut out: Array<Variant> = Array::new();
    for r in results {
        let mut d: VarDictionary = Dictionary::new();
        d.set("band",        r.band as i64);
        let string_label = GString::from(&r.string_label);
        d.set("string",      &string_label);
        d.set("frequency",   r.raw_freq as f64);
        d.set("periodicity", r.periodicity as f64);
        if let Some(ref note) = r.note {
            let note_str = GString::from(note.display().as_str());
            d.set("note",  &note_str);
            d.set("cents", note.cents as f64);
        } else {
            let empty = GString::new();
            d.set("note",  &empty);
            d.set("cents", 0.0f64);
        }
        out.push(&d.to_variant());
    }
    out
}
