extends SceneTree

const DEFAULT_DATASET_DIR := "res://tests/dataset/guitarset/audio/mic"
const MAX_FILES := 12
const MAX_SECONDS_PER_FILE := 6.0

func _init() -> void:
	await process_frame

	var effect: AudioEffectQEngine = _get_qengine_effect_on_capture()
	if effect == null:
		push_error("QEngine effect not found on Capture bus")
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
	player.bus = "Capture"
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
				var note: String = String(item.get("note", ""))
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

func _get_qengine_effect_on_capture() -> AudioEffectQEngine:
	var bus_idx: int = AudioServer.get_bus_index("Capture")
	if bus_idx < 0:
		return null
	for i in AudioServer.get_bus_effect_count(bus_idx):
		var fx := AudioServer.get_bus_effect(bus_idx, i)
		if fx is AudioEffectQEngine:
			return fx
	return null

func _list_dataset_files(dataset_dir: String) -> PackedStringArray:
	var files: PackedStringArray = []
	var dir := DirAccess.open(dataset_dir)
	if dir == null:
		return files
	dir.list_dir_begin()
	while true:
		var name: String = dir.get_next()
		if name.is_empty():
			break
		if dir.current_is_dir():
			continue
		if not name.to_lower().ends_with("_solo_mic.wav"):
			continue
		files.append(dataset_dir.path_join(name))
	dir.list_dir_end()
	files.sort()
	return files

func _expected_note_from_filename(file_name: String) -> String:
	var parts: PackedStringArray = file_name.split("-")
	if parts.size() < 3:
		return ""
	var note_and_rest: String = parts[2]
	var note: String = note_and_rest.split("_")[0]
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
