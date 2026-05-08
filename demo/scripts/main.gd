## main.gd – Demo scene controller for QEngine guitar pitch detection.
##
## Architecture v2: the C++ AudioEffect is an analysis engine, not a gameplay
## judge.  This script demonstrates the three-tier consumption model:
##
##   1. get_latest_detection() → tuner/debug UI (reads best pitch each frame)
##   2. pop_note_events()      → chart-aware attack judgment (onset events)
##   3. get_frame_history(n)   → sustain / bend / vibrato tracking
##
## GDScript owns all chart-aware hit/miss logic; the C++ layer only reports
## what the audio analysis observed.
extends Control

# ── Tuning definitions ────────────────────────────────────────────────────────
# band_ranges: 12 floats per tuning – [min0, max0, min1, max1, …, min5, max5]
# Index 0 = lowest string. Bounds: half-semitone below open, 1 octave above.

const TUNING_NAMES: PackedStringArray = ["Standard", "DropD", "OpenD", "DropC", "DADGAD"]

var TUNING_DATA := {
	"Standard": PackedFloat32Array([
		 80.11,  164.82,   # string 6: E2  82.41 Hz
		106.87,  220.00,   # string 5: A2  110.00 Hz
		142.65,  293.66,   # string 4: D3  146.83 Hz
		190.42,  392.00,   # string 3: G3  196.00 Hz
		239.91,  493.88,   # string 2: B3  246.94 Hz
		320.25,  659.26,   # string 1: E4  329.63 Hz
	]),
	"DropD": PackedFloat32Array([
		 71.33,  146.84,   # string 6: D2  73.42 Hz
		106.87,  220.00,
		142.65,  293.66,
		190.42,  392.00,
		239.91,  493.88,
		320.25,  659.26,
	]),
	"OpenD": PackedFloat32Array([
		 71.33,  146.84,   # string 6: D2  73.42 Hz
		106.87,  220.00,   # string 5: A2
		142.65,  293.66,   # string 4: D3
		179.73,  370.00,   # string 3: F#3 185.00 Hz
		213.74,  440.00,   # string 2: A3  220.00 Hz
		285.30,  587.32,   # string 1: D4  293.66 Hz
	]),
	"DropC": PackedFloat32Array([
		 63.54,  130.82,   # string 6: C2  65.41 Hz
		 95.21,  196.00,   # string 5: G2  98.00 Hz
		127.09,  261.62,   # string 4: C3  130.81 Hz
		169.64,  349.22,   # string 3: F3  174.61 Hz
		213.74,  440.00,   # string 2: A3  220.00 Hz
		285.30,  587.32,   # string 1: D4  293.66 Hz
	]),
	"DADGAD": PackedFloat32Array([
		 71.33,  146.84,   # string 6: D2  73.42 Hz
		106.87,  220.00,   # string 5: A2
		142.65,  293.66,   # string 4: D3
		190.42,  392.00,   # string 3: G3
		213.74,  440.00,   # string 2: A3  220.00 Hz
		285.30,  587.32,   # string 1: D4  293.66 Hz
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
## Only display detections with confidence >= this threshold.
const MIN_CONFIDENCE       := 0.85
## Project sample rate (must match project.godot audio/driver/mix_rate).
const SAMPLE_RATE          := 48000.0
## Chart hit window: accept onsets within ±HIT_WINDOW_SEC of a chart note.
const HIT_WINDOW_SEC       := 0.120
## Sustain history depth: read last N frames for sustain/bend checking.
const HISTORY_FRAMES       := 30

@onready var _mode_opt:      OptionButton   = $VBox/ModeHBox/ModeOption
@onready var _tuning_opt:    OptionButton   = $VBox/TuningHBox/TuningOption
@onready var _thresh_slider: HSlider        = $VBox/ThresholdHBox/ThresholdSlider
@onready var _thresh_label:  Label          = $VBox/ThresholdHBox/ThresholdValue
@onready var _strings_grid:  GridContainer  = $VBox/StringsGrid
@onready var _status_bar:    Label          = $VBox/StatusBar
@onready var _detector_node: Node           = $QEngineDetectorNode
@onready var _guitar_in_player: AudioStreamPlayer = $GuitarInPlayer

var _band_labels: Array = []

## Chord panel: rows of [StringLabel, NoteLabel, ConfLabel, ActiveLabel]
var _chord_labels: Array = []
var _dominant_label: Label = null

var _audio_effect = null
var _dataset_player: AudioStreamPlayer = null
var _dataset_monitor_player: AudioStreamPlayer = null
var _dataset_single_file: bool = false
var _dataset_single_path: String = ""
var _dataset_all_files: PackedStringArray = []
var _dataset_files: PackedStringArray = []
var _dataset_index: int = 0

## 0 = Playback (dataset files routed through GuitarIn), 1 = Input (live mic).
var _mode: int = 0

# ── Minimal chart for demo purposes ──────────────────────────────────────────
# Each entry: { time_sec, midi_note, duration_sec }
# In a real game these come from a parsed chart file.
var _demo_chart: Array = []
var _song_time: float = 0.0
var _last_event_log: String = ""

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
	_build_chord_panel()
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
	var bus_idx := AudioServer.get_bus_index("GuitarIn")
	if bus_idx < 0:
		_status_bar.text = "Status: GuitarIn bus not found – using QEngineDetectorNode"
		return

	for i in AudioServer.get_bus_effect_count(bus_idx):
		var fx := AudioServer.get_bus_effect(bus_idx, i)
		if fx and fx.has_method("poll_notes"):
			_audio_effect = fx
			_audio_effect.set("band_ranges",     TUNING_DATA[TUNING_NAMES[_tuning_opt.selected]])
			_audio_effect.set("sample_rate",     SAMPLE_RATE)
			_audio_effect.set("min_periodicity", MIN_CONFIDENCE)
			_audio_effect.set("threshold_db",    _thresh_slider.value)
			_status_bar.text = "Status: AudioEffectQEngine found on GuitarIn bus"
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
	_status_bar.text = "Status: AudioEffectQEngine added to GuitarIn bus"

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
		_dataset_player.bus = "GuitarIn"
		add_child(_dataset_player)
	if _dataset_monitor_player == null:
		_dataset_monitor_player = AudioStreamPlayer.new()
		_dataset_monitor_player.bus = "Playback"
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

func _process(delta: float) -> void:
	_song_time += delta

	if _audio_effect:
		# ── Tier 1: per-string tuner UI via legacy poll_notes() ──────────────
		_on_notes_detected(_audio_effect.poll_notes())

		# ── Tier 2: chart-aware attack judgment via SPSC onset event queue ───
		# pop_note_events() drains the SPSC onset ring buffer filled by the
		# C++ analysis layer.  We compare each event to the active chart note.
		var events: Array = _audio_effect.pop_note_events()
		for ev in events:
			_judge_note_event(ev)

		# ── Tier 3: sustain / bend tracking via frame history ─────────────────
		# get_frame_history() returns the last N DetectionFrames (newest first)
		# from the SPSC circular history buffer.  Use this to check whether the
		# player is sustaining the expected pitch after an attack was confirmed.
		# (Shown here as a debug line; a real game would apply scoring logic.)
		var history: Array = _audio_effect.get_frame_history(HISTORY_FRAMES)
		_update_sustain_debug(history)

		# ── Tier 4: per-string chord detection via ChordFrame queue ───────────
		# pop_chord_frames() drains the SPSC chord ring buffer.  Each ChordFrame
		# carries the per-string detection snapshot (active/inactive, MIDI, Hz,
		# confidence) and a dominant/root note.  Use this for Rocksmith-style
		# per-string chord matching.  Only the latest frame is shown in the UI.
		var chord_frames: Array = _audio_effect.pop_chord_frames()
		if not chord_frames.is_empty():
			_update_chord_panel(chord_frames[chord_frames.size() - 1])

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

# ── Chart-aware judgment helpers ──────────────────────────────────────────────

## Judge one onset event against the demo chart.
## In a real game _demo_chart would be loaded from a chart file; here we
## just log each detected onset so the flow is visible.
func _judge_note_event(ev: Dictionary) -> void:
	var ev_midi: int    = int(ev.get("midi_note", -1))
	var ev_time: float  = float(ev.get("time_sec", 0.0))
	var ev_conf: float  = float(ev.get("confidence", 0.0))

	if ev_midi < 0 or ev_conf < MIN_CONFIDENCE:
		return

	# Find a matching chart note within the hit window.
	var hit_note: Dictionary = {}
	for chart_note in _demo_chart:
		var dt: float = abs(float(chart_note.get("time_sec", 0.0)) - ev_time)
		if dt <= HIT_WINDOW_SEC and int(chart_note.get("midi_note", -1)) == ev_midi:
			hit_note = chart_note
			break

	var note_name: String = midi_to_note_display(ev_midi)
	if hit_note.is_empty():
		_last_event_log = "onset: %s (midi %d) @ %.3f s  [no chart match]" % [note_name, ev_midi, ev_time]
	else:
		_last_event_log = "HIT:   %s (midi %d) @ %.3f s  conf=%.2f" % [note_name, ev_midi, ev_time, ev_conf]

## Check whether the player is sustaining a pitch from the history buffer.
## 'history' is newest-first; index 0 is the most recent frame.
func _update_sustain_debug(history: Array) -> void:
	if history.is_empty():
		return
	var valid_count: int = 0
	var pitch_sum: float = 0.0
	for frame in history:
		if bool(frame.get("pitch_valid", false)):
			valid_count += 1
			pitch_sum += float(frame.get("pitch_hz", 0.0))
	if valid_count == 0:
		return
	# Average pitch over recent frames – useful to detect bends / vibrato.
	@warning_ignore("integer_division")
	var avg_hz: float = pitch_sum / float(valid_count)
	# In a real game: compare avg_hz against the expected chart note's pitch
	# to determine sustain/bend accuracy.  Here we just update the status bar
	# when the last-event log is empty (so it doesn't overwrite HIT messages).
	if _last_event_log.is_empty():
		_status_bar.text = "sustain avg: %.1f Hz  (%d/%d valid frames)" % [avg_hz, valid_count, history.size()]

# ── Mode / tuning / threshold callbacks ──────────────────────────────────────

func _on_mode_changed(index: int) -> void:
	_mode = index
	_song_time = 0.0
	if _mode == 0:
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
		_status_bar.text = "Status: Playback – dataset on GuitarIn bus"
	else:
		if _dataset_player:
			_dataset_player.stop()
		if _dataset_monitor_player:
			_dataset_monitor_player.stop()
		_guitar_in_player.play()
		_status_bar.text = "Status: Input – live mic/guitar on GuitarIn bus"

func _on_tuning_changed(index: int) -> void:
	var tuning_name: String = TUNING_NAMES[index]
	var ranges: PackedFloat32Array = TUNING_DATA[tuning_name]
	if _audio_effect:
		_audio_effect.set("band_ranges", ranges)
	if _detector_node:
		_detector_node.set("band_ranges", ranges)
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

# ── Per-string tuner UI update (Tier 1) ──────────────────────────────────────

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

	# Show latest detection snapshot in status bar (Tier 1 debug info).
	if _audio_effect and _audio_effect.has_method("get_latest_detection"):
		var det: Dictionary = _audio_effect.get_latest_detection()
		if bool(det.get("pitch_valid", false)):
			var snap_note: String = midi_to_note_display(int(det.get("midi_note", -1)))
			var snap_conf: float  = float(det.get("confidence", 0.0))
			var snap_hz: float    = float(det.get("pitch_hz", 0.0))
			var onset_flag: String = " [ONSET]" if bool(det.get("onset", false)) else ""
			_status_bar.text = "Detection: %s  %.1f Hz  conf %.0f%%%s" % [
				snap_note, snap_hz, snap_conf * 100.0, onset_flag
			]
			if not _last_event_log.is_empty():
				_status_bar.text += "  |  " + _last_event_log
				_last_event_log = ""

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

## Build the per-string chord-detection panel (Tier 4 / ChordFrame display).
## Creates a GridContainer with a header row + 6 string rows + a dominant note
## label, appended dynamically below the status bar.
func _build_chord_panel() -> void:
	var vbox: VBoxContainer = get_node_or_null("VBox") as VBoxContainer
	if vbox == null:
		return

	# Section heading
	var heading := Label.new()
	heading.text = "Per-String Chord Detection (Tier 4)"
	heading.add_theme_font_size_override("font_size", 13)
	heading.modulate = Color(0.8, 0.8, 1.0)
	vbox.add_child(heading)

	# Header row
	var grid := GridContainer.new()
	grid.columns = 5
	vbox.add_child(grid)

	for h in ["String", "Note", "Hz", "Conf.", "Active?"]:
		var hdr := Label.new()
		hdr.text = h
		hdr.add_theme_font_size_override("font_size", 13)
		hdr.modulate = Color(0.8, 0.8, 1.0)
		grid.add_child(hdr)

	# String rows
	var string_labels := ["6 (Low)", "5", "4", "3", "2", "1 (High)"]
	for i in 6:
		var row: Array = []
		for col in 5:
			var lbl := Label.new()
			lbl.custom_minimum_size = Vector2(90, 0)
			lbl.add_theme_font_size_override("font_size", 14)
			lbl.text = string_labels[i] if col == 0 else "—"
			grid.add_child(lbl)
			row.append(lbl)
		_chord_labels.append(row)

	# Dominant / root note label
	_dominant_label = Label.new()
	_dominant_label.text = "Dominant: —"
	_dominant_label.add_theme_font_size_override("font_size", 13)
	_dominant_label.modulate = Color(1.0, 1.0, 0.6)
	vbox.add_child(_dominant_label)

## Update the chord panel with the latest ChordFrame dictionary.
## Each frame has: time_sec, level, dominant_band, dominant_midi,
##   dominant_pitch_hz, dominant_confidence, active_count,
##   strings: Array[6] of { band, pitch_hz, midi_float, midi_note,
##                          confidence, cents, active }.
## Color coding:
##   Green  = string active (detected above min_periodicity threshold)
##   Grey   = string inactive / muted
func _update_chord_panel(cf: Dictionary) -> void:
	if _chord_labels.is_empty():
		return

	var strings: Array = cf.get("strings", [])
	for i in min(strings.size(), _chord_labels.size()):
		var sc: Dictionary = strings[i]
		var row: Array = _chord_labels[i]
		var active: bool  = bool(sc.get("active", false))
		var midi: int     = int(sc.get("midi_note", -1))
		var hz: float     = float(sc.get("pitch_hz", 0.0))
		var conf: float   = float(sc.get("confidence", 0.0))

		if active and midi >= 0:
			row[1].text     = midi_to_note_display(midi)
			row[2].text     = "%.1f" % hz
			row[3].text     = "%.0f%%" % (conf * 100.0)
			row[4].text     = "YES"
			row[1].modulate = Color.GREEN
			row[4].modulate = Color.GREEN
		else:
			row[1].text     = "—"
			row[2].text     = "—"
			row[3].text     = "—"
			row[4].text     = "muted"
			row[1].modulate = Color(0.5, 0.5, 0.5)
			row[4].modulate = Color(0.4, 0.4, 0.4)

	# Update dominant / root note line.
	if _dominant_label:
		var dom_midi: int   = int(cf.get("dominant_midi", -1))
		var dom_conf: float = float(cf.get("dominant_confidence", 0.0))
		var active_n: int   = int(cf.get("active_count", 0))
		if dom_midi >= 0:
			var dom_note: String = midi_to_note_display(dom_midi)
			_dominant_label.text = "Dominant: %s  conf %.0f%%  (%d string%s active)" % [
				dom_note, dom_conf * 100.0, active_n, "s" if active_n != 1 else ""
			]
		else:
			_dominant_label.text = "Dominant: —"
