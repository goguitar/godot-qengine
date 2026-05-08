extends SceneTree

const DEFAULT_DATASET_DIR := "/home/csantz/godot-qengine/demo/tests/dataset/guitarset/audio/mic"
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

	for cls in ["QEngineDetectorNode", "AudioEffectQEngine"]:
		if not ClassDB.class_exists(cls):
			printerr("FAIL: Class '%s' not registered" % cls)
			quit(1)
			return

	var effect = _get_qengine_effect_on_capture()
	if effect == null:
		effect = _add_qengine_effect_on_capture()
	if effect == null:
		printerr("FAIL: AudioEffectQEngine missing on GuitarIn bus")
		quit(1)
		return

	var dataset_dir: String = OS.get_environment("QENGINE_DATASET_DIR")
	if dataset_dir.is_empty():
		dataset_dir = DEFAULT_DATASET_DIR

	var files: PackedStringArray = _list_rock00_comp_files(dataset_dir)
	if files.is_empty():
		printerr("FAIL: No 00_Rock*_comp_mic.wav files found in %s" % dataset_dir)
		quit(1)
		return

	var root: Window = get_root()
	var player := AudioStreamPlayer.new()
	player.bus = "GuitarIn"
	root.add_child(player)

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

		var stream: AudioStreamWAV = AudioStreamWAV.load_from_file(file_path)
		if stream == null:
			printerr("FAIL: could not load %s" % file_path)
			failed = true
			continue

		player.stream = stream
		effect.reset()
		player.play()

		var band_counts: Array = []
		for _i in 6:
			band_counts.append({})

		var start_ms := Time.get_ticks_msec()
		while player.playing and (Time.get_ticks_msec() - start_ms) < int(MAX_SECONDS_PER_FILE * 1000.0):
			await process_frame
			var notes: Array = effect.poll_notes()
			for band in range(min(6, notes.size())):
				var item: Dictionary = notes[band]
				var conf: float = float(item.get("periodicity", 0.0))
				var midi: int = int(item.get("midi_note", -1))
				if midi < 0 or conf < MIN_CONFIDENCE:
					continue
				var cls: String = NOTE_NAMES[((midi % 12) + 12) % 12]
				var counts: Dictionary = band_counts[band]
				counts[cls] = int(counts.get(cls, 0)) + 1
				band_counts[band] = counts

		player.stop()

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

	root.remove_child(player)
	player.queue_free()

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
	fx.set("min_periodicity", MIN_CONFIDENCE)
	fx.set("band_ranges", STANDARD_RANGES)
	AudioServer.add_bus_effect(bus_idx, fx, 0)
	return fx
