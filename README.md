# godot-qengine

A Godot 4.4+ GDExtension for real-time guitar/bass analysis.

This branch now uses a **Rust-first architecture**:

- **Godot layer:** Rust (`godot-rust`)
- **Bridge:** C-compatible FFI (`src/qengine_ffi.h/.cpp`)
- **DSP backend:** C++ (`cycfi/Q`) with async processing/ring buffers

## Architecture

### 1) Rust Godot extension (`rust/`)
Rust exposes Godot-facing classes:

- `AudioEffectQEngine`
- `QEngineDetectorNode`

Responsibilities:

- class registration / lifecycle
- property and method exposure to GDScript
- conversion of native snapshots/events/history/chord frames to Godot `Dictionary`/`Array`
- lightweight audio ingestion + non-blocking polling APIs

### 2) C ABI bridge (`src/qengine_ffi.h`, `src/qengine_ffi.cpp`)
The Rust layer talks to C++ through an opaque-handle C API:

- `qengine_create`
- `qengine_destroy`
- `qengine_push_audio`
- `qengine_process`
- `qengine_get_latest_detection`
- `qengine_pop_note_events`
- `qengine_get_recent_frames`
- `qengine_pop_chord_frames`
- `qengine_get_latest_chord_frame`

Rules:

- no C++ types cross FFI
- only POD structs over ABI
- explicit ownership (`create/destroy`)

### 3) C++ DSP core (`src/band_detector.*`, `src/async_band_detector.*`)
The backend keeps cycfi/Q-based analysis:

- per-string pitch + periodicity
- onset event queue
- latest detection snapshot
- recent frame history
- per-string chord frames
- dominant/root string reporting
- SPSC buffers to keep audio callback path lightweight

## Thread model / data flow

1. Audio samples are pushed via `qengine_push_audio`.
2. The async detector worker processes buffered audio (no blocking on audio thread).
3. Gameplay/UI polls:
   - latest snapshot
   - note event FIFO
   - recent frame history
   - chord frame FIFO

## Build

### Prerequisites

- CMake 3.22+
- C++20 compiler
- Rust toolchain (`cargo`, `rustc`)

### Configure + build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Outputs:

- extension library in `build/` (`libgodot_qengine.so` / `godot_qengine.dll` / `libgodot_qengine.dylib`)
- copied to `demo/addons/qengine/bin/`

## Tests

Native tests (no Godot runtime required):

```bash
cmake --build build --target qengine_tests qengine_wav_tests qengine_ffi_tests --config Release
ctest --test-dir build --output-on-failure
```

`qengine_ffi_tests` covers:

- FFI lifecycle (`create/destroy/reset`)
- latest detection retrieval
- event queue draining
- frame history retrieval
- chord frame retrieval (including active/dominant data)

## Demo usage

The demo project still uses:

- `AudioEffectQEngine` for capture-bus analysis
- `QEngineDetectorNode` for manual sample-push workflows

It remains a playable/debuggable reference for per-string expected vs detected notes, chord components, and dominant/root reporting.

## Windows/runtime notes

- Build with MSVC-compatible CMake + Rust toolchain.
- The generated extension binary is copied into the addon `bin/` directory.
- Keep extension binary and its runtime dependencies in the same packaged addon output.

## License

MIT (project, cycfi/Q, cycfi/infra, godot-rust ecosystem dependencies are MIT-compatible).
