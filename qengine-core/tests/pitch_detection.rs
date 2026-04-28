//! Integration tests for multi-band pitch detection.
//!
//! Synthetic test signals (pure sine waves) stand in for real guitar WAV
//! files so the test suite runs without external data.  When the
//! GuitarSet dataset is available (see README), set the environment variable
//! `QENGINE_DATASET_DIR` to the path of the `audio/mic` folder and the
//! dataset tests will run automatically.

use qengine_core::ring_buffer::ProducerExt;
use qengine_core::tuning::TuningId;
use qengine_core::BandDetector;
use std::f32::consts::PI;

/// Generate a pure sine wave at `freq` Hz.
fn sine_wave(freq: f32, sample_rate: f32, seconds: f32) -> Vec<f32> {
    let n = (sample_rate * seconds) as usize;
    (0..n)
        .map(|i| (2.0 * PI * freq * i as f32 / sample_rate).sin())
        .collect()
}

// ---------------------------------------------------------------------------
// Helper: run the detector on a sine wave and return the band result.
// ---------------------------------------------------------------------------
fn detect_sine(
    freq: f32,
    tuning: TuningId,
    expected_band: usize,
) -> (f32, Option<String>) {
    let sr = 44100.0;
    let (mut det, mut prod) = BandDetector::new(sr, tuning);
    let buf = sine_wave(freq, sr, 1.0);
    prod.push_samples(&buf);
    let results = det.process();
    let r = &results[expected_band];
    (r.raw_freq, r.note.as_ref().map(|n| n.display()))
}

// ===========================================================================
// E Standard open strings
// ===========================================================================

