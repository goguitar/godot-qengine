//! Guitar tuning definitions – maps `TuningId` to the corresponding Q FFI
//! constant and provides human-readable metadata for each of the 6 strings.

use qengine_sys::{
    Q_TUNING_DADGAD, Q_TUNING_DROP_C, Q_TUNING_DROP_D, Q_TUNING_OPEN_D, Q_TUNING_STANDARD,
};

/// Supported guitar tunings.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum TuningId {
    /// E A D G B E  (standard E tuning)
    #[default]
    Standard,
    /// D A D G B E  (Drop D)
    DropD,
    /// D A D F# A D  (Open D)
    OpenD,
    /// C G C F A D  (Drop C)
    DropC,
    /// D A D G A D
    Dadgad,
}

impl TuningId {
    /// Convert to the C integer constant expected by `q_guitar_detector_new`.
    pub fn to_c_id(self) -> i32 {
        match self {
            TuningId::Standard => Q_TUNING_STANDARD,
            TuningId::DropD    => Q_TUNING_DROP_D,
            TuningId::OpenD    => Q_TUNING_OPEN_D,
            TuningId::DropC    => Q_TUNING_DROP_C,
            TuningId::Dadgad   => Q_TUNING_DADGAD,
        }
    }

    /// Parse from a case-insensitive string slice.
    pub fn from_str(s: &str) -> Option<Self> {
        match s.to_lowercase().replace(['-', '_', ' '], "").as_str() {
            "standard" | "estandard" => Some(TuningId::Standard),
            "dropd" | "droped"      => Some(TuningId::DropD),
            "opend"                 => Some(TuningId::OpenD),
            "dropc"                 => Some(TuningId::DropC),
            "dadgad"                => Some(TuningId::Dadgad),
            _                       => None,
        }
    }

    /// Short human-readable name.
    pub fn name(self) -> &'static str {
        match self {
            TuningId::Standard => "Standard",
            TuningId::DropD    => "Drop D",
            TuningId::OpenD    => "Open D",
            TuningId::DropC    => "Drop C",
            TuningId::Dadgad   => "DADGAD",
        }
    }
}

/// Open-string metadata for one guitar string.
#[derive(Debug, Clone)]
pub struct StringInfo {
    /// 1-based string number (1 = highest pitch, 6 = lowest).
    pub number:    usize,
    /// Open-string note name, e.g. `"E"`, `"A"`, `"D#"`.
    pub note_name: &'static str,
    /// Open-string octave.
    pub octave:    i32,
    /// Open-string frequency in Hz.
    pub open_hz:   f32,
}

/// Complete tuning descriptor: 6 strings + name.
#[derive(Debug, Clone)]
pub struct Tuning {
    pub id:      TuningId,
    pub strings: [StringInfo; 6],
}

impl Tuning {
    pub fn get(id: TuningId) -> Self {
        let strings: [StringInfo; 6] = match id {
            TuningId::Standard => [
                StringInfo { number: 6, note_name: "E",  octave: 2, open_hz:  82.41 },
                StringInfo { number: 5, note_name: "A",  octave: 2, open_hz: 110.00 },
                StringInfo { number: 4, note_name: "D",  octave: 3, open_hz: 146.83 },
                StringInfo { number: 3, note_name: "G",  octave: 3, open_hz: 196.00 },
                StringInfo { number: 2, note_name: "B",  octave: 3, open_hz: 246.94 },
                StringInfo { number: 1, note_name: "E",  octave: 4, open_hz: 329.63 },
            ],
            TuningId::DropD => [
                StringInfo { number: 6, note_name: "D",  octave: 2, open_hz:  73.42 },
                StringInfo { number: 5, note_name: "A",  octave: 2, open_hz: 110.00 },
                StringInfo { number: 4, note_name: "D",  octave: 3, open_hz: 146.83 },
                StringInfo { number: 3, note_name: "G",  octave: 3, open_hz: 196.00 },
                StringInfo { number: 2, note_name: "B",  octave: 3, open_hz: 246.94 },
                StringInfo { number: 1, note_name: "E",  octave: 4, open_hz: 329.63 },
            ],
            TuningId::OpenD => [
                StringInfo { number: 6, note_name: "D",  octave: 2, open_hz:  73.42 },
                StringInfo { number: 5, note_name: "A",  octave: 2, open_hz: 110.00 },
                StringInfo { number: 4, note_name: "D",  octave: 3, open_hz: 146.83 },
                StringInfo { number: 3, note_name: "F#", octave: 3, open_hz: 185.00 },
                StringInfo { number: 2, note_name: "A",  octave: 3, open_hz: 220.00 },
                StringInfo { number: 1, note_name: "D",  octave: 4, open_hz: 293.66 },
            ],
            TuningId::DropC => [
                StringInfo { number: 6, note_name: "C",  octave: 2, open_hz:  65.41 },
                StringInfo { number: 5, note_name: "G",  octave: 2, open_hz:  98.00 },
                StringInfo { number: 4, note_name: "C",  octave: 3, open_hz: 130.81 },
                StringInfo { number: 3, note_name: "F",  octave: 3, open_hz: 174.61 },
                StringInfo { number: 2, note_name: "A",  octave: 3, open_hz: 220.00 },
                StringInfo { number: 1, note_name: "D",  octave: 4, open_hz: 293.66 },
            ],
            TuningId::Dadgad => [
                StringInfo { number: 6, note_name: "D",  octave: 2, open_hz:  73.42 },
                StringInfo { number: 5, note_name: "A",  octave: 2, open_hz: 110.00 },
                StringInfo { number: 4, note_name: "D",  octave: 3, open_hz: 146.83 },
                StringInfo { number: 3, note_name: "G",  octave: 3, open_hz: 196.00 },
                StringInfo { number: 2, note_name: "A",  octave: 3, open_hz: 220.00 },
                StringInfo { number: 1, note_name: "D",  octave: 4, open_hz: 293.66 },
            ],
        };
        Tuning { id, strings }
    }
}
