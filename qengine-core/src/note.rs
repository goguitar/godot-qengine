//! Musical note utilities: frequency ↔ note name/MIDI mapping.

/// The 12 chromatic note names (C = index 0).
pub const NOTE_NAMES: [&str; 12] =
    ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/// A detected note with all relevant information.
#[derive(Debug, Clone, PartialEq)]
pub struct DetectedNote {
    /// Chromatic note name, e.g. `"E"`, `"A"`, `"C#"`.
    pub name: &'static str,
    /// Octave number (A4 = octave 4).
    pub octave: i32,
    /// MIDI note number (A4 = 69).
    pub midi: i32,
    /// Exact equal-temperament frequency for this MIDI note.
    pub exact_freq: f32,
    /// Raw detected frequency from the pitch detector.
    pub detected_freq: f32,
    /// Deviation from the nearest equal-temperament pitch in cents.
    pub cents: f32,
}

impl DetectedNote {
    /// Build a `DetectedNote` from a raw detected frequency.
    ///
    /// Returns `None` if `freq` is not a valid guitar pitch.
    pub fn from_frequency(freq: f32) -> Option<Self> {
        if freq <= 0.0 || !freq.is_finite() {
            return None;
        }
        // MIDI = 69 + 12·log₂(f / 440)
        let midi_f = 69.0 + 12.0 * (freq / 440.0_f32).log2();
        let midi = midi_f.round() as i32;
        if !(0..=127).contains(&midi) {
            return None;
        }
        let note_idx = ((midi % 12) + 12) as usize % 12;
        let octave   = midi / 12 - 1;
        let exact    = 440.0 * 2.0_f32.powf((midi as f32 - 69.0) / 12.0);
        let cents    = (midi_f - midi as f32) * 100.0;
        Some(DetectedNote {
            name:          NOTE_NAMES[note_idx],
            octave,
            midi,
            exact_freq:    exact,
            detected_freq: freq,
            cents,
        })
    }

    /// Human-readable note string, e.g. `"E2"`.
    pub fn display(&self) -> String {
        format!("{}{}", self.name, self.octave)
    }
}

/// Convert a MIDI note number to its equal-temperament frequency.
#[inline]
pub fn midi_to_freq(midi: i32) -> f32 {
    440.0 * 2.0_f32.powf((midi as f32 - 69.0) / 12.0)
}

/// Convert a frequency to the nearest MIDI note number.
#[inline]
pub fn freq_to_midi(freq: f32) -> Option<i32> {
    if freq <= 0.0 || !freq.is_finite() {
        return None;
    }
    Some((69.0 + 12.0 * (freq / 440.0_f32).log2()).round() as i32)
}

/* =========================================================================
 * Tests
 * ====================================================================== */

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a4_round_trip() {
        let note = DetectedNote::from_frequency(440.0).unwrap();
        assert_eq!(note.name, "A");
        assert_eq!(note.octave, 4);
        assert_eq!(note.midi, 69);
        assert!((note.cents).abs() < 0.01);
    }

    #[test]
    fn e2_identification() {
        let note = DetectedNote::from_frequency(82.41).unwrap();
        assert_eq!(note.name, "E");
        assert_eq!(note.octave, 2);
    }

    #[test]
    fn midi_freq_round_trip() {
        for midi in 24..=96 {
            let freq = midi_to_freq(midi);
            let back = freq_to_midi(freq).unwrap();
            assert_eq!(back, midi, "round-trip failed for MIDI {midi}");
        }
    }
}
