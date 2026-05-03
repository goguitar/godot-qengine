## guitar_detector.gd
##
## Attaches a QEngineDetectorNode to the scene.  In a production setup you
## would push audio frames from an AudioStreamPlayer / microphone input via
## AudioEffectCapture into this node.  Here we demonstrate the manual
## push_samples() path using synthetic test tones so the demo works without
## physical microphone hardware.
##
## Expected notes (E Standard): E2 A2 D3 G3 B3 E4

extends Node

# Note frequencies for E Standard (open strings) – used for synthesis.
var STANDARD_FREQS: PackedFloat32Array = PackedFloat32Array([82.41, 110.00, 146.83, 196.00, 246.94, 329.63])

# Per-band frequency ranges for E Standard tuning.
# Format: [min0, max0, min1, max1, …, min5, max5]  (index 0 = lowest string)
var STANDARD_RANGES: PackedFloat32Array = PackedFloat32Array([
	 80.11,  329.64,   # string 6: E2
	106.87,  440.00,   # string 5: A2
	142.65,  587.32,   # string 4: D3
	190.42,  784.00,   # string 3: G3
	239.91,  987.76,   # string 2: B3
	320.25, 1318.52,   # string 1: E4
])

const SAMPLE_RATE    := 44100.0
const BLOCK_SECONDS  := 0.05   # generate 50 ms of audio each frame

var _phase      : float = 0.0
var _play_band  : int   = 0     # which string to synthesise
var _time_acc   : float = 0.0
var _note_time  : float = 1.5   # seconds per note cycle

func _ready() -> void:
	# Configure the detector with E Standard band ranges.
	# Setting band_ranges automatically triggers init_detector() internally.
	set("band_ranges", STANDARD_RANGES)
	set("sample_rate", SAMPLE_RATE)
	set("threshold_db", -45.0)
	set("auto_poll", false)   # we will poll manually

func _process(delta: float) -> void:
	# Cycle through each open-string frequency every _note_time seconds
	_time_acc += delta
	if _time_acc >= _note_time:
		_time_acc = 0.0
		_play_band = (_play_band + 1) % 6

	# Generate one block of a pure sine wave at the current string frequency
	var freq: float = STANDARD_FREQS[_play_band]
	var n     := int(SAMPLE_RATE * BLOCK_SECONDS)
	var buf   := PackedFloat32Array()
	buf.resize(n)
	for i in n:
		buf[i] = sin(2.0 * PI * freq * (_phase + i) / SAMPLE_RATE)
	_phase = fmod(_phase + n, SAMPLE_RATE / freq)

	# Push samples and poll
	call("push_samples", buf)
	call("poll_notes")   # triggers notes_detected signal
