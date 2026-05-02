## ci_headless_test.gd – Headless CI integration test for the QEngine GDExtension.
##
## Run from the repo root with:
##   godot --headless --path demo/ --script res://scripts/ci_headless_test.gd
##
## Does NOT require audio hardware — uses QEngineDetectorNode.push_samples()
## directly to feed synthetic sine waves through the pitch detectors.
extends SceneTree

const SAMPLE_RATE := 44100

# Standard tuning band_ranges (mirrors TUNING_DATA["Standard"] in main.gd)
const STANDARD_RANGES := PackedFloat32Array([
	 80.11,  329.64,   # band 0: E2  82.41 Hz
	106.87,  440.00,   # band 1: A2 110.00 Hz
	142.65,  587.32,   # band 2: D3 146.83 Hz
	190.42,  784.00,   # band 3: G3 196.00 Hz
	239.91,  987.76,   # band 4: B3 246.94 Hz
	320.25, 1318.52,   # band 5: E4 329.63 Hz
])

# Open-string frequencies, expected MIDI notes, and names for Standard tuning.
const OPEN_STRINGS := [
	[82.41,  40, "E2"],
	[110.00, 45, "A2"],
	[146.83, 50, "D3"],
	[196.00, 55, "G3"],
	[246.94, 59, "B3"],
	[329.63, 64, "E4"],
]

func _init() -> void:
	await process_frame
	var failed := false

	# ── Test 1: GDExtension class registration ────────────────────────────────
	for cls in ["QEngineDetectorNode", "AudioEffectQEngine"]:
		if not ClassDB.class_exists(cls):
			printerr("FAIL: Class '%s' not registered — extension may not have loaded" % cls)
			failed = true
	if not failed:
		print("PASS: QEngineDetectorNode and AudioEffectQEngine are registered")

	if failed:
		quit(1)
		return

	# ── Test 2: poll_notes() returns 6 silent dicts before band_ranges is set ─
	var node = ClassDB.instantiate("QEngineDetectorNode")
	var pre: Array = node.poll_notes()
	var pre_ok := pre.size() == 6
	if pre_ok:
		for d in pre:
			if int(d.get("midi_note", -1)) != -1:
				pre_ok = false
				break
	if pre_ok:
		print("PASS: poll_notes() returns 6 silent dicts before band_ranges set")
	else:
		printerr("FAIL: Expected 6 silent dicts before band_ranges set, got: %s" % str(pre))
		failed = true

	# ── Tests 3–8: Detect each open Standard-tuning string via push_samples() ─
	# We push sine-wave chunks and call poll_notes() after each chunk.
	# QEngineDetectorNode is NOT added to the scene tree so _process() is never
	# called; poll_notes() drains the ring buffer on demand.
	const CHUNK := 4096
	const MAX_SAMPLES := SAMPLE_RATE * 3  # 3 s per string

	node.band_ranges = STANDARD_RANGES

	for band_idx in 6:
		var freq: float       = OPEN_STRINGS[band_idx][0]
		var expected_midi: int = OPEN_STRINGS[band_idx][1]
		var label: String      = OPEN_STRINGS[band_idx][2]

		# Reinitialise detector (clear ring buffer + reset pitch detectors)
		node.band_ranges = STANDARD_RANGES

		var tw := 2.0 * PI * freq / float(SAMPLE_RATE)
		var detected_midi := -1
		var offset := 0

		while offset < MAX_SAMPLES and detected_midi == -1:
			var n := mini(CHUNK, MAX_SAMPLES - offset)
			var buf := PackedFloat32Array()
			buf.resize(n)
			for i in n:
				buf[i] = sin(tw * float(offset + i))
			offset += n
			node.push_samples(buf)
			var results: Array = node.poll_notes()
			var m: int = int(results[band_idx].get("midi_note", -1))
			if m != -1:
				detected_midi = m

		if detected_midi == expected_midi:
			print("PASS: Band %d (%s) → MIDI %d" % [band_idx, label, detected_midi])
		else:
			printerr("FAIL: Band %d (%s): expected MIDI %d, got %d" % [
				band_idx, label, expected_midi, detected_midi,
			])
			failed = true

	node.free()

	print("")
	if failed:
		printerr("One or more CI headless tests FAILED")
		quit(1)
	else:
		print("All CI headless tests PASSED")
		quit(0)
