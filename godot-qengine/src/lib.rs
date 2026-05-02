//! Godot 4.5 GDExtension entry-point.
//!
//! Registers:
//! - [`AudioEffectQEngine`]  – `AudioEffectCapture`-based resource for the audio bus
//! - [`QEngineDetectorNode`] – convenience `Node` for GDScript integration

use godot::prelude::*;

mod audio_effect_qengine;
mod note_detector_node;

pub use audio_effect_qengine::AudioEffectQEngine;
pub use note_detector_node::QEngineDetectorNode;

struct GodotQEngineExtension;

#[gdextension]
unsafe impl ExtensionLibrary for GodotQEngineExtension {}
