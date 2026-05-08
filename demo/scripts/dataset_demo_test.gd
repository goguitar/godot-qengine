extends SceneTree

const DEFAULT_DATASET_DIR := "res://tests/dataset/guitarset/audio/mic"
const MAX_FILES := 12
const MAX_SECONDS_PER_FILE := 6.0

# ── Note-name lookup (same logic as main.gd / guitar_detector.gd) ────────────
const NOTE_NAMES := ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

static func freq_to_note_display(freq: float) -> String:
	if freq <= 0.0:
		return ""
	var midi_f: float = 69.0 + 12.0 * log(freq / 440.0) / log(2.0)
	var midi: int = int(round(midi_f))
	if midi < 0 or midi > 127:
		return ""
	var note_idx: int = ((midi % 12) + 12) % 12
	var octave: int   = int(float(midi) / 12.0) - 1
	return NOTE_NAMES[note_idx] + str(octave)

func _init() -> void:
	await process_frame

	var effect = _get_qengine_effect_on_capture()
	if effect == null:
		effect = _add_qengine_effect_on_capture()
	if effect == null:
		push_error("QEngine effect not found on GuitarIn bus")
		quit(1)
		return

	var dataset_dir: String = OS.get_environment("QENGINE_DATASET_DIR")
	if dataset_dir.is_empty():
		dataset_dir = DEFAULT_DATASET_DIR

	var files: PackedStringArray = _list_dataset_files(dataset_dir)
	if files.is_empty():
		push_error("No dataset WAV files found in: %s" % dataset_dir)
		quit(1)
		return

	var root: Window = get_root()
	var player := AudioStreamPlayer.new()
	player.bus = "GuitarIn"
	root.add_child(player)

	var tested: int = 0
	var passed: int = 0
	for file_path in files:
		if tested >= MAX_FILES:
			break

		var stream: AudioStreamWAV = AudioStreamWAV.load_from_file(file_path)
		if stream == null:
			continue

		player.stream = stream
		effect.reset()
		player.play()

		var expected: String = _expected_note_from_filename(file_path.get_file())
		var counts: Dictionary = {}
		var start_ms: int = Time.get_ticks_msec()
		while player.playing and (Time.get_ticks_msec() - start_ms) < int(MAX_SECONDS_PER_FILE * 1000.0):
			await process_frame
			var notes: Array = effect.poll_notes()
			for item in notes:
				var freq: float = float(item.get("frequency", 0.0))
				var note: String = freq_to_note_display(freq)
				if note.is_empty():
					continue
				var note_class: String = _normalize_note_class(_detected_note_class(note))
				counts[note_class] = int(counts.get(note_class, 0)) + 1

		player.stop()

		var detected: String = _top_detected(counts)
		var ok: bool = (expected != "" and int(counts.get(expected, 0)) >= 3)
		tested += 1
		if ok:
			passed += 1

		print("expected=%s played=%s detected=%s hits=%d file=%s" % [
			expected,
			expected,
			detected,
			int(counts.get(expected, 0)),
			file_path.get_file(),
		])

	print("demo_dataset_test: passed=%d tested=%d" % [passed, tested])
	quit(0 if tested > 0 and passed == tested else 1)

func _get_qengine_effect_on_capture():
	var bus_idx: int = AudioServer.get_bus_index("GuitarIn")
	if bus_idx < 0:
		return null
	for i in AudioServer.get_bus_effect_count(bus_idx):
		var fx := AudioServer.get_bus_effect(bus_idx, i)
		if fx and fx.has_method("poll_notes"):
			return fx
	return null

func _add_qengine_effect_on_capture():
	var bus_idx: int = AudioServer.get_bus_index("GuitarIn")
	if bus_idx < 0:
		return null

	var fx: AudioEffect = ClassDB.instantiate("AudioEffectQEngine") as AudioEffect
	if fx == null:
		return null

	fx.set("sample_rate", 48000.0)
	fx.set("min_periodicity", 0.85)
	fx.set("band_ranges", PackedFloat32Array([
		 80.11,  164.82,
		106.87,  220.00,
		142.65,  293.66,
		190.42,  392.00,
		239.91,  493.88,
		320.25,  659.26,
	]))
	AudioServer.add_bus_effect(bus_idx, fx, 0)
	return fx

func _list_dataset_files(dataset_dir: String) -> PackedStringArray:
	var files: PackedStringArray = []
	var dir := DirAccess.open(dataset_dir)
	if dir == null:
		return files
	dir.list_dir_begin()
	while true:
		var entry: String = dir.get_next()
		if entry.is_empty():
			break
		if dir.current_is_dir():
			continue
		if not entry.to_lower().ends_with("_solo_mic.wav"):
			continue
		files.append(dataset_dir.path_join(entry))
	dir.list_dir_end()
	files.sort()
	return files

func _expected_note_from_filename(file_name: String) -> String:
	var parts: PackedStringArray = file_name.split("-")
	if parts.size() < 3:
		return ""
	var note_and_rest: String = parts[2]
	var note: String = note_and_rest.split("_")[0]
	# Strip minor-key suffix (e.g. "Em" → "E")
	if note.ends_with("m"):
		note = note.left(note.length() - 1)
	return _normalize_note_class(note)

func _detected_note_class(note: String) -> String:
	if note.length() >= 2 and (note[1] == "#" or note[1] == "b"):
		return note.substr(0, 2)
	if note.length() >= 1:
		return note.substr(0, 1)
	return ""

func _normalize_note_class(note: String) -> String:
	match note:
		"Cb":
			return "B"
		"Db":
			return "C#"
		"Eb":
			return "D#"
		"Fb":
			return "E"
		"Gb":
			return "F#"
		"Ab":
			return "G#"
		"Bb":
			return "A#"
		"E#":
			return "F"
		"B#":
			return "C"
		_:
			return note

func _top_detected(counts: Dictionary) -> String:
	var top_note: String = ""
	var top_count: int = -1
	for k in counts.keys():
		var c: int = int(counts[k])
		if c > top_count:
			top_count = c
			top_note = String(k)
	return top_note
