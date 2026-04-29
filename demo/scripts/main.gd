## main.gd – Demo scene controller for QEngine guitar pitch detection.
## Displays per-string detection results in a grid of UI labels.
extends Control

# ─── tuning options ──────────────────────────────────────────────────────────
const TUNINGS: PackedStringArray = ["Standard", "DropD", "OpenD", "DropC", "DADGAD"]

# ─── node refs ──────────────────────────────────────────────────────────────
@onready var _tuning_opt    : OptionButton = $VBox/TuningHBox/TuningOption
@onready var _thresh_slider : HSlider      = $VBox/ThresholdHBox/ThresholdSlider
@onready var _thresh_label  : Label        = $VBox/ThresholdHBox/ThresholdValue
@onready var _strings_grid  : GridContainer = $VBox/StringsGrid
@onready var _status_bar    : Label        = $VBox/StatusBar
@onready var _detector_node: QEngineDetectorNode = $QEngineDetectorNode

# Per-band UI labels: [string_lbl, freq_lbl, note_lbl, cents_lbl, conf_lbl]
var _band_labels : Array = []

# ─── AudioEffect reference on the Master bus ────────────────────────────────
var _audio_effect: AudioEffectQEngine = null

# ─── _ready ─────────────────────────────────────────────────────────────────
func _ready() -> void:
	# Populate tuning dropdown
	for t in TUNINGS:
		_tuning_opt.add_item(t)
	_tuning_opt.selected = 0
	_tuning_opt.item_selected.connect(_on_tuning_changed)

	# Threshold slider
	_thresh_slider.value_changed.connect(_on_threshold_changed)

	# Build per-band label rows
	_build_string_labels()

	# Ensure AudioEffectQEngine is present on the Master bus.
	_ensure_audio_effect_on_master_bus()

	# Connect detector node signal (fallback path)
	if _detector_node and _detector_node.has_signal("notes_detected"):
		_detector_node.connect("notes_detected", _on_notes_detected)

func _ensure_audio_effect_on_master_bus() -> void:
	var bus_idx := AudioServer.get_bus_index("Master")
	if bus_idx < 0:
		_status_bar.text = "Status: Master bus not found – using QEngineDetectorNode"
		return

	for i in AudioServer.get_bus_effect_count(bus_idx):
		var fx := AudioServer.get_bus_effect(bus_idx, i)
		if fx is AudioEffectQEngine:
			_audio_effect = fx
			_status_bar.text = "Status: AudioEffectQEngine found on Master bus"
			return
		if fx and fx.has_method("poll_notes"):
			_audio_effect = fx
			_status_bar.text = "Status: AudioEffectQEngine found on Master bus"
			return

	var new_fx := ClassDB.instantiate("AudioEffectQEngine") as AudioEffectQEngine
	if new_fx == null:
		_status_bar.text = "Status: could not instantiate AudioEffectQEngine – using QEngineDetectorNode"
		return

	new_fx.tuning = TUNINGS[_tuning_opt.selected]
	new_fx.threshold_db = _thresh_slider.value
	AudioServer.add_bus_effect(bus_idx, new_fx, 0)
	_audio_effect = new_fx
	_status_bar.text = "Status: AudioEffectQEngine added to Master bus"

# ─── _process ───────────────────────────────────────────────────────────────
func _process(_delta: float) -> void:
	# Preferred path: poll the AudioEffectQEngine directly if available
	if _audio_effect:
		_on_notes_detected(_audio_effect.poll_notes())

# ─── UI callbacks ────────────────────────────────────────────────────────────
func _on_tuning_changed(index: int) -> void:
	if _audio_effect:
		_audio_effect.tuning = String(TUNINGS[index])
	if _detector_node:
		_detector_node.tuning = String(TUNINGS[index])
		_detector_node.init_detector()

func _on_threshold_changed(val: float) -> void:
	_thresh_label.text = "%.0f dB" % val
	if _audio_effect:
		_audio_effect.threshold_db = val

# ─── note display ────────────────────────────────────────────────────────────
func _on_notes_detected(notes: Array) -> void:
	# notes is an Array of Dictionary (from poll_notes / notes_detected signal)
	for item in notes:
		var band : int   = item.get("band", -1)
		if band < 0 or band >= _band_labels.size():
			continue
		var row   : Array = _band_labels[band]
		var freq  : float = item.get("frequency", 0.0)
		var note  : String = item.get("note", "")
		var cents : float = item.get("cents", 0.0)
		var conf  : float = item.get("periodicity", 0.0)

		row[1].text = "%.1f Hz" % freq if freq > 0.0 else "—"
		row[2].text = note if note != "" else "—"
		row[3].text = "%.1f ¢" % cents if note != "" else "—"
		row[4].text = "%.0f%%" % (conf * 100.0)
		# Colour the note label: green if confident, grey otherwise
		row[2].modulate = Color.GREEN if conf >= 0.8 else Color(0.6, 0.6, 0.6)

# ─── UI builder ──────────────────────────────────────────────────────────────
func _build_string_labels() -> void:
	# Header row
	for h in ["String", "Frequency", "Note", "Cents", "Conf."]:
		var lbl := Label.new()
		lbl.text = h
		lbl.add_theme_font_size_override("font_size", 13)
		lbl.modulate = Color(0.8, 0.8, 1.0)
		_strings_grid.add_child(lbl)

	# 6 data rows
	var string_labels := ["6 (Low)", "5", "4", "3", "2", "1 (High)"]
	for i in 6:
		var row : Array = []
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
