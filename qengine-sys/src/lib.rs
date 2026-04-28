//! Raw FFI bindings to the cycfi/Q C++ pitch-detection library.
//!
//! All symbols live in `libq_bridge` which is compiled by `build.rs` from
//! [`src/cpp/q_bridge.cpp`].  The safe Rust wrappers are in [`crate::bridge`].

#![allow(non_camel_case_types)]
#![allow(clippy::missing_safety_doc)]

use std::ffi::c_void;

pub mod bridge;

/* -------------------------------------------------------------------------
 * Tuning constants (mirror C `#define`s)
 * ---------------------------------------------------------------------- */

pub const Q_TUNING_STANDARD: i32 = 0;
pub const Q_TUNING_DROP_D:   i32 = 1;
pub const Q_TUNING_OPEN_D:   i32 = 2;
pub const Q_TUNING_DROP_C:   i32 = 3;
pub const Q_TUNING_DADGAD:   i32 = 4;

/* -------------------------------------------------------------------------
 * Raw extern "C" declarations
 * ---------------------------------------------------------------------- */

extern "C" {
    // --- Single-band pitch detector ---

    pub fn q_pitch_detector_new(
        min_hz:       f32,
        max_hz:       f32,
        sample_rate:  f32,
        threshold_db: f32,
    ) -> *mut c_void;

    pub fn q_pitch_detector_free(handle: *mut c_void);

    pub fn q_pitch_detector_process(handle: *mut c_void, sample: f32) -> bool;

    pub fn q_pitch_detector_get_frequency(handle: *mut c_void) -> f32;

    pub fn q_pitch_detector_periodicity(handle: *mut c_void) -> f32;

    pub fn q_pitch_detector_reset(handle: *mut c_void);

    pub fn q_pitch_detector_process_buffer(
        handle:  *mut c_void,
        samples: *const f32,
        count:   i32,
    ) -> f32;

    // --- Multi-band guitar detector (6 strings) ---

    pub fn q_guitar_detector_new(
        tuning_id:    i32,
        sample_rate:  f32,
        threshold_db: f32,
    ) -> *mut c_void;

    pub fn q_guitar_detector_free(handle: *mut c_void);

    pub fn q_guitar_detector_process(
        handle:           *mut c_void,
        sample:           f32,
        out_frequencies:  *mut f32,
        out_periodicities: *mut f32,
    );

    pub fn q_guitar_detector_process_buffer(
        handle:            *mut c_void,
        samples:           *const f32,
        count:             i32,
        out_frequencies:   *mut f32,
        out_periodicities: *mut f32,
    );

    pub fn q_guitar_detector_reset(handle: *mut c_void);
}
