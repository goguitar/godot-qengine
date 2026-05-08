extends Control

const INSTRUMENTS: PackedStringArray = ["Guitar", "Bass"]
const GUITAR_TUNINGS: PackedStringArray = ["Standard", "DropD", "OpenD", "DropC"]
const BASS_TUNINGS: PackedStringArray = ["Standard", "DropD"]
const DEFAULT_DATASET_DIR := "res://tests/dataset/guitarset/audio/mic"
const NOTE_NAMES := ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

@onready var _back_button: Button = $RootMargin/RootVBox/Top/BackButton
@onready var _instrument_opt: OptionButton = $RootMargin/RootVBox/Controls/ControlsHBox/InstrumentOption
@onready var _tuning_opt: OptionButton = $RootMargin/RootVBox/Controls/ControlsHBox/TuningOption
@onready var _play_pause: Button = $RootMargin/RootVBox/Controls/ControlsHBox/PlayPause
@onready var _next_button: Button = $RootMargin/RootVBox/Controls/ControlsHBox/NextButton

@onready var _current_file: Label = $RootMargin/RootVBox/InfoCard/InfoVBox/CurrentFile
@onready var _expected: Label = $RootMargin/RootVBox/InfoCard/InfoVBox/Expected
@onready var _detected: Label = $RootMargin/RootVBox/InfoCard/InfoVBox/Detected
@onready var _frequency: Label = $RootMargin/RootVBox/InfoCard/InfoVBox/Frequency
@onready var _confidence: Label = $RootMargin/RootVBox/InfoCard/InfoVBox/Confidence
@onready var _status_bar: Label = $RootMargin/RootVBox/StatusBar

@onready var _detector_node: QEngineDetectorNode = $QEngineDetectorNode

var _audio_effect: AudioEffectQEngine = null
var _rustortion_effect = null
var _dataset_player: AudioStreamPlayer = null
var _monitor_player: AudioStreamPlayer = null

var _all_files: PackedStringArray = []
var _playlist: PackedStringArray = []
var _index := 0
var _instrument := "Guitar"
var _playing := true

func _ready() -> void:
	_back_button.pressed.connect(func() -> void: get_tree().change_scene_to_file("res://scenes/main.tscn"))
	_play_pause.pressed.connect(_toggle_play)
	_next_button.pressed.connect(_next_file)
	_instrument_opt.item_selected.connect(_on_instrument_changed)
	_tuning_opt.item_selected.connect(_on_tuning_changed)

	for item in INSTRUMENTS:
		_instrument_opt.add_item(item)
	_instrument_opt.selected = 0
	_populate_tunings()

	_ensure_audio_effect_on_capture_bus()
	_setup_players()
	_load_dataset_files()
	_rebuild_playlist()
	_play_current_file()

func _process(_delta: float) -> void:
	if _playing and _dataset_player and not _dataset_player.playing and _playlist.size() > 1:
		_index = (_index + 1) % _playlist.size()
		_play_current_file()

	if _audio_effect and _playing:
		_update_detected()

func _toggle_play() -> void:
	_playing = not _playing
	_play_pause.text = "Pause" if _playing else "Play"
	if _dataset_player:
		if _playing:
			_dataset_player.play()
			if _monitor_player:
				_monitor_player.play()
		else:
			_dataset_player.stop()
			if _monitor_player:
				_monitor_player.stop()

func _next_file() -> void:
	if _playlist.is_empty():
		return
	_index = (_index + 1) % _playlist.size()
	_play_current_file()

func _on_instrument_changed(index: int) -> void:
	if index < 0 or index >= INSTRUMENTS.size():
		return
	_instrument = INSTRUMENTS[index]
	_populate_tunings()
	_apply_detector_tuning()
	_rebuild_playlist()
	_play_current_file()

func _on_tuning_changed(_index: int) -> void:
	_apply_detector_tuning()
	_rebuild_playlist()
	_play_current_file()

func _populate_tunings() -> void:
	_tuning_opt.clear()
	var tunings: PackedStringArray = GUITAR_TUNINGS if _instrument == "Guitar" else BASS_TUNINGS
	for t in tunings:
		_tuning_opt.add_item(t)
	_tuning_opt.selected = 0

func _selected_tuning() -> String:
	if _tuning_opt.selected < 0:
		return "Standard"
	return _tuning_opt.get_item_text(_tuning_opt.selected)

func _apply_detector_tuning() -> void:
	var tuning: String = _selected_tuning()
	if _instrument == "Bass" and tuning != "DropD":
		tuning = "Standard"
	if _audio_effect:
		_audio_effect.tuning = tuning
	if _detector_node:
		_detector_node.tuning = tuning
		_detector_node.init_detector()

func _ensure_audio_effect_on_capture_bus() -> void:
	var bus_idx := AudioServer.get_bus_index("GuitarIn")
	if bus_idx < 0:
		_status_bar.text = "Status: GuitarIn bus missing"
		return

	for i in AudioServer.get_bus_effect_count(bus_idx):
		var fx := AudioServer.get_bus_effect(bus_idx, i)
		if fx is AudioEffectQEngine:
			_audio_effect = fx
		if _rustortion_effect == null and fx and fx.is_class("AudioEffectRustortion"):
			_rustortion_effect = fx

	if _audio_effect:
		_apply_detector_tuning()
		_ensure_rustortion_on_bus(bus_idx)
		return

	var new_fx := ClassDB.instantiate("AudioEffectQEngine") as AudioEffectQEngine
	if new_fx == null:
		_status_bar.text = "Status: failed to create AudioEffectQEngine"
		return
	new_fx.tuning = _selected_tuning()
	AudioServer.add_bus_effect(bus_idx, new_fx, 0)
	_audio_effect = new_fx
	_ensure_rustortion_on_bus(bus_idx)

