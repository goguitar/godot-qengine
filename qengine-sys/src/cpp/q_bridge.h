/*
 * q_bridge.h  –  C interface to the cycfi/Q pitch detection library.
 *
 * All functions use opaque `void*` handles so they can be called from Rust
 * through `extern "C"` declarations without any C++ headers visible on the
 * Rust side.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Per-string pitch detector
 * ---------------------------------------------------------------------- */

/**
 * Create a new Q pitch detector for a single frequency band.
 *
 * @param min_hz        Lowest detectable frequency in Hz  (e.g. 60 for B1)
 * @param max_hz        Highest detectable frequency in Hz (e.g. 1500)
 * @param sample_rate   Audio sample rate in samples/second (e.g. 44100)
 * @param threshold_db  Noise-floor threshold in dB  (negative, e.g. -45)
 * @return Opaque handle; must be freed with q_pitch_detector_free().
 */
void* q_pitch_detector_new(float min_hz,
                           float max_hz,
                           float sample_rate,
                           float threshold_db);

/** Free a detector created by q_pitch_detector_new(). */
void  q_pitch_detector_free(void* handle);

/**
 * Feed one audio sample into the detector.
 *
 * @return true when the detector has updated its frequency estimate.
 */
bool  q_pitch_detector_process(void* handle, float sample);

/** Return the latest detected frequency in Hz (0 if no pitch found yet). */
float q_pitch_detector_get_frequency(void* handle);

/** Return the latest periodicity confidence [0, 1]. */
float q_pitch_detector_periodicity(void* handle);

/** Reset internal state (call between distinct note events if needed). */
void  q_pitch_detector_reset(void* handle);

/**
 * Convenience: process a whole buffer in one call.
 *
 * @param samples   Pointer to mono f32 samples, normalised to [-1, 1].
 * @param count     Number of samples in the buffer.
 * @return Last detected frequency in Hz (0 if no pitch was found).
 */
float q_pitch_detector_process_buffer(void*        handle,
                                      const float* samples,
                                      int          count);

/* -------------------------------------------------------------------------
 * Multi-band guitar detector  (6 strings, one detector per band)
 * ---------------------------------------------------------------------- */

/** Tuning IDs understood by q_guitar_detector_new(). */
#define Q_TUNING_STANDARD  0   /* E2 A2 D3 G3 B3 E4          */
#define Q_TUNING_DROP_D    1   /* D2 A2 D3 G3 B3 E4          */
#define Q_TUNING_OPEN_D    2   /* D2 A2 D3 F#3 A3 D4         */
#define Q_TUNING_DROP_C    3   /* C2 G2 C3 F3 A3 D4          */
#define Q_TUNING_DADGAD    4   /* D2 A2 D3 G3 A3 D4          */

/**
 * Create a 6-band guitar detector for the given tuning.
 *
 * @param tuning_id     One of the Q_TUNING_* constants above.
 * @param sample_rate   Audio sample rate.
 * @param threshold_db  Noise-floor threshold (e.g. -45).
 * @return Opaque handle; free with q_guitar_detector_free().
 */
void* q_guitar_detector_new(int   tuning_id,
                            float sample_rate,
                            float threshold_db);

/** Free a detector created by q_guitar_detector_new(). */
void  q_guitar_detector_free(void* handle);

/**
 * Process one mono sample through all 6 band detectors simultaneously.
 *
 * @param out_frequencies  Caller-allocated array of 6 floats.
 *                         Each element receives the latest frequency for
 *                         that string band (0 = not detected).
 * @param out_periodicities Caller-allocated array of 6 floats (confidence).
 */
void  q_guitar_detector_process(void*  handle,
                                float  sample,
                                float* out_frequencies,
                                float* out_periodicities);

/**
 * Convenience: process a whole buffer for all 6 bands.
 *
 * @param out_frequencies   Caller-allocated array of 6 floats.
 * @param out_periodicities Caller-allocated array of 6 floats.
 */
void  q_guitar_detector_process_buffer(void*        handle,
                                       const float* samples,
                                       int          count,
                                       float*       out_frequencies,
                                       float*       out_periodicities);

/** Reset all 6 band detectors. */
void  q_guitar_detector_reset(void* handle);

#ifdef __cplusplus
} /* extern "C" */
#endif
