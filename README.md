# godot-qengine

A **Godot 4.4+ GDExtension** written in C++ that adds real-time guitar/bass
pitch detection to any Godot project.  
Detection is powered by the [**cycfi/Q**](https://github.com/cycfi/q)
C++ library, accessed directly (no FFI bridge required).

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
| `third_party/q` | cycfi/Q (git submodule) |
| `third_party/infra` | cycfi/infra (git submodule) |

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
| git | for submodule checkout |

---

## Getting started

### 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/goguitar/godot-qengine.git
cd godot-qengine
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

### 2. Build the GDExtension

```bash
# Configure (downloads godot-cpp automatically via FetchContent)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build the shared library
cmake --build build --target godot_qengine --config Release
```

The compiled library lands in `build/`:

| Platform | File |
|---|---|
| Linux | `build/libgodot_qengine.so` |
| macOS | `build/libgodot_qengine.dylib` |
| Windows | `build/godot_qengine.dll` (or `build/Release/`) |

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
