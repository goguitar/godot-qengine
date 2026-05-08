extends SceneTree

const DEFAULT_DATASET_DIR := "res://tests/dataset/guitarset/audio/mic"
const MIN_CONFIDENCE := 0.85
const MAX_SECONDS_PER_FILE := 6.0
const REQUIRED_STRING_HITS := 3
const REQUIRED_GOOD_FILES := 1
const REQUIRED_AVG_HITS := 1.0

var STANDARD_RANGES := PackedFloat32Array([
	 80.11,  164.82,
	106.87,  220.00,
	142.65,  293.66,
	190.42,  392.00,
	239.91,  493.88,
	320.25,  659.26,
])

const NOTE_NAMES := ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
const NOTE_INDEX := {
	"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5,
	"F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11,
}

func _init() -> void:
	await process_frame
	if not _has_default_demo_buses():
		printerr("FAIL: default_bus_layout is missing required buses (GuitarIn/Playback)")
		quit(1)
		return

	for cls in ["QEngineDetectorNode", "AudioEffectQEngine"]:
		if not ClassDB.class_exists(cls):
			printerr("FAIL: Class '%s' not registered" % cls)
			quit(1)
			return

	var root: Window = get_root()
	var detector = ClassDB.instantiate("QEngineDetectorNode")
	if detector == null:
		printerr("FAIL: QEngineDetectorNode not available")
		quit(1)
		return
	root.add_child(detector)
	detector.set("auto_poll", false)
	detector.set("sample_rate", 44100.0)
	detector.set("min_periodicity", MIN_CONFIDENCE)
	detector.set("band_ranges", STANDARD_RANGES)
	var detector_sample_rate := 44100.0

	var dataset_dir: String = OS.get_environment("QENGINE_DATASET_DIR")
	if dataset_dir.is_empty():
		dataset_dir = DEFAULT_DATASET_DIR

	var files: PackedStringArray = _list_rock00_comp_files(dataset_dir)
	if files.is_empty():
		printerr("FAIL: No 00_Rock*_comp_mic.wav files found in %s" % dataset_dir)
		quit(1)
		return

	var failed := false
	var files_with_expected_hit_floor := 0
	var total_hits := 0
	var total_files := 0
	for file_path in files:
		var expected_root := _key_token_from_filename(file_path.get_file())
		var expected_chord: PackedStringArray = _major_triad(expected_root)
		if expected_chord.is_empty():
			printerr("FAIL: cannot parse expected chord from %s" % file_path.get_file())
			failed = true
			continue

		var wav := _load_wav_samples(file_path, MAX_SECONDS_PER_FILE)
		if wav.is_empty():
			printerr("FAIL: could not decode %s" % file_path)
			failed = true
			continue
		var samples: PackedFloat32Array = wav.get("samples", PackedFloat32Array())
		if samples.is_empty():
			printerr("FAIL: empty samples in %s" % file_path)
			failed = true
			continue
		var wav_rate: float = float(wav.get("sample_rate", detector_sample_rate))
		if abs(wav_rate - detector_sample_rate) > 0.5:
			detector.set("sample_rate", wav_rate)
			detector.init_detector()
			detector_sample_rate = wav_rate

		detector.reset()

		var band_counts: Array = []
		for _i in 6:
			band_counts.append({})

		var idx := 0
		var block_size := 512
		while idx < samples.size():
			var end_idx: int = min(idx + block_size, samples.size())
			var block: PackedFloat32Array = samples.slice(idx, end_idx)
			detector.push_samples(block)
			await process_frame
			_drain_chord_frames(detector, band_counts)
			idx = end_idx

		for _i in 3:
			await process_frame
			_drain_chord_frames(detector, band_counts)

		var dominant: PackedStringArray = []
		for band in 6:
			dominant.append(_top_note_class(band_counts[band]))

		var detected_chord := _detected_chord_classes(dominant)
		var hit_count := 0
		var detected_strings := 0
		for band in 6:
			if not String(dominant[band]).is_empty():
				detected_strings += 1
			if expected_chord.has(String(dominant[band])):
				hit_count += 1

		total_files += 1
		total_hits += hit_count
		if hit_count >= REQUIRED_STRING_HITS:
			files_with_expected_hit_floor += 1

		_print_chart(file_path.get_file(), expected_root, expected_chord, detected_chord, dominant, hit_count)
		if detected_strings < 3:
			printerr("FAIL: too few detected strings in %s (%d)" % [file_path.get_file(), detected_strings])
			failed = true

	detector.queue_free()
	await detector.tree_exited

	var avg_hits := 0.0
	if total_files > 0:
		avg_hits = float(total_hits) / float(total_files)
	print("aggregate: files=%d good_files=%d avg_hits=%.2f" % [total_files, files_with_expected_hit_floor, avg_hits])
	if files_with_expected_hit_floor < REQUIRED_GOOD_FILES:
		printerr("FAIL: expected at least %d files with >= %d chord-tone string hits" % [REQUIRED_GOOD_FILES, REQUIRED_STRING_HITS])
		failed = true
	if avg_hits < REQUIRED_AVG_HITS:
		printerr("FAIL: average chord-tone string hits too low (%.2f < %.2f)" % [avg_hits, REQUIRED_AVG_HITS])
		failed = true

	if failed:
		printerr("One or more 00_Rock chord/per-string checks failed")
		quit(1)
	else:
		print("All 00_Rock chord/per-string checks passed")
		quit(0)

