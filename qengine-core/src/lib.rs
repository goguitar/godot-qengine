//! `qengine-core` – guitar pitch detection built on top of the cycfi/Q FFI.
//!
//! # Architecture
//!
//! ```text
//! ┌─────────────────────────────────────────────────┐
//! │  Audio thread (Godot)                           │
//! │   AudioEffectInstanceQEngine                    │
//! │     ↓  pushes f32 samples (lock-free SPSC)      │
//! └─────────────────────────────────────────────────┘
//!              ↓  rtrb Producer → Consumer ↓
//! ┌─────────────────────────────────────────────────┐
//! │  Main thread (GDScript _process)               │
//! │   AudioEffectQEngine::poll_notes()              │
//! │     → BandDetector::process_buffer()            │
//! │       → Q::GuitarDetector (6 bands via FFI)     │
//! │       ← Vec<DetectionResult>                    │
//! │     → NoteEvent SPSC queue                      │
//! │   QEngineDetectorNode::get_notes() → GDScript   │
//! └─────────────────────────────────────────────────┘
//! ```

pub mod note;
pub mod tuning;
pub mod ring_buffer;
pub mod detector;

pub use note::{DetectedNote, NOTE_NAMES};
pub use tuning::{Tuning, TuningId};
pub use ring_buffer::AudioRingBuffer;
pub use detector::{BandDetector, DetectionResult};
