## main.gd – Demo scene controller for QEngine guitar pitch detection.
## Displays per-string detection results in a grid of UI labels.
##
## The C++ layer returns raw Q pitch-detector results (frequency + periodicity).
## This script handles tuning definitions and note-name mapping.
extends Control

# ── Tuning definitions ────────────────────────────────────────────────────────
# band_ranges: 12 floats per tuning – [min0, max0, min1, max1, …, min5, max5]
# Index 0 = lowest string.  Bounds: half-semitone below open, 2 octaves above.

const TUNING_NAMES: PackedStringArray = ["Standard", "DropD", "OpenD", "DropC", "DADGAD"]

var TUNING_DATA := {
	"Standard": PackedFloat32Array([
		 80.11,  329.64,   # string 6: E2  82.41 Hz
		106.87,  440.00,   # string 5: A2  110.00 Hz
		142.65,  587.32,   # string 4: D3  146.83 Hz
		190.42,  784.00,   # string 3: G3  196.00 Hz
		239.91,  987.76,   # string 2: B3  246.94 Hz
		320.25, 1318.52,   # string 1: E4  329.63 Hz
	]),
	"DropD": PackedFloat32Array([
		 71.33,  293.68,   # string 6: D2  73.42 Hz
		106.87,  440.00,
		142.65,  587.32,
		190.42,  784.00,
		239.91,  987.76,
		320.25, 1318.52,
	]),
	"OpenD": PackedFloat32Array([
		 71.33,  293.68,   # string 6: D2  73.42 Hz
		106.87,  440.00,   # string 5: A2
		142.65,  587.32,   # string 4: D3
		179.73,  740.00,   # string 3: F#3 185.00 Hz
		213.74,  880.00,   # string 2: A3  220.00 Hz
		285.30, 1174.64,   # string 1: D4  293.66 Hz
	]),
	"DropC": PackedFloat32Array([
		 63.54,  261.64,   # string 6: C2  65.41 Hz
		 95.21,  392.00,   # string 5: G2  98.00 Hz
		127.09,  523.24,   # string 4: C3  130.81 Hz
		169.64,  698.44,   # string 3: F3  174.61 Hz
		213.74,  880.00,   # string 2: A3  220.00 Hz
		285.30, 1174.64,   # string 1: D4  293.66 Hz
	]),
	"DADGAD": PackedFloat32Array([
		 71.33,  293.68,   # string 6: D2  73.42 Hz
		106.87,  440.00,   # string 5: A2
		142.65,  587.32,   # string 4: D3
		190.42,  784.00,   # string 3: G3
		213.74,  880.00,   # string 2: A3  220.00 Hz
		285.30, 1174.64,   # string 1: D4  293.66 Hz
	]),
}

# Open-string note labels per tuning, used for the "String" column UI.
const TUNING_STRING_LABELS := {
	"Standard": ["E2", "A2", "D3", "G3", "B3", "E4"],
	"DropD":    ["D2", "A2", "D3", "G3", "B3", "E4"],
	"OpenD":    ["D2", "A2", "D3", "F#3", "A3", "D4"],
	"DropC":    ["C2", "G2", "C3", "F3",  "A3", "D4"],
	"DADGAD":   ["D2", "A2", "D3", "G3",  "A3", "D4"],
}

# ── Note-name lookup ─────────────────────────────────────────────────────────
const NOTE_NAMES := ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

## Returns "E2", "G#3", etc. from a MIDI note number, or "" if out of range.
static func midi_to_note_display(midi: int) -> String:
	if midi < 0 or midi > 127:
		return ""
	var note_idx: int = ((midi % 12) + 12) % 12
	var octave: int   = int(float(midi) / 12.0) - 1
	return NOTE_NAMES[note_idx] + str(octave)

# ── Scene references ──────────────────────────────────────────────────────────

const DEFAULT_DATASET_DIR := "res://tests/dataset/guitarset/audio/mic"
## Only display detections with periodicity >= this threshold.
const MIN_CONFIDENCE       := 0.85
## Project sample rate (must match project.godot audio/driver/mix_rate).
const SAMPLE_RATE          := 48000.0

