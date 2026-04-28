# godot-qengine

A **Godot 4.5 GDExtension** written in Rust that adds real-time guitar/bass
pitch detection to any Godot 4.5 project.  
Detection is powered by the [**cycfi/Q**](https://github.com/cycfi/q)
C++ library, accessed from Rust via a thin C FFI bridge.

---

## Architecture

```
┌───────────────────────────────────────────────────────┐
│  Godot audio thread                                    │
│    AudioEffectInstanceQEngine::process_rawptr()        │
│      ↓  mono-mixed f32 samples  (lock-free SPSC)       │
└───────────────────────────────────────────────────────┘
                 ↓ rtrb Producer → Consumer ↓
┌───────────────────────────────────────────────────────┐
│  Godot main thread (GDScript _process)                │
│    AudioEffectQEngine::poll_notes()  ─or─             │
│    QEngineDetectorNode::poll_notes()                  │
│      → qengine-core BandDetector                      │
│        → qengine-sys (C FFI) → cycfi/Q GuitarDetector │
│           6 bands × pitch_detector                    │
│      ← Vec<DetectionResult> → Array[Dictionary]       │
└───────────────────────────────────────────────────────┘
```

### Crates

| Crate | Purpose |
|---|---|
| `qengine-sys` | Thin `extern "C"` declarations + safe Rust wrappers (`PitchDetector`, `GuitarDetector`) over the compiled C++ bridge |
| `qengine-core` | Pure-Rust logic: note identification, guitar tunings, SPSC ring buffer helpers, `BandDetector` |
| `godot-qengine` | Godot 4.5 GDExtension: `AudioEffectQEngine`, `AudioEffectInstanceQEngine`, `QEngineDetectorNode` |

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
| Rust toolchain | 1.75+ (`edition = "2021"`) |
| C++ compiler | GCC 12+ or Clang 16+ (C++20 required by cycfi/Q) |
| Godot | 4.5+ |

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
# Debug (fast rebuild)
cargo build -p godot-qengine

# Release (optimised – use for distribution)
cargo build -p godot-qengine --release
```

The compiled library lands in `target/debug/` or `target/release/` and is
referenced by the `.gdextension` file in `demo/`.

### 3. Run the demo

Open `demo/` as a Godot 4.5 project.  The demo synthesises each open string
of E Standard tuning and displays the detected note + cents deviation.

---

## Using in your own project

### Option A – AudioEffectQEngine (audio-bus integration)

1. Copy `demo/godot-qengine.gdextension` to your project root and adjust the
   library paths.
2. In the Godot Audio panel, add **AudioEffectQEngine** to any bus.
3. In GDScript, poll every frame:

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

```bash
# Unit + integration tests (no Godot required)
cargo test -p qengine-sys -p qengine-core

# Dataset tests (requires GuitarSet audio/mic files)
QENGINE_DATASET_DIR=/path/to/guitarset/audio/mic \
    cargo test -p qengine-core --features dataset_tests -- --ignored
```

The GuitarSet dataset is available at  
<https://github.com/santzit/guitar-pitch-detection-/tree/main/tests/dataset/guitarset/audio/mic>

---

## License

MIT – see [LICENSE](LICENSE).  
cycfi/Q and cycfi/infra are also MIT licensed.