func _print_chart(file_name: String, root_note: String, expected_chord: PackedStringArray, detected_chord: PackedStringArray, dominant: PackedStringArray, hit_count: int) -> void:
	print("")
	print("FILE: %s" % file_name)
	print("ROWS: 7 (Chord + 6 strings)")
	print("row | expected                     | detected")
	print("--- | ---------------------------- | ----------------------------")
	print("chord | %s major %s              | %s" % [root_note, str(expected_chord), str(detected_chord)])
	for band in 6:
		var string_label := "S%d" % (6 - band)
		var detected := String(dominant[band])
		if detected.is_empty():
			detected = "--"
		print("%s | one of %s | %s" % [string_label, str(expected_chord), detected])
	print("score: %d/6 strings matched chord tones (min %d)" % [hit_count, REQUIRED_STRING_HITS])

func _detected_chord_classes(dominant: PackedStringArray) -> PackedStringArray:
	var out: PackedStringArray = []
	for n in dominant:
		var cls: String = String(n)
		if cls.is_empty():
			continue
		if not out.has(cls):
			out.append(cls)
	return out

func _major_triad(root_note: String) -> PackedStringArray:
	if not NOTE_INDEX.has(root_note):
		return []
	var r: int = int(NOTE_INDEX[root_note])
	return PackedStringArray([
		NOTE_NAMES[r],
		NOTE_NAMES[(r + 4) % 12],
		NOTE_NAMES[(r + 7) % 12],
	])

func _list_rock00_comp_files(dataset_dir: String) -> PackedStringArray:
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
		if not entry.begins_with("00_Rock"):
			continue
		if not entry.to_lower().ends_with("_comp_mic.wav"):
			continue
		files.append(dataset_dir.path_join(entry))
	dir.list_dir_end()
	files.sort()
	return files

func _key_token_from_filename(file_name: String) -> String:
	var parts: PackedStringArray = file_name.split("-")
	if parts.size() < 3:
		return ""
	var token: String = String(parts[2]).split("_")[0]
	return _normalize_note(token)

func _normalize_note(note: String) -> String:
	match note:
		"Cb": return "B"
		"Db": return "C#"
		"Eb": return "D#"
		"Fb": return "E"
		"Gb": return "F#"
		"Ab": return "G#"
		"Bb": return "A#"
		"E#": return "F"
		"B#": return "C"
		_: return note

func _top_note_class(counts: Dictionary) -> String:
	var top := ""
	var top_count := -1
	for k in counts.keys():
		var c: int = int(counts[k])
		if c > top_count:
			top_count = c
			top = String(k)
	return top

func _drain_chord_frames(detector, band_counts: Array) -> void:
	var chord_frames: Array = detector.pop_chord_frames()
	for cf in chord_frames:
		var strings: Array = cf.get("strings", [])
		for band in range(min(6, strings.size())):
			var item: Dictionary = strings[band]
			var conf: float = float(item.get("confidence", 0.0))
			var midi: int = int(item.get("midi_note", -1))
			if midi < 0 or conf < MIN_CONFIDENCE or not bool(item.get("active", false)):
				continue
			var cls: String = NOTE_NAMES[((midi % 12) + 12) % 12]
			var counts: Dictionary = band_counts[band]
			counts[cls] = int(counts.get(cls, 0)) + 1
			band_counts[band] = counts

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

func _has_default_demo_buses() -> bool:
	return AudioServer.get_bus_index("GuitarIn") >= 0 and AudioServer.get_bus_index("Playback") >= 0