@onready var _mode_opt:      OptionButton   = $VBox/ModeHBox/ModeOption
@onready var _tuning_opt:    OptionButton   = $VBox/TuningHBox/TuningOption
@onready var _thresh_slider: HSlider        = $VBox/ThresholdHBox/ThresholdSlider
@onready var _thresh_label:  Label          = $VBox/ThresholdHBox/ThresholdValue
@onready var _strings_grid:  GridContainer  = $VBox/StringsGrid
@onready var _status_bar:    Label          = $VBox/StatusBar
@onready var _detector_node: Node           = $QEngineDetectorNode
@onready var _guitar_in_player: AudioStreamPlayer = $GuitarInPlayer

var _band_labels: Array = []

var _audio_effect = null
var _dataset_player: AudioStreamPlayer = null
var _dataset_monitor_player: AudioStreamPlayer = null
var _dataset_single_file: bool = false
var _dataset_single_path: String = ""
var _dataset_all_files: PackedStringArray = []
var _dataset_files: PackedStringArray = []
var _dataset_index: int = 0

## 0 = Playback (dataset files routed through Guitar In), 1 = Input (live mic).
var _mode: int = 0

func _ready() -> void:
	# Populate mode selector – Playback first so dataset runs on launch.
	_mode_opt.add_item("Playback")
	_mode_opt.add_item("Input")
	_mode_opt.selected = 0
	_mode_opt.item_selected.connect(_on_mode_changed)

	for t in TUNING_NAMES:
		_tuning_opt.add_item(t)
	_tuning_opt.selected = 0
	_tuning_opt.item_selected.connect(_on_tuning_changed)

	_thresh_slider.value_changed.connect(_on_threshold_changed)

	_build_string_labels()
	_ensure_audio_effect_on_capture_bus()
	_setup_dataset_playback()

	if _detector_node and _detector_node.has_signal("notes_detected"):
		_detector_node.set("band_ranges", TUNING_DATA[TUNING_NAMES[_tuning_opt.selected]])
		_detector_node.set("sample_rate", SAMPLE_RATE)
		_detector_node.set("min_periodicity", MIN_CONFIDENCE)
		_detector_node.connect("notes_detected", _on_notes_detected)

	# Start in Playback mode – mic player off.
	_guitar_in_player.stop()

func _ensure_audio_effect_on_capture_bus() -> void:
	var bus_idx := AudioServer.get_bus_index("Guitar In")
	if bus_idx < 0:
		_status_bar.text = "Status: Guitar In bus not found – using QEngineDetectorNode"
		return

	for i in AudioServer.get_bus_effect_count(bus_idx):
		var fx := AudioServer.get_bus_effect(bus_idx, i)
		if fx and fx.has_method("poll_notes"):
			_audio_effect = fx
			# Always apply current configuration so detection works immediately.
			_audio_effect.set("band_ranges",     TUNING_DATA[TUNING_NAMES[_tuning_opt.selected]])
			_audio_effect.set("sample_rate",     SAMPLE_RATE)
			_audio_effect.set("min_periodicity", MIN_CONFIDENCE)
			_audio_effect.set("threshold_db",    _thresh_slider.value)
			_status_bar.text = "Status: AudioEffectQEngine found on Guitar In bus"
			return

	var new_fx: AudioEffect = ClassDB.instantiate("AudioEffectQEngine") as AudioEffect
	if new_fx == null:
		_status_bar.text = "Status: could not instantiate AudioEffectQEngine – using QEngineDetectorNode"
		return

	new_fx.set("band_ranges",     TUNING_DATA[TUNING_NAMES[_tuning_opt.selected]])
	new_fx.set("sample_rate",     SAMPLE_RATE)
	new_fx.set("min_periodicity", MIN_CONFIDENCE)
	new_fx.set("threshold_db",    _thresh_slider.value)
	AudioServer.add_bus_effect(bus_idx, new_fx, 0)
	_audio_effect = new_fx
	_status_bar.text = "Status: AudioEffectQEngine added to Guitar In bus"

