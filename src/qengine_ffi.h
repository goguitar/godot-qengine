#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
  #ifdef QENGINE_FFI_EXPORTS
    #define QENGINE_FFI_API __declspec(dllexport)
  #else
    #define QENGINE_FFI_API __declspec(dllimport)
  #endif
#else
  #define QENGINE_FFI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QEngineHandle QEngineHandle;

typedef struct QEngineBandRange {
    float freq_min;
    float freq_max;
} QEngineBandRange;

typedef struct QEngineDetectionFrame {
    double time_sec;
    float pitch_hz;
    float midi_float;
    int32_t midi_note;
    float confidence;
    float level;
    uint8_t onset;
    uint8_t pitch_valid;
} QEngineDetectionFrame;

typedef struct QEngineNoteEvent {
    double time_sec;
    float pitch_hz;
    int32_t midi_note;
    float confidence;
    float level;
} QEngineNoteEvent;

typedef struct QEngineStringComponent {
    int32_t band;
    float pitch_hz;
    float midi_float;
    int32_t midi_note;
    float confidence;
    float cents;
    uint8_t active;
} QEngineStringComponent;

typedef struct QEngineChordFrame {
    double time_sec;
    float level;
    QEngineStringComponent strings[6];
    int32_t dominant_band;
    int32_t dominant_midi;
    float dominant_pitch_hz;
    float dominant_confidence;
    int32_t active_count;
} QEngineChordFrame;

QENGINE_FFI_API QEngineHandle* qengine_create(
    float sample_rate,
    float threshold_db,
    float min_periodicity,
    const QEngineBandRange* ranges,
    size_t range_count);

QENGINE_FFI_API void qengine_destroy(QEngineHandle* handle);
QENGINE_FFI_API void qengine_reset(QEngineHandle* handle);
QENGINE_FFI_API void qengine_set_min_periodicity(QEngineHandle* handle, float v);
QENGINE_FFI_API void qengine_push_audio(QEngineHandle* handle, const float* samples, size_t count);

// Async backend processes audio on its worker thread. This acts as a wakeup hint.
QENGINE_FFI_API void qengine_process(QEngineHandle* handle);

QENGINE_FFI_API int qengine_get_latest_detection(QEngineHandle* handle, QEngineDetectionFrame* out_frame);
QENGINE_FFI_API size_t qengine_pop_note_events(QEngineHandle* handle, QEngineNoteEvent* out_events, size_t max_events);
QENGINE_FFI_API size_t qengine_get_recent_frames(QEngineHandle* handle, QEngineDetectionFrame* out_frames, size_t max_frames);
QENGINE_FFI_API size_t qengine_pop_chord_frames(QEngineHandle* handle, QEngineChordFrame* out_frames, size_t max_frames);
QENGINE_FFI_API int qengine_get_latest_chord_frame(QEngineHandle* handle, QEngineChordFrame* out_frame);

#ifdef __cplusplus
}
#endif
