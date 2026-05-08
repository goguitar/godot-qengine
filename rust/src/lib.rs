use godot::classes::{INode, Node};
use godot::prelude::*;

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct QEngineBandRange {
    freq_min: f32,
    freq_max: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct QEngineDetectionFrame {
    time_sec: f64,
    pitch_hz: f32,
    midi_float: f32,
    midi_note: i32,
    confidence: f32,
    level: f32,
    onset: u8,
    pitch_valid: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct QEngineNoteEvent {
    time_sec: f64,
    pitch_hz: f32,
    midi_note: i32,
    confidence: f32,
    level: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct QEngineStringComponent {
    band: i32,
    pitch_hz: f32,
    midi_float: f32,
    midi_note: i32,
    confidence: f32,
    cents: f32,
    active: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct QEngineChordFrame {
    time_sec: f64,
    level: f32,
    strings: [QEngineStringComponent; 6],
    dominant_band: i32,
    dominant_midi: i32,
    dominant_pitch_hz: f32,
    dominant_confidence: f32,
    active_count: i32,
}

enum QEngineHandle {}

unsafe extern "C" {
    fn qengine_create(
        sample_rate: f32,
        threshold_db: f32,
        min_periodicity: f32,
        ranges: *const QEngineBandRange,
        range_count: usize,
    ) -> *mut QEngineHandle;
    fn qengine_destroy(handle: *mut QEngineHandle);
    fn qengine_reset(handle: *mut QEngineHandle);
    fn qengine_set_min_periodicity(handle: *mut QEngineHandle, v: f32);
    fn qengine_push_audio(handle: *mut QEngineHandle, samples: *const f32, count: usize);
    fn qengine_process(handle: *mut QEngineHandle);
    fn qengine_get_latest_detection(
        handle: *mut QEngineHandle,
        out_frame: *mut QEngineDetectionFrame,
    ) -> i32;
    fn qengine_pop_note_events(
        handle: *mut QEngineHandle,
        out_events: *mut QEngineNoteEvent,
        max_events: usize,
    ) -> usize;
    fn qengine_get_recent_frames(
        handle: *mut QEngineHandle,
        out_frames: *mut QEngineDetectionFrame,
        max_frames: usize,
    ) -> usize;
    fn qengine_pop_chord_frames(
        handle: *mut QEngineHandle,
        out_frames: *mut QEngineChordFrame,
        max_frames: usize,
    ) -> usize;
    fn qengine_get_latest_chord_frame(
        handle: *mut QEngineHandle,
        out_frame: *mut QEngineChordFrame,
    ) -> i32;
}

struct NativeDetector {
    raw: *mut QEngineHandle,
}

unsafe impl Send for NativeDetector {}
unsafe impl Sync for NativeDetector {}

impl Drop for NativeDetector {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { qengine_destroy(self.raw) };
            self.raw = std::ptr::null_mut();
        }
    }
}

impl NativeDetector {
    fn create(sample_rate: f32, threshold_db: f32, min_periodicity: f32, ranges: &[QEngineBandRange; 6]) -> Option<Self> {
        let raw = unsafe {
            qengine_create(
                sample_rate,
                threshold_db,
                min_periodicity,
                ranges.as_ptr(),
                ranges.len(),
            )
        };
        if raw.is_null() {
            return None;
        }
        Some(Self { raw })
    }

    fn process(&self) {
        unsafe { qengine_process(self.raw) };
    }

    fn push_audio(&self, samples: &[f32]) {
        if samples.is_empty() {
            return;
        }
        unsafe { qengine_push_audio(self.raw, samples.as_ptr(), samples.len()) };
    }

    fn reset(&self) {
        unsafe { qengine_reset(self.raw) };
    }

    fn set_min_periodicity(&self, v: f32) {
        unsafe { qengine_set_min_periodicity(self.raw, v) };
    }

    fn latest_detection(&self) -> QEngineDetectionFrame {
        let mut frame = QEngineDetectionFrame::default();
        unsafe {
            qengine_get_latest_detection(self.raw, &mut frame);
        }
        frame
    }

    fn latest_chord(&self) -> QEngineChordFrame {
        let mut frame = QEngineChordFrame::default();
        unsafe {
            qengine_get_latest_chord_frame(self.raw, &mut frame);
        }
        frame
    }

    fn pop_note_events(&self, max: usize) -> Vec<QEngineNoteEvent> {
        if max == 0 {
            return Vec::new();
        }
        let mut events = vec![QEngineNoteEvent::default(); max];
        let got = unsafe { qengine_pop_note_events(self.raw, events.as_mut_ptr(), max) };
        events.truncate(got);
        events
    }

    fn get_recent_frames(&self, max: usize) -> Vec<QEngineDetectionFrame> {
        if max == 0 {
            return Vec::new();
        }
        let mut frames = vec![QEngineDetectionFrame::default(); max];
        let got = unsafe { qengine_get_recent_frames(self.raw, frames.as_mut_ptr(), max) };
        frames.truncate(got);
        frames
    }

    fn pop_chord_frames(&self, max: usize) -> Vec<QEngineChordFrame> {
        if max == 0 {
            return Vec::new();
        }
        let mut frames = vec![QEngineChordFrame::default(); max];
        let got = unsafe { qengine_pop_chord_frames(self.raw, frames.as_mut_ptr(), max) };
        frames.truncate(got);
        frames
    }
}

fn ranges_from_packed(ranges: &PackedFloat32Array) -> Option<[QEngineBandRange; 6]> {
    if ranges.len() < 12 {
        return None;
    }

    let mut out = [QEngineBandRange::default(); 6];
    for i in 0..6 {
        out[i] = QEngineBandRange {
            freq_min: ranges.get(i * 2).unwrap_or(0.0),
            freq_max: ranges.get(i * 2 + 1).unwrap_or(0.0),
        };
    }
    Some(out)
}

fn detection_dict(frame: QEngineDetectionFrame) -> VarDictionary {
    let mut d = VarDictionary::new();
    d.set("time_sec", frame.time_sec);
    d.set("pitch_hz", frame.pitch_hz as f64);
    d.set("midi_note", frame.midi_note);
    d.set("midi_float", frame.midi_float as f64);
    d.set("confidence", frame.confidence as f64);
    d.set("level", frame.level as f64);
    d.set("onset", frame.onset != 0);
    d.set("pitch_valid", frame.pitch_valid != 0);
    d
}

fn note_event_dict(ev: QEngineNoteEvent) -> VarDictionary {
    let mut d = VarDictionary::new();
    d.set("time_sec", ev.time_sec);
    d.set("pitch_hz", ev.pitch_hz as f64);
    d.set("midi_note", ev.midi_note);
    d.set("confidence", ev.confidence as f64);
    d.set("level", ev.level as f64);
    d
}

fn string_component_dict(sc: QEngineStringComponent) -> VarDictionary {
    let mut d = VarDictionary::new();
    d.set("band", sc.band);
    d.set("pitch_hz", sc.pitch_hz as f64);
    d.set("midi_float", sc.midi_float as f64);
    d.set("midi_note", sc.midi_note);
    d.set("confidence", sc.confidence as f64);
    d.set("cents", sc.cents as f64);
    d.set("active", sc.active != 0);
    d
}

fn chord_frame_dict(cf: QEngineChordFrame) -> VarDictionary {
    let mut d = VarDictionary::new();
    d.set("time_sec", cf.time_sec);
    d.set("level", cf.level as f64);
    d.set("dominant_band", cf.dominant_band);
    d.set("dominant_midi", cf.dominant_midi);
    d.set("dominant_pitch_hz", cf.dominant_pitch_hz as f64);
    d.set("dominant_confidence", cf.dominant_confidence as f64);
    d.set("active_count", cf.active_count);

    let mut strings: Array<VarDictionary> = Array::new();
    for sc in cf.strings {
        strings.push(&string_component_dict(sc));
    }
    d.set("strings", strings);
    d
}

fn chord_row(chord: QEngineChordFrame, min_periodicity: f32) -> VarDictionary {
    let mut seen = [false; 12];
    let note_names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    let mut chord_notes = PackedStringArray::new();

    for sc in chord.strings {
        if sc.midi_note < 0 || sc.confidence < min_periodicity || sc.pitch_hz <= 0.0 {
            continue;
        }
        let cls = ((sc.midi_note % 12) + 12) % 12;
        if !seen[cls as usize] {
            chord_notes.push(note_names[cls as usize]);
            seen[cls as usize] = true;
        }
    }

    let mut d = VarDictionary::new();
    d.set("band", 6);
    d.set("kind", "chord");
    d.set("frequency", 0.0f64);
    d.set("periodicity", 0.0f64);
    d.set("midi_note", -1);
    d.set("cents", 0.0f64);
    d.set("chord_notes", chord_notes);
    d
}

#[derive(GodotClass)]
#[class(base=Node)]
struct QEngineDetectorNode {
    base: Base<Node>,
    sample_rate: f32,
    threshold_db: f32,
    min_periodicity: f32,
    band_ranges: PackedFloat32Array,
    auto_poll: bool,
    detector: Option<NativeDetector>,
}

#[godot_api]
impl INode for QEngineDetectorNode {
    fn init(base: Base<Node>) -> Self {
        Self {
            base,
            sample_rate: 44100.0,
            threshold_db: -45.0,
            min_periodicity: 0.85,
            band_ranges: PackedFloat32Array::new(),
            auto_poll: true,
            detector: None,
        }
    }

    fn ready(&mut self) {
        self.init_detector();
    }

    fn process(&mut self, _delta: f64) {
        if self.auto_poll {
            self.poll_notes();
        }
    }
}

#[godot_api]
impl QEngineDetectorNode {
    #[func]
    fn init_detector(&mut self) {
        let ranges = match ranges_from_packed(&self.band_ranges) {
            Some(r) => r,
            None => {
                self.detector = None;
                return;
            }
        };

        self.detector = NativeDetector::create(
            self.sample_rate,
            self.threshold_db,
            self.min_periodicity,
            &ranges,
        );
    }

    #[func]
    fn push_samples(&mut self, samples: PackedFloat32Array) {
        if let Some(detector) = &self.detector {
            detector.push_audio(samples.as_slice());
        }
    }

    #[func]
    fn poll_notes(&mut self) -> Array<VarDictionary> {
        let mut out: Array<VarDictionary> = Array::new();
        let Some(detector) = &self.detector else {
            return out;
        };

        detector.process();
        let chord = detector.latest_chord();

        for sc in chord.strings {
            let mut d = VarDictionary::new();
            d.set("band", sc.band);
            d.set("frequency", sc.pitch_hz as f64);
            d.set("periodicity", sc.confidence as f64);
            d.set("midi_note", sc.midi_note);
            d.set("cents", sc.cents as f64);
            out.push(&d);
        }
        out.push(&chord_row(chord, self.min_periodicity));
        out
    }

    #[func]
    fn reset(&mut self) {
        if let Some(detector) = &self.detector {
            detector.reset();
        }
    }

    #[func]
    fn get_latest_detection(&self) -> VarDictionary {
        match &self.detector {
            Some(detector) => detection_dict(detector.latest_detection()),
            None => detection_dict(QEngineDetectionFrame::default()),
        }
    }

    #[func]
    fn pop_note_events(&mut self) -> Array<VarDictionary> {
        let mut out: Array<VarDictionary> = Array::new();
        if let Some(detector) = &self.detector {
            for ev in detector.pop_note_events(128) {
                out.push(&note_event_dict(ev));
            }
        }
        out
    }

    #[func]
    fn get_frame_history(&mut self, count: i32) -> Array<VarDictionary> {
        let mut out: Array<VarDictionary> = Array::new();
        if count <= 0 {
            return out;
        }
        if let Some(detector) = &self.detector {
            for frame in detector.get_recent_frames(count as usize) {
                out.push(&detection_dict(frame));
            }
        }
        out
    }

    #[func]
    fn pop_chord_frames(&mut self) -> Array<VarDictionary> {
        let mut out: Array<VarDictionary> = Array::new();
        if let Some(detector) = &self.detector {
            for frame in detector.pop_chord_frames(128) {
                out.push(&chord_frame_dict(frame));
            }
        }
        out
    }

    #[func]
    fn set_sample_rate(&mut self, v: f64) {
        self.sample_rate = v as f32;
    }

    #[func]
    fn get_sample_rate(&self) -> f64 {
        self.sample_rate as f64
    }

    #[func]
    fn set_threshold_db(&mut self, v: f64) {
        self.threshold_db = v as f32;
    }

    #[func]
    fn get_threshold_db(&self) -> f64 {
        self.threshold_db as f64
    }

    #[func]
    fn set_min_periodicity(&mut self, v: f64) {
        self.min_periodicity = v as f32;
        if let Some(detector) = &self.detector {
            detector.set_min_periodicity(self.min_periodicity);
        }
    }

    #[func]
    fn get_min_periodicity(&self) -> f64 {
        self.min_periodicity as f64
    }

    #[func]
    fn set_band_ranges(&mut self, v: PackedFloat32Array) {
        self.band_ranges = v;
        self.init_detector();
    }

    #[func]
    fn get_band_ranges(&self) -> PackedFloat32Array {
        self.band_ranges.clone()
    }

    #[func]
    fn set_auto_poll(&mut self, v: bool) {
        self.auto_poll = v;
    }

    #[func]
    fn get_auto_poll(&self) -> bool {
        self.auto_poll
    }
}

#[derive(GodotClass)]
#[class(base=Node)]
struct AudioEffectQEngine {
    base: Base<Node>,
    sample_rate: f32,
    threshold_db: f32,
    min_periodicity: f32,
    band_ranges: PackedFloat32Array,
    detector: Option<NativeDetector>,
}

#[godot_api]
impl INode for AudioEffectQEngine {
    fn init(base: Base<Node>) -> Self {
        Self {
            base,
            sample_rate: 44100.0,
            threshold_db: -45.0,
            min_periodicity: 0.85,
            band_ranges: PackedFloat32Array::new(),
            detector: None,
        }
    }
}

#[godot_api]
impl AudioEffectQEngine {
    fn ensure_detector(&mut self) {
        if self.detector.is_some() {
            return;
        }
        let Some(ranges) = ranges_from_packed(&self.band_ranges) else {
            return;
        };
        self.detector = NativeDetector::create(
            self.sample_rate,
            self.threshold_db,
            self.min_periodicity,
            &ranges,
        );
    }

    #[func]
    fn push_samples(&mut self, samples: PackedFloat32Array) {
        self.ensure_detector();
        if let Some(detector) = &self.detector {
            detector.push_audio(samples.as_slice());
        }
    }

    #[func]
    fn poll_notes(&mut self) -> Array<VarDictionary> {
        self.ensure_detector();

        let mut out: Array<VarDictionary> = Array::new();
        let Some(detector) = &self.detector else {
            return out;
        };

        detector.process();
        let chord = detector.latest_chord();
        for sc in chord.strings {
            let mut d = VarDictionary::new();
            d.set("band", sc.band);
            d.set("frequency", sc.pitch_hz as f64);
            d.set("periodicity", sc.confidence as f64);
            d.set("midi_note", sc.midi_note);
            d.set("cents", sc.cents as f64);
            out.push(&d);
        }
        out.push(&chord_row(chord, self.min_periodicity));
        out
    }

    #[func]
    fn reset(&mut self) {
        if let Some(detector) = &self.detector {
            detector.reset();
        }
    }

    #[func]
    fn get_latest_detection(&mut self) -> VarDictionary {
        self.ensure_detector();
        match &self.detector {
            Some(detector) => detection_dict(detector.latest_detection()),
            None => detection_dict(QEngineDetectionFrame::default()),
        }
    }

    #[func]
    fn pop_note_events(&mut self) -> Array<VarDictionary> {
        self.ensure_detector();
        let mut out: Array<VarDictionary> = Array::new();
        if let Some(detector) = &self.detector {
            for ev in detector.pop_note_events(128) {
                out.push(&note_event_dict(ev));
            }
        }
        out
    }

    #[func]
    fn get_frame_history(&mut self, count: i32) -> Array<VarDictionary> {
        self.ensure_detector();

        let mut out: Array<VarDictionary> = Array::new();
        if count <= 0 {
            return out;
        }

        if let Some(detector) = &self.detector {
            for frame in detector.get_recent_frames(count as usize) {
                out.push(&detection_dict(frame));
            }
        }
        out
    }

    #[func]
    fn pop_chord_frames(&mut self) -> Array<VarDictionary> {
        self.ensure_detector();

        let mut out: Array<VarDictionary> = Array::new();
        if let Some(detector) = &self.detector {
            for frame in detector.pop_chord_frames(128) {
                out.push(&chord_frame_dict(frame));
            }
        }
        out
    }

    #[func]
    fn set_sample_rate(&mut self, v: f64) {
        self.sample_rate = v as f32;
        self.detector = None;
    }

    #[func]
    fn get_sample_rate(&self) -> f64 {
        self.sample_rate as f64
    }

    #[func]
    fn set_threshold_db(&mut self, v: f64) {
        self.threshold_db = v as f32;
        self.detector = None;
    }

    #[func]
    fn get_threshold_db(&self) -> f64 {
        self.threshold_db as f64
    }

    #[func]
    fn set_min_periodicity(&mut self, v: f64) {
        self.min_periodicity = v as f32;
        if let Some(detector) = &self.detector {
            detector.set_min_periodicity(self.min_periodicity);
        }
    }

    #[func]
    fn get_min_periodicity(&self) -> f64 {
        self.min_periodicity as f64
    }

    #[func]
    fn set_band_ranges(&mut self, v: PackedFloat32Array) {
        self.band_ranges = v;
        self.detector = None;
    }

    #[func]
    fn get_band_ranges(&self) -> PackedFloat32Array {
        self.band_ranges.clone()
    }
}

struct QEngineRustExtension;

#[gdextension]
unsafe impl ExtensionLibrary for QEngineRustExtension {}