func _setup_dataset_playback() -> void:
	_dataset_single_file = false
	_dataset_single_path = ""
	_dataset_all_files = []
	_dataset_files = []
	_dataset_index = 0

	var dataset_file: String = OS.get_environment("QENGINE_DATASET_FILE")
	if dataset_file.is_empty():
		var dataset_dir: String = OS.get_environment("QENGINE_DATASET_DIR")
		if dataset_dir.is_empty():
			dataset_dir = DEFAULT_DATASET_DIR

		var dir := DirAccess.open(dataset_dir)
		if dir == null:
			_status_bar.text = "Status: dataset dir not found"
			return

		var wav_files: PackedStringArray = []
		dir.list_dir_begin()
		while true:
			var entry: String = dir.get_next()
			if entry.is_empty():
				break
			if dir.current_is_dir():
				continue
			if entry.to_lower().ends_with("_solo_mic.wav"):
				wav_files.append(entry)
		dir.list_dir_end()

		if wav_files.is_empty():
			_status_bar.text = "Status: no *_solo_mic.wav files in dataset dir"
			return

		wav_files.sort()
		for wav in wav_files:
			_dataset_all_files.append(dataset_dir.path_join(wav))

		_rebuild_dataset_playlist()
		if _dataset_files.is_empty():
			_dataset_files = _dataset_all_files
		dataset_file = _dataset_files[0]
	else:
		_dataset_single_file = true
		_dataset_single_path = dataset_file
		_dataset_files = [dataset_file]

	var stream: AudioStreamWAV = _load_wav(dataset_file)
	if stream == null:
		_status_bar.text = "Status: failed to load dataset WAV"
		return

	if _dataset_player == null:
		_dataset_player = AudioStreamPlayer.new()
		_dataset_player.bus = "Guitar In"    # routes through AudioEffectQEngine for detection
		add_child(_dataset_player)
	if _dataset_monitor_player == null:
		_dataset_monitor_player = AudioStreamPlayer.new()
		_dataset_monitor_player.bus = "Playback"  # dedicated Playback bus → Master for output
		add_child(_dataset_monitor_player)

	_dataset_player.stream = stream
	_dataset_monitor_player.stream = stream
	_dataset_player.play()
	_dataset_monitor_player.play()
	_status_bar.text = "Status: Playback [%s] – %s" % [
		String(TUNING_NAMES[_tuning_opt.selected]),
		dataset_file.get_file(),
	]

func _load_wav(path: String) -> AudioStreamWAV:
	if path.begins_with("res://"):
		return ResourceLoader.load(path, "AudioStreamWAV") as AudioStreamWAV
	return AudioStreamWAV.load_from_file(path)

func _process(_delta: float) -> void:
	if _audio_effect:
		_on_notes_detected(_audio_effect.poll_notes())

	# Advance dataset playlist when current track ends (Playback mode only).
	if _mode == 0 and _dataset_player and _dataset_files.size() > 1 and not _dataset_player.playing:
		_dataset_index = (_dataset_index + 1) % _dataset_files.size()
		var next_stream: AudioStreamWAV = _load_wav(_dataset_files[_dataset_index])
		if next_stream:
			_dataset_player.stream = next_stream
			if _dataset_monitor_player:
				_dataset_monitor_player.stream = next_stream
			_dataset_player.play()
			if _dataset_monitor_player:
				_dataset_monitor_player.play()

func _on_mode_changed(index: int) -> void:
	_mode = index
	if _mode == 0:
		# Playback mode: stop mic, start dataset.
		_guitar_in_player.stop()
		if _dataset_player and _dataset_files.size() > 0:
			var stream: AudioStreamWAV = _load_wav(_dataset_files[_dataset_index])
			if stream:
				_dataset_player.stream = stream
				if _dataset_monitor_player:
					_dataset_monitor_player.stream = stream
				_dataset_player.play()
				if _dataset_monitor_player:
					_dataset_monitor_player.play()
		_status_bar.text = "Status: Playback – dataset on Guitar In bus"
	else:
		# Input mode: stop dataset, start live mic.
		if _dataset_player:
			_dataset_player.stop()
		if _dataset_monitor_player:
			_dataset_monitor_player.stop()
		_guitar_in_player.play()
		_status_bar.text = "Status: Input – live mic/guitar on Guitar In bus"

