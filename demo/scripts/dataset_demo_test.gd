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

	var dataset_dir: String = OS.get_environment("QENGINE_DATASET_DIR")
	if dataset_dir.is_empty():
		dataset_dir = DEFAULT_DATASET_DIR

	var files: PackedStringArray = _list_dataset_files(dataset_dir)
	if files.is_empty():
		push_error("No dataset WAV files found in: %s" % dataset_dir)
		quit(1)
		return

	var root: Window = get_root()
	var detector = ClassDB.instantiate("QEngineDetectorNode")
	if detector == null:
		push_error("QEngineDetectorNode class not available")
		quit(1)
		return
	root.add_child(detector)
	detector.set("auto_poll", false)
	detector.set("sample_rate", 44100.0)
	detector.set("min_periodicity", 0.85)
	detector.set("band_ranges", PackedFloat32Array([
		 80.11,  164.82,
		106.87,  220.00,
		142.65,  293.66,
		190.42,  392.00,
		239.91,  493.88,
		320.25,  659.26,
	]))
	var detector_sample_rate := 44100.0

	var tested: int = 0
	var passed: int = 0
	for file_path in files:
		if tested >= MAX_FILES:
			break

		var wav := _load_wav_samples(file_path, MAX_SECONDS_PER_FILE)
		if wav.is_empty():
			continue
		var samples: PackedFloat32Array = wav.get("samples", PackedFloat32Array())
		if samples.is_empty():
			continue
		var wav_rate: float = float(wav.get("sample_rate", detector_sample_rate))
		if abs(wav_rate - detector_sample_rate) > 0.5:
			detector.set("sample_rate", wav_rate)
			detector.init_detector()
			detector_sample_rate = wav_rate

		detector.reset()

		var expected: String = _expected_note_from_filename(file_path.get_file())
		var counts: Dictionary = {}
		var idx: int = 0
		var block_size: int = 512
		while idx < samples.size():
			var end_idx: int = min(idx + block_size, samples.size())
			var block: PackedFloat32Array = samples.slice(idx, end_idx)
			detector.push_samples(block)
			await process_frame
			_drain_chord_frames(detector, counts)
			idx = end_idx

		for _i in 3:
			await process_frame
			_drain_chord_frames(detector, counts)

		var detected: String = _top_detected(counts)
		var expected_hits: int = int(counts.get(expected, 0))
		var ok: bool = (expected != "" and detected == expected and expected_hits >= 3)
		tested += 1
		if ok:
			passed += 1

		var status: String = "PASS" if ok else "FAIL"
		print("%s expected=%s detected=%s hits=%d file=%s" % [
			status,
			expected,
			detected,
			expected_hits,
			file_path.get_file(),
		])

	detector.queue_free()
	await detector.tree_exited

	print("demo_dataset_test: passed=%d tested=%d" % [passed, tested])
	quit(0 if tested > 0 and passed == tested else 1)

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
		var lower_entry := entry.to_lower()
		if not (lower_entry.ends_with("_solo_mic.wav") or lower_entry.ends_with("_comp_mic.wav")):
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

func _midi_note_class(midi: int) -> String:
	if midi < 0 or midi > 127:
		return ""
	return NOTE_NAMES[int(midi) % 12]

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

func _drain_chord_frames(detector, counts: Dictionary) -> void:
	var frames: Array = detector.pop_chord_frames()
	for cf in frames:
		var active_count: int = int(cf.get("active_count", 0))
		var dom_midi: int = int(cf.get("dominant_midi", -1))
		if active_count <= 0 or dom_midi < 0:
			continue
		var note_class: String = _normalize_note_class(_midi_note_class(dom_midi))
		if note_class.is_empty():
			continue
		counts[note_class] = int(counts.get(note_class, 0)) + 1

func _load_wav_samples(path: String, max_seconds: float) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var riff := file.get_buffer(4).get_string_from_ascii()
	if riff != "RIFF":
		return {}
	file.get_32()
	var wave := file.get_buffer(4).get_string_from_ascii()
	if wave != "WAVE":
		return {}

	var channels: int = 0
	var bits: int = 0
	var sample_rate: int = 0
	var data_pos: int = -1
	var data_size: int = 0

	while file.get_position() < file.get_length():
		var chunk_id_bytes := file.get_buffer(4)
		if chunk_id_bytes.size() < 4:
			break
		var chunk_id := chunk_id_bytes.get_string_from_ascii()
		var chunk_size := int(file.get_32())
		if chunk_id == "fmt ":
			var format := int(file.get_16())
			channels = int(file.get_16())
			sample_rate = int(file.get_32())
			file.get_32()
			file.get_16()
			bits = int(file.get_16())
			var extra := chunk_size - 16
			if extra > 0:
				file.seek(file.get_position() + extra)
			if format != 1:
				return {}
		elif chunk_id == "data":
			data_pos = int(file.get_position())
			data_size = chunk_size
			break
		else:
			var skip := chunk_size + (chunk_size % 2)
			file.seek(file.get_position() + skip)

	if data_pos < 0 or bits != 16 or channels <= 0 or sample_rate <= 0:
		return {}

	file.seek(data_pos)
	var bytes_per_sample := bits / 8
	var total_frames := int(data_size / (bytes_per_sample * channels))
	if max_seconds > 0.0:
		var max_frames := int(float(sample_rate) * max_seconds)
		total_frames = min(total_frames, max_frames)

	var samples := PackedFloat32Array()
	samples.resize(total_frames)
	for i in total_frames:
		var sum := 0.0
		for _c in channels:
			if file.eof_reached():
				break
			var raw := int(file.get_16())
			if raw > 32767:
				raw -= 65536
			sum += float(raw) / 32768.0
		samples[i] = sum / float(channels)

	return {
		"sample_rate": float(sample_rate),
		"samples": samples,
	}