func _ensure_rustortion_on_bus(bus_idx: int) -> void:
	if _rustortion_effect != null:
		return
	if not ClassDB.class_exists("AudioEffectRustortion"):
		return

	for i in AudioServer.get_bus_effect_count(bus_idx):
		var fx := AudioServer.get_bus_effect(bus_idx, i)
		if fx and fx.is_class("AudioEffectRustortion"):
			_rustortion_effect = fx
			return

	var rustortion = ClassDB.instantiate("AudioEffectRustortion") as AudioEffect
	if rustortion == null:
		return
	AudioServer.add_bus_effect(bus_idx, rustortion)
	_rustortion_effect = rustortion

func _setup_players() -> void:
	_dataset_player = AudioStreamPlayer.new()
	_dataset_player.bus = "GuitarIn"
	add_child(_dataset_player)

	_monitor_player = AudioStreamPlayer.new()
	_monitor_player.bus = "Master"
	add_child(_monitor_player)

func _load_dataset_files() -> void:
	_all_files = []
	var dataset_dir: String = OS.get_environment("QENGINE_DATASET_DIR")
	if dataset_dir.is_empty():
		dataset_dir = DEFAULT_DATASET_DIR
	var dir := DirAccess.open(dataset_dir)
	if dir == null:
		_status_bar.text = "Status: dataset dir not found (%s)" % dataset_dir
		return

	dir.list_dir_begin()
	while true:
		var name: String = dir.get_next()
		if name.is_empty():
			break
		if dir.current_is_dir():
			continue
		if name.to_lower().ends_with("_solo_mic.wav"):
			_all_files.append(dataset_dir.path_join(name))
	dir.list_dir_end()
	_all_files.sort()

func _rebuild_playlist() -> void:
	var keys: PackedStringArray = _wanted_keys(_instrument, _selected_tuning())
	_playlist = []
	for file_path in _all_files:
		var k: String = _key_class_from_filename(file_path.get_file())
		if keys.has(k):
			_playlist.append(file_path)
	if _playlist.is_empty():
		_playlist = _all_files
	_index = 0

func _play_current_file() -> void:
	if _playlist.is_empty():
		_current_file.text = "File: --"
		_expected.text = "Expected: --"
		_status_bar.text = "Status: no dataset files"
		return

	var path: String = _playlist[_index]
	var stream: AudioStreamWAV = _load_wav(path)
	if stream == null:
		_status_bar.text = "Status: failed to load %s" % path.get_file()
		return

	_dataset_player.stream = stream
	_monitor_player.stream = stream
	if _playing:
		_dataset_player.play()
		_monitor_player.play()

	_current_file.text = "File: %s" % path.get_file()
	_expected.text = "Expected: %s" % _key_class_from_filename(path.get_file())
	_detected.text = "Detected: --"
	_frequency.text = "Frequency: --"
	_confidence.text = "Confidence: --"
	_status_bar.text = "Status: %s / %s" % [_instrument, _selected_tuning()]

func _load_wav(path: String) -> AudioStreamWAV:
	if path.begins_with("res://"):
		return ResourceLoader.load(path, "AudioStreamWAV") as AudioStreamWAV
	return AudioStreamWAV.load_from_file(path)

func _update_detected() -> void:
	var latest: Dictionary = _audio_effect.get_latest_detection()
	var freq: float = float(latest.get("pitch_hz", 0.0))
	var conf: float = float(latest.get("confidence", 0.0))
	var note: String = _note_class_from_midi(int(latest.get("midi_note", -1)))

	var chord_frames: Array = _audio_effect.pop_chord_frames()
	if not chord_frames.is_empty():
		var chord: Dictionary = chord_frames[chord_frames.size() - 1]
		var dom_midi: int = int(chord.get("dominant_midi", -1))
		var dom_note: String = _note_class_from_midi(dom_midi)
		if not dom_note.is_empty():
			note = dom_note
		var dom_freq: float = float(chord.get("dominant_pitch_hz", 0.0))
		if dom_freq > 0.0:
			freq = dom_freq
		var dom_conf: float = float(chord.get("dominant_confidence", 0.0))
		if dom_conf > 0.0:
			conf = dom_conf

	_detected.text = "Detected: %s" % (note if note != "" else "--")
	_frequency.text = "Frequency: %.1f Hz" % freq if freq > 0.0 else "Frequency: --"
	_confidence.text = "Confidence: %.0f%%" % (conf * 100.0)

func _note_class_from_midi(midi: int) -> String:
	if midi < 0 or midi > 127:
		return ""
	return NOTE_NAMES[((midi % 12) + 12) % 12]

func _wanted_keys(instrument: String, tuning: String) -> PackedStringArray:
	if instrument == "Bass":
		if tuning == "DropD":
			return ["D", "A", "G"]
		return ["E", "A", "D", "G"]

	match tuning:
		"Standard":
			return ["E"]
		"DropD":
			return ["D"]
		"OpenD":
			return ["D", "F#", "A"]
		"DropC":
			return ["C", "D#", "F", "G", "A"]
		_:
			return ["E"]

func _key_class_from_filename(file_name: String) -> String:
	var parts: PackedStringArray = file_name.split("-")
	if parts.size() < 3:
		return ""
	var note_part: String = String(parts[2]).split("_")[0]
	if note_part.ends_with("m"):
		note_part = note_part.left(note_part.length() - 1)
	return _normalize_note(note_part)

func _normalize_note(note: String) -> String:
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
