# godot-qengine

A **Godot 4.4+ GDExtension** written in C++ that adds real-time guitar/bass
pitch detection to any Godot project.  
Detection is powered by the [**cycfi/Q**](https://github.com/cycfi/q)
C++ library, accessed directly (no FFI bridge required).

---

## Architecture

```
Guitar/bass audio
     │
     ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Godot audio bus  (GuitarIn)                                         │
│    AudioEffectQEngine  ←  extends AudioEffectCapture                 │
│      drains capture buffer (stereo → mono)                           │
└─────────────────────────────────────────────────────────────────────┘
                      │ PCM samples
                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│  BandDetector  (C++)                                                  │
│    6 × cycfi::q::pitch_detector  (one per guitar string)             │
│    SPSC audio ring buffer  (audio thread → main thread safe)         │
│                                                                       │
│  Outputs four data tiers via SPSC ring buffers:                      │
│    ① latest DetectionFrame  – best-pitch snapshot                    │
│    ② NoteEvent queue        – onset-triggered events  (SPSC FIFO)   │
│    ③ frame history          – last 128 frames  (SPSC circular log)   │
│    ④ ChordFrame queue       – per-string snapshots  (SPSC FIFO)     │
└─────────────────────────────────────────────────────────────────────┘
          │ ①              │ ②                   │ ③         │ ④
          ▼                ▼                      ▼           ▼
┌─────────────────────────────────────────────────────────────────────┐
│  GDScript / gameplay  (main thread)                                   │
│    Tuner / debug UI     Chart-aware attack       Sustain / bend /    │
│    reads latest_frame   judgment (pop events,    vibrato analysis    │
│                         compare to chart note)   (read frame history)│
│                                                                       │
│    Per-string chord detection (pop_chord_frames):                    │
│      Rocksmith-style active/muted per string, dominant/root note     │
└─────────────────────────────────────────────────────────────────────┘
```

### Design principle

The C++ layer is a **real-time audio analysis engine**, not a gameplay judge.
It answers *"what did the audio observe?"*  
GDScript answers *"does that match what the chart expects?"*

| Layer | Responsibility |
|---|---|
| `AudioEffectQEngine` / `BandDetector` | Pitch detection, onset detection, level tracking, per-string chord data, SPSC state export |
| GDScript gameplay | Chart loading, hit windows, scoring, sustain/bend judgment, chord matching |

### Source layout

| Path | Purpose |
|---|---|
| `src/band_detector.hpp/.cpp` | `BandDetector`, `StringComponent`, `ChordFrame`, `NoteEvent`, `DetectionFrame`, `SPSCEventQueue<T,N>`, `SPSCFrameHistory<T,N>`, `AudioRingBuffer<N>` |
| `src/audio_effect_qengine.hpp/.cpp` | Godot `AudioEffectQEngine` (extends `AudioEffectCapture`) |
| `src/detector_node.hpp/.cpp` | Godot `QEngineDetectorNode` (extends `Node`) |
| `src/register_types.cpp` | GDExtension entry point (`godot_qengine_init`) |
| `tests/test_pitch_detection.cpp` | Standalone C++ tests (no Godot required) |
| `tests/test_wav_pitch_detection.cpp` | Real-audio WAV tests using the GuitarSet Rock subset (`*Rock*.wav`) |
| *(auto-fetched)* | cycfi/Q — downloaded by CMake FetchContent at configure time |
| *(auto-fetched)* | cycfi/infra — downloaded by CMake FetchContent at configure time |

---

## GDScript API

Both `AudioEffectQEngine` and `QEngineDetectorNode` expose the same four-tier
analysis API.

### Tier 1 – Latest detection snapshot

```gdscript
# Dictionary keys:
#   time_sec    float   – elapsed audio time (s)
#   pitch_hz    float   – detected frequency (0 if none)
#   midi_note   int     – nearest MIDI note [0,127]; -1 if none
#   midi_float  float   – fractional MIDI (e.g. 57.35) for fine pitch
#   confidence  float   – Q periodicity [0,1]
#   level       float   – RMS signal level [0,1]
#   onset       bool    – true when a new attack was just detected
#   pitch_valid bool    – true when pitch fields are reliable
var det: Dictionary = fx.get_latest_detection()
if det.pitch_valid:
    print("pitch: %s  %.1f Hz" % [note_name(det.midi_note), det.pitch_hz])
```

### Tier 2 – Onset event queue (chart-aware attack judgment)

```gdscript
# Drain the SPSC NoteEvent ring buffer each frame.
# Each event Dictionary:
#   time_sec, pitch_hz, midi_note, confidence, level
for ev in fx.pop_note_events():
    if abs(ev.time_sec - chart_note.time_sec) < HIT_WINDOW:
        if ev.midi_note == chart_note.midi_note:
            mark_hit()
```

### Tier 3 – Frame history (sustain / bend / vibrato)

```gdscript
# Read up to N recent DetectionFrames (newest first, non-destructive).
# Same keys as get_latest_detection() minus 'onset'.
var history: Array = fx.get_frame_history(30)
var valid_frames := history.filter(func(f): return f.pitch_valid)
if valid_frames.size() > 20:
    var avg_hz = valid_frames.reduce(func(a,b): return a + b.pitch_hz, 0.0) \
                 / valid_frames.size()
    check_bend_target(avg_hz, expected_hz)
```

### Tier 4 – Per-string ChordFrame queue (Rocksmith-style chord detection)

```gdscript
# Drain the SPSC ChordFrame ring buffer each frame.
# Each ChordFrame Dictionary:
#   time_sec             float  – elapsed audio time (s)
#   level                float  – RMS signal level [0,1]
#   dominant_band        int    – string index of highest-confidence string; -1 if none
#   dominant_midi        int    – MIDI note of dominant string; -1 if none
#   dominant_pitch_hz    float  – Hz of dominant string (0 if none)
#   dominant_confidence  float  – Q periodicity of dominant string
#   active_count         int    – number of active strings [0-6]
#   strings              Array  – 6 per-string StringComponent Dicts (index 0 = low E)
#
# Each StringComponent Dictionary:
#   band        int    – string index [0-5]; 0 = lowest string (E2 in Standard)
#   pitch_hz    float  – detected frequency (0 if inactive)
#   midi_float  float  – fractional MIDI; -1 if inactive
#   midi_note   int    – nearest MIDI note; -1 if inactive
#   confidence  float  – Q periodicity [0,1]
#   cents       float  – deviation from nearest semitone [-50, +50]
#   active      bool   – true when detected above min_periodicity threshold

for cf in fx.pop_chord_frames():
    var strings: Array = cf.strings
    for i in 6:
        var sc = strings[i]
        if sc.active:
            print("String %d: %s  %.1f Hz  conf %.0f%%" % [
                i, note_name(sc.midi_note), sc.pitch_hz, sc.confidence * 100.0
            ])
        else:
            print("String %d: muted" % i)
    if cf.dominant_midi >= 0:
        print("Root/dominant: %s" % note_name(cf.dominant_midi))
```

**Example output for an A major chord (A2 + E3 + A3 + C#4 + E4):**

```
String 0: muted
String 1: A2  110.0 Hz  conf 96%
String 2: E3  165.0 Hz  conf 91%
String 3: A3  220.0 Hz  conf 94%
String 4: C#4 277.2 Hz  conf 88%
String 5: E4  329.6 Hz  conf 93%
Root/dominant: A3
```

**Chart-aware chord matching:**

```gdscript
# Compare all expected chart strings against what was detected.
func _judge_chord_frame(cf: Dictionary, chart_chord: Dictionary) -> void:
    var expected_strings: Array = chart_chord.get("strings", [])  # [{midi_note, required}]
    var strings: Array = cf.get("strings", [])
    var all_matched := true
    for i in expected_strings.size():
        var expected: Dictionary = expected_strings[i]
        var detected: Dictionary = strings[i] if i < strings.size() else {}
        if expected.get("required", false):
            var expected_midi: int = expected.get("midi_note", -1)
            var detected_midi: int = detected.get("midi_note", -1)
            var active: bool       = bool(detected.get("active", false))
            if not active or detected_midi != expected_midi:
                all_matched = false
    if all_matched:
        score_chord_hit(cf.dominant_midi, cf.time_sec)
```

### Legacy per-string tuner API (backwards-compatible)

```gdscript
# Returns 7 Dictionaries: indices 0-5 are per-string bands, index 6 is
# a chord summary row.  Keys: band, frequency, periodicity, midi_note, cents.
var notes: Array = fx.poll_notes()
for i in 6:
    print("string %d: %s" % [i, notes[i].get("midi_note", -1)])
```

---

## Supported tunings

| ID | Name | Strings (low → high) |
|---|---|---|
| `Standard` | E Standard | E2 A2 D3 G3 B3 E4 |
| `DropD` | Drop D | D2 A2 D3 G3 B3 E4 |
| `OpenD` | Open D | D2 A2 D3 F#3 A3 D4 |
| `DropC` | Drop C | C2 G2 C3 F3 A3 D4 |
| `DADGAD` | DADGAD | D2 A2 D3 G3 A3 D4 |

---

## Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | GCC 12+, Clang 16+, or MSVC 2022+ (C++20 required) |
| CMake | 3.22+ |
| Godot | 4.4+ |
| git | for cloning the repository |

---

## Getting started

### 1. Clone the repository

```bash
git clone https://github.com/goguitar/godot-qengine.git
cd godot-qengine
```

No submodule init is needed — all C++ dependencies (godot-cpp, cycfi/Q,
cycfi/infra) are downloaded automatically by CMake FetchContent at configure
time.

### 2. Build the GDExtension

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target godot_qengine --config Release
```

The compiled library lands in `build/` **and is automatically copied** to
`demo/addons/qengine/bin/` so the demo project is immediately usable:

| Platform | Build output | Demo addon copy |
|---|---|---|
| Linux | `build/libgodot_qengine.so` | `demo/addons/qengine/bin/libgodot_qengine.so` |
| macOS | `build/libgodot_qengine.dylib` | `demo/addons/qengine/bin/libgodot_qengine.dylib` |
| Windows | `build/godot_qengine.dll` | `demo/addons/qengine/bin/godot_qengine.dll` |

### 3. Run the demo

Open `demo/` as a Godot 4.4+ project.  The demo plays dataset audio through
the `GuitarIn` bus and shows:

- per-string tuner grid (Tier 1 / `poll_notes`)
- latest detection snapshot in the status bar (`get_latest_detection`)
- onset event log with chart-match result (`pop_note_events`)
- sustain tracking debug line (`get_frame_history`)
- per-string chord panel with active/muted status and dominant note (`pop_chord_frames`)

---

## Using in your own project

### Option A – AudioEffectQEngine (audio-bus integration)

1. Copy `demo/godot-qengine.gdextension` to your project root and adjust the
   library paths.
2. In the Godot Audio panel, add **AudioEffectQEngine** to any bus.
3. Configure once from GDScript:

```gdscript
@onready var fx := AudioServer.get_bus_effect(
    AudioServer.get_bus_index("GuitarIn"), 0)

func _ready():
    fx.band_ranges    = STANDARD_RANGES   # 12 floats; must be set before detection works
    fx.sample_rate    = 48000.0
    fx.min_periodicity = 0.85
    fx.threshold_db   = -45.0
```

4. Each frame, consume the data tier(s) you need:

```gdscript
func _process(_delta):
    # Tier 1 – tuner UI
    var det = fx.get_latest_detection()

    # Tier 2 – chart attack judgment
    for ev in fx.pop_note_events():
        judge_against_chart(ev)

    # Tier 3 – sustain / bend / vibrato
    var history = fx.get_frame_history(30)

    # Tier 4 – Rocksmith-style per-string chord detection
    for cf in fx.pop_chord_frames():
        _judge_chord_frame(cf, current_chart_chord)
```

### Option B – QEngineDetectorNode (standalone node)

Add a **QEngineDetectorNode** to your scene and push audio samples manually:

```gdscript
@onready var detector := $QEngineDetectorNode

func _ready():
    detector.band_ranges = STANDARD_RANGES
    detector.connect("notes_detected", _on_notes)

func push_audio_block(buf: PackedFloat32Array):
    detector.push_samples(buf)

func _process(_delta):
    for ev in detector.pop_note_events():
        judge_against_chart(ev)
```

---

## Running the tests

Tests are standalone C++ executables and do **not** require Godot:

```bash
cmake --build build --target qengine_tests
ctest --test-dir build --output-on-failure
```

---

## Thread safety

All shared state between the analysis layer and GDScript uses SPSC (single-
producer / single-consumer) ring buffers with `std::atomic` head/tail indices:

| Buffer | Producer | Consumer |
|---|---|---|
| `AudioRingBuffer` (PCM samples) | audio thread | main thread (`poll_notes`) |
| `SPSCEventQueue<NoteEvent>` | main thread analysis | main thread GDScript |
| `SPSCFrameHistory<DetectionFrame>` | main thread analysis | main thread GDScript |
| `SPSCEventQueue<ChordFrame>` | main thread analysis | main thread GDScript |

No scene-tree operations are performed from the audio thread.

---

## License

MIT – see [LICENSE](LICENSE).  
cycfi/Q and cycfi/infra are also MIT licensed.  
godot-cpp is MIT licensed.


---

## Architecture

```
┌───────────────────────────────────────────────────────┐
│  Godot audio bus                                       │
│    AudioEffectQEngine::poll_notes()                    │
│      drains AudioEffectCapture buffer (stereo→mono)    │
└───────────────────────────────────────────────────────┘
                  ↓ PackedVector2Array frames ↓
┌───────────────────────────────────────────────────────┐
│  BandDetector (C++)                                    │
│    6 × cycfi::q::pitch_detector (per guitar string)   │
│    → DetectedNote (frequency / MIDI / cents)          │
└───────────────────────────────────────────────────────┘
```

### Source layout

| Path | Purpose |
|---|---|
| `src/note.hpp` | Header-only: `DetectedNote`, frequency↔MIDI helpers |
| `src/tuning.hpp` | Header-only: `TuningId`, `StringInfo`, `Tuning`, `get_tuning()` |
| `src/band_detector.hpp/.cpp` | `BandDetector` – drives 6 Q pitch detectors |
| `src/audio_effect_qengine.hpp/.cpp` | Godot `AudioEffectQEngine` (extends `AudioEffectCapture`) |
| `src/detector_node.hpp/.cpp` | Godot `QEngineDetectorNode` (extends `Node`) |
| `src/register_types.cpp` | GDExtension entry point (`godot_qengine_init`) |
| `tests/test_pitch_detection.cpp` | Standalone C++ tests (no Godot required) |
| *(auto-fetched)* | cycfi/Q — downloaded by CMake FetchContent at configure time |
| *(auto-fetched)* | cycfi/infra — downloaded by CMake FetchContent at configure time |

---

## Supported tunings

| ID | Name | Strings (low → high) |
|---|---|---|
| `Standard` | E Standard | E2 A2 D3 G3 B3 E4 |
| `DropD` | Drop D | D2 A2 D3 G3 B3 E4 |
| `OpenD` | Open D | D2 A2 D3 F#3 A3 D4 |
| `DropC` | Drop C | C2 G2 C3 F3 A3 D4 |
| `DADGAD` | DADGAD | D2 A2 D3 G3 A3 D4 |

---

## Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | GCC 12+, Clang 16+, or MSVC 2022+ (C++20 required) |
| CMake | 3.22+ |
| Godot | 4.4+ |
| git | for cloning the repository |

---

## Getting started

### 1. Clone the repository

```bash
git clone https://github.com/goguitar/godot-qengine.git
cd godot-qengine
```

No submodule init is needed — all C++ dependencies (godot-cpp, cycfi/Q,
cycfi/infra) are downloaded automatically by CMake FetchContent at configure
time.

### 2. Build the GDExtension

```bash
# Configure (downloads godot-cpp automatically via FetchContent)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build the shared library
cmake --build build --target godot_qengine --config Release
```

The compiled library lands in `build/` **and is automatically copied** to
`demo/addons/qengine/bin/` so the demo project is immediately usable:

| Platform | Build output | Demo addon copy |
|---|---|---|
| Linux | `build/libgodot_qengine.so` | `demo/addons/qengine/bin/libgodot_qengine.so` |
| macOS | `build/libgodot_qengine.dylib` | `demo/addons/qengine/bin/libgodot_qengine.dylib` |
| Windows | `build/godot_qengine.dll` | `demo/addons/qengine/bin/godot_qengine.dll` |

### 3. Run the demo

Open `demo/` as a Godot 4.4+ project.  The demo synthesises each open string
of E Standard tuning and displays the detected note + cents deviation.

---

## Using in your own project

### Option A – AudioEffectQEngine (audio-bus integration)

1. Copy `demo/godot-qengine.gdextension` to your project root and adjust the
   library paths.
2. In the Godot Audio panel, add **AudioEffectQEngine** to any bus.
3. Poll every frame from GDScript:

```gdscript
@onready var fx := AudioServer.get_bus_effect(
    AudioServer.get_bus_index("Master"), 0)

func _process(_delta):
    for band in fx.poll_notes():
        if band.note != "":
            print(band.string, " → ", band.note, "  (", "%.1f" % band.cents, " ¢)")
```

### Option B – QEngineDetectorNode (standalone)

Add a **QEngineDetectorNode** to your scene and push audio samples manually:

```gdscript
@onready var detector := $QEngineDetectorNode

func _ready():
    detector.tuning = "DropD"
    detector.connect("notes_detected", _on_notes)

func push_audio_block(buf: PackedFloat32Array):
    detector.push_samples(buf)

func _on_notes(notes: Array):
    for n in notes:
        print(n.string, " → ", n.note)
```

### Dictionary keys returned by `poll_notes()` / `notes_detected`

| Key | Type | Description |
|---|---|---|
| `band` | `int` | Band index (0 = lowest string) |
| `string` | `String` | Open-string label, e.g. `"E2"` |
| `frequency` | `float` | Detected frequency in Hz (0 if none) |
| `periodicity` | `float` | Q confidence `[0, 1]` |
| `note` | `String` | Nearest note, e.g. `"A2"` (`""` if none) |
| `cents` | `float` | Deviation from equal temperament (¢) |

---

## Running the tests

Tests are standalone C++ executables and do **not** require Godot:

```bash
# Build and run (after cmake -B build above)
cmake --build build --target qengine_tests
ctest --test-dir build --output-on-failure
```

---

## License

MIT – see [LICENSE](LICENSE).  
cycfi/Q and cycfi/infra are also MIT licensed.  
godot-cpp is MIT licensed.