func _on_tuning_changed(index: int) -> void:
	var tuning_name: String = TUNING_NAMES[index]
	var ranges: PackedFloat32Array = TUNING_DATA[tuning_name]
	if _audio_effect:
		_audio_effect.set("band_ranges", ranges)
	if _detector_node:
		_detector_node.set("band_ranges", ranges)  # auto-triggers init_detector()
	_rebuild_dataset_playlist()

func _on_threshold_changed(val: float) -> void:
	_thresh_label.text = "%.0f dB" % val
	if _audio_effect:
		_audio_effect.threshold_db = val

func _rebuild_dataset_playlist() -> void:
	if _dataset_single_file:
		_dataset_files = [_dataset_single_path]
		_dataset_index = 0
		return
	if _dataset_all_files.is_empty():
		return

	var selected_tuning: String = String(TUNING_NAMES[_tuning_opt.selected])
	var wanted_keys: PackedStringArray = _preferred_dataset_keys_for_tuning(selected_tuning)
	var filtered: PackedStringArray = []
	for path in _dataset_all_files:
		var key_class: String = _key_class_from_filename(path.get_file())
		if wanted_keys.has(key_class):
			filtered.append(path)
	if filtered.is_empty():
		filtered = _dataset_all_files

	_dataset_files = filtered
	_dataset_index = 0

	if _mode == 0 and _dataset_player and _dataset_files.size() > 0:
		var stream: AudioStreamWAV = _load_wav(_dataset_files[0])
		if stream:
			_dataset_player.stream = stream
			if _dataset_monitor_player:
				_dataset_monitor_player.stream = stream
			_dataset_player.play()
			if _dataset_monitor_player:
				_dataset_monitor_player.play()

func _preferred_dataset_keys_for_tuning(tuning: String) -> PackedStringArray:
	match tuning:
		"Standard":
			return ["E"]
		"DropD":
			return ["D"]
		"OpenD":
			return ["D", "F#", "A"]
		"DropC":
			return ["C", "D#", "F", "G", "A"]
		"DADGAD":
			return ["D", "G", "A"]
		_:
			return []

func _key_class_from_filename(file_name: String) -> String:
	var parts: PackedStringArray = file_name.split("-")
	if parts.size() < 3:
		return ""
	var note_part: String = String(parts[2]).split("_")[0]
	if note_part.ends_with("m"):
		note_part = note_part.left(note_part.length() - 1)
	return _normalize_note_class(note_part)

func _normalize_note_class(note: String) -> String:
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
		_:    return note

func _on_notes_detected(notes: Array) -> void:
	for band in range(min(notes.size(), _band_labels.size())):
		var item: Dictionary = notes[band]
		var row: Array   = _band_labels[band]
		var freq: float  = item.get("frequency", 0.0)
		var conf: float  = item.get("periodicity", 0.0)
		var midi: int    = item.get("midi_note", -1)
		var cents: float = item.get("cents", 0.0)

		if midi == -1 or freq <= 0.0:
			row[1].text = "—"
			row[2].text = "—"
			row[3].text = "—"
			row[4].text = "—"
			row[2].modulate = Color(0.6, 0.6, 0.6)
		else:
			var note: String = midi_to_note_display(midi)
			row[1].text = "%.1f Hz" % freq
			row[2].text = note if note != "" else "—"
			row[3].text = "%.1f ¢" % cents if note != "" else "—"
			row[4].text = "%.0f%%" % (conf * 100.0)
			row[2].modulate = Color.GREEN

func _build_string_labels() -> void:
	for h in ["String", "Frequency", "Note", "Cents", "Conf."]:
		var lbl := Label.new()
		lbl.text = h
		lbl.add_theme_font_size_override("font_size", 13)
		lbl.modulate = Color(0.8, 0.8, 1.0)
		_strings_grid.add_child(lbl)

	var string_labels := ["6 (Low)", "5", "4", "3", "2", "1 (High)"]
	for i in 6:
		var row: Array = []
		for col in 5:
			var lbl := Label.new()
			lbl.custom_minimum_size = Vector2(100, 0)
			lbl.add_theme_font_size_override("font_size", 14)
			if col == 0:
				lbl.text = string_labels[i]
			else:
				lbl.text = "—"
			_strings_grid.add_child(lbl)
			row.append(lbl)
		_band_labels.append(row)