#[test]
fn standard_e2_open_string() {
    let (freq, note) = detect_sine(82.41, TuningId::Standard, 0);
    assert!((freq - 82.41).abs() < 2.0, "E2 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("E2"));
}

#[test]
fn standard_a2_open_string() {
    let (freq, note) = detect_sine(110.0, TuningId::Standard, 1);
    assert!((freq - 110.0).abs() < 2.0, "A2 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("A2"));
}

#[test]
fn standard_d3_open_string() {
    let (freq, note) = detect_sine(146.83, TuningId::Standard, 2);
    assert!((freq - 146.83).abs() < 2.0, "D3 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("D3"));
}

#[test]
fn standard_g3_open_string() {
    let (freq, note) = detect_sine(196.0, TuningId::Standard, 3);
    assert!((freq - 196.0).abs() < 2.0, "G3 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("G3"));
}

#[test]
fn standard_b3_open_string() {
    let (freq, note) = detect_sine(246.94, TuningId::Standard, 4);
    assert!((freq - 246.94).abs() < 2.0, "B3 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("B3"));
}

#[test]
fn standard_e4_open_string() {
    let (freq, note) = detect_sine(329.63, TuningId::Standard, 5);
    assert!((freq - 329.63).abs() < 2.0, "E4 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("E4"));
}

// ===========================================================================
// Drop D open strings
// ===========================================================================

#[test]
fn drop_d_low_string() {
    let (freq, note) = detect_sine(73.42, TuningId::DropD, 0);
    assert!((freq - 73.42).abs() < 2.0, "D2 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("D2"));
}

#[test]
fn drop_d_high_strings_unchanged() {
    // Strings 2–6 are the same as Standard in Drop D
    let standard_freqs = [110.0, 146.83, 196.0, 246.94, 329.63];
    let expected_notes = ["A2", "D3", "G3", "B3", "E4"];
    for (i, (&freq, &expected)) in standard_freqs.iter().zip(expected_notes.iter()).enumerate() {
        let (_, note) = detect_sine(freq, TuningId::DropD, i + 1);
        assert_eq!(
            note.as_deref(),
            Some(expected),
            "Drop D band {}: expected {expected}, got {:?}", i + 1, note
        );
    }
}

// ===========================================================================
// Drop C open strings
// ===========================================================================

#[test]
fn drop_c_low_string_c2() {
    let (freq, note) = detect_sine(65.41, TuningId::DropC, 0);
    assert!((freq - 65.41).abs() < 2.0, "C2 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("C2"));
}

// ===========================================================================
// DADGAD open strings
// ===========================================================================

#[test]
fn dadgad_low_d2() {
    let (freq, _note) = detect_sine(73.42, TuningId::Dadgad, 0);
    assert!((freq - 73.42).abs() < 2.0, "DADGAD D2 freq: got {freq:.2}");
}

// ===========================================================================
// Fretted notes (E Standard, first position)
// ===========================================================================

#[test]
fn standard_first_fret_e2_string_f2() {
    // Low E string, fret 1 → F2 (87.31 Hz)
    let (freq, note) = detect_sine(87.31, TuningId::Standard, 0);
    assert!((freq - 87.31).abs() < 2.0, "F2 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("F2"));
}

#[test]
fn standard_twelfth_fret_e2_string_e3() {
    // Low E string, fret 12 → E3 (164.81 Hz – one octave up from E2)
    let (freq, note) = detect_sine(164.81, TuningId::Standard, 0);
    assert!((freq - 164.81).abs() < 2.0, "E3 freq: got {freq:.2}");
    assert_eq!(note.as_deref(), Some("E3"));
}

// ===========================================================================
// Em chord detection
//
// An Em (E minor) chord contains three pitch classes:
//   E – root
//   G – minor third
//   B – perfect fifth
//
// Open Em on guitar (Standard tuning) — one note per string band:
//   Band 0 → E2  (82.41 Hz)  — low E string, open
//   Band 1 → B2 (123.47 Hz)  — A string, 2nd fret
//   Band 2 → E3 (164.81 Hz)  — D string, 2nd fret
//   Band 3 → G3 (196.00 Hz)  — G string, open
//   Band 4 → B3 (246.94 Hz)  — B string, open
//   Band 5 → E4 (329.63 Hz)  — high E string, open
//
// Q's pitch_detector is a per-band monophonic algorithm designed for
// hexaphonic (one pickup per string) use.  Each string's frequency is
// therefore validated through its dedicated band; the chord test then asserts
// that the union of detected note classes equals {E, G, B}.
// ===========================================================================

/// (frequency_hz, band_index, expected_note_display)
const EM_CHORD_STRINGS: &[(f32, usize, &str)] = &[
    (82.41,  0, "E2"),
    (123.47, 1, "B2"),
    (164.81, 2, "E3"),
    (196.00, 3, "G3"),
    (246.94, 4, "B3"),
    (329.63, 5, "E4"),
];

/// Extract the pitch-class letter(s) from a note-display string.
/// "E2" → "E",  "G3" → "G",  "C#4" → "C#"
fn pitch_class(note_display: &str) -> String {
    let octave_start = note_display
        .find(|c: char| c.is_ascii_digit())
        .unwrap_or(note_display.len());
    note_display[..octave_start].to_string()
}

/// Test 1 – three unique pitch classes (E, G, B) are each detected.
///
/// Feeds one representative frequency per Em pitch class through its
/// dedicated band and asserts all three are identified.
#[test]
fn em_chord_three_pitch_classes_detected() {
    // E2 (band 0) → "E",  G3 (band 3) → "G",  B3 (band 4) → "B"
    let em_three: &[(f32, usize)] = &[
        (82.41,  0),
        (196.00, 3),
        (246.94, 4),
    ];

    let mut detected: Vec<String> = Vec::new();
    for &(freq, band) in em_three {
        let (_, note) = detect_sine(freq, TuningId::Standard, band);
        if let Some(display) = note {
            detected.push(pitch_class(&display));
        }
    }

    for expected_class in &["E", "G", "B"] {
        assert!(
            detected.iter().any(|n| n == expected_class),
            "Em chord: pitch class '{expected_class}' not detected; got {detected:?}"
        );
    }
}

/// Test 2 – every string of the open Em chord returns the correct note name.
///
/// Processes all six string frequencies through their dedicated bands and
/// checks the exact note display (e.g. "E2", "B2", …).
#[test]
fn em_chord_open_six_strings_per_band() {
    for &(freq, band, expected_display) in EM_CHORD_STRINGS {
        let (raw_freq, note) = detect_sine(freq, TuningId::Standard, band);
        assert_eq!(
            note.as_deref(),
            Some(expected_display),
            "Em open chord band {band} ({freq} Hz): expected {expected_display}, \
             got {note:?} (raw: {raw_freq:.2} Hz)"
        );
    }
}

/// Test 3 – the union of note classes across all six Em strings is exactly {E, G, B}.
///
/// Collects the pitch-class letter from every band result and asserts the set
/// equals the Em triad, confirming no extraneous notes are reported.
#[test]
fn em_chord_note_classes_are_exactly_e_g_b() {
    let detected: std::collections::HashSet<String> = EM_CHORD_STRINGS
        .iter()
        .filter_map(|&(freq, band, _)| {
            let (_, note_opt) = detect_sine(freq, TuningId::Standard, band);
            note_opt.map(|display| pitch_class(&display))
        })
        .collect();

    let expected: std::collections::HashSet<String> =
        ["E", "G", "B"].iter().map(|&s| s.to_string()).collect();

    assert_eq!(
        detected, expected,
        "Em chord: expected pitch classes {{E, G, B}}, got {detected:?}"
    );
}

// ===========================================================================
// Dataset test (skipped unless env-var is set)
// ===========================================================================

/// Load a WAV file and run detection.
/// Requires `hound` as a dev-dependency; only compiled when the test file exists.
#[cfg(feature = "dataset_tests")]
mod dataset {
    use super::*;
    use std::path::PathBuf;

    fn dataset_dir() -> Option<PathBuf> {
        std::env::var("QENGINE_DATASET_DIR").ok().map(PathBuf::from)
    }

    /// Read a mono WAV as f32 samples.
    fn read_wav_mono(path: &std::path::Path) -> Vec<f32> {
        let mut reader = hound::WavReader::open(path).expect("open wav");
        let spec = reader.spec();
        match spec.sample_format {
            hound::SampleFormat::Float => reader
                .samples::<f32>()
                .map(|s| s.unwrap())
                .collect(),
            hound::SampleFormat::Int => {
                let max = (1i64 << (spec.bits_per_sample - 1)) as f32;
                reader
                    .samples::<i32>()
                    .map(|s| s.unwrap() as f32 / max)
                    .collect()
            }
        }
    }

    #[test]
    #[ignore = "needs QENGINE_DATASET_DIR env-var pointing to guitarset/audio/mic"]
    fn guitarset_e_standard_open_strings() {
        let dir = match dataset_dir() {
            Some(d) => d,
            None => {
                eprintln!("QENGINE_DATASET_DIR not set – skipping dataset test");
                return;
            }
        };

        // Expected mapping: file-prefix → (expected_note, band_index)
        // The GuitarSet mic dataset files contain single-note recordings.
        let cases: &[(&str, &str, usize)] = &[
            ("E2_", "E2", 0),
            ("A2_", "A2", 1),
            ("D3_", "D3", 2),
            ("G3_", "G3", 3),
            ("B3_", "B3", 4),
            ("E4_", "E4", 5),
        ];

        for &(prefix, expected_note, band) in cases {
            let wav_files: Vec<_> = std::fs::read_dir(&dir)
                .expect("read dataset dir")
                .filter_map(|e| {
                    let e = e.ok()?;
                    let name = e.file_name().to_string_lossy().to_string();
                    if name.starts_with(prefix) && name.ends_with(".wav") {
                        Some(e.path())
                    } else {
                        None
                    }
                })
                .collect();

            assert!(
                !wav_files.is_empty(),
                "No WAV files found for prefix {prefix} in {dir:?}"
            );

            for wav in &wav_files {
                let samples = read_wav_mono(wav);
                let (mut det, mut prod) = BandDetector::new(44100.0, TuningId::Standard);
                prod.push_samples(&samples);
                let results = det.process();
                let note = results[band].note.as_ref().map(|n| n.display());
                assert_eq!(
                    note.as_deref(),
                    Some(expected_note),
                    "File {:?}: expected {expected_note}, got {note:?}",
                    wav.file_name()
                );
            }
        }
    }
}
