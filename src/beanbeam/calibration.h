/*
 * Calibration model for Beanbeam — raw-value pipeline
 * 
 * Three-stage calibration approach:
 * 
 * 1. Dark offset correction (per-channel zero baseline)
 * 2. White reference normalization (per-channel sensitivity scaling)
 * 3. Two-plate color calibration (red/NIR ratio → roast number)
 * 
 * All stages are firmware-applied to raw sensor values (uint16_t).
 */

#ifndef BEANBEAM_CALIBRATION_H
#define BEANBEAM_CALIBRATION_H

#include <stdint.h>

// ===================== Stage 1: Dark offset ====================================
// Black reference: the raw count when NO light is present.
// Captured once during a dark-reference measurement (sensor in a dark box).
// Stored per-channel to remove thermal/electronic baseline drift.

static uint16_t darkOffsets[18] = {
  0, 0, 0, 0, 0, 0,  // A-F: defaults (zero offset)
  0, 0, 0, 0, 0, 0,  // G-L
  0, 0, 0, 0, 0, 0   // R, S, T, U, V, W
};

// ===================== Stage 2: White reference normalization ==================
// Reference white: a known reflectance standard (e.g. 99% barium sulfate or
// calibrated tile). Raw counts from the white reference are stored per-channel.
// To normalize: (rawValue - darkOffset) / (whiteRawValue - darkOffset).
//
// The whiteReference[18] values are captured by measuring the white standard
// with the SAME gain, illumination, and distance as your roast samples will be.
// Then calibrationFactors[18] = normalization for EACH sample:
//   normalized = (raw - darkOffsets[ch]) / (whiteReference[ch] - darkOffsets[ch])
//   calibrated = normalized * calibrationFactors[ch]

static uint16_t whiteReference[18] = {
  8000, 8000, 8000, 8000, 8000, 8000,  // A-F: placeholder counts
  8000, 8000, 8000, 8000, 8000, 8000,  // G-L
  8000, 8000, 8000, 8000, 8000, 8000   // R, S, T, U, V, W
};

// ===================== Stage 3: Two-plate color calibration ====================
// Tonino-lite: two reference disks (brown/red) define a line in NIR/red ratio space.
// The 860nm (NIR) channel is used as the development indicator (roast darkness).
// The 645nm (red) channel provides color contrast.

static const int   DEFAULT_CH_NIR   = 13;  // ~860 nm (near-infrared) — roast development
static const int   DEFAULT_CH_RED   = 9;   // ~645 nm (red) — color/contrast

// Two-plate target ratios (Tonino-derived, stage 1 of the fit)
static const float LOW_TARGET   = 1.5f;    // brown disk: red/nir ratio
static const float HIGH_TARGET  = 3.7f;    // red disk: red/nir ratio

// Factory defaults (Tonino tonino.h) — fallback before first user calibration
static const float DEFAULT_CAL_0 = 1.011949f;
static const float DEFAULT_CAL_1 = -0.094599f;

// Stage-2 fixed scale polynomial: maps normalized ratio → Tonino roast number
// Leading two terms are 0 (effectively linear per Tonino V1.x).
static const float SCALE_0 = 0.0f;
static const float SCALE_1 = 0.0f;
static const float SCALE_2 = 102.2727273f;
static const float SCALE_3 = -128.4090909f;

// Degenerate-line guard
static const float MIN_RATIO_SEPARATION = 0.05f;

// ===================== Persisted calibration structure ===========================

struct ColourCalibration {
  float cal0;       // Stage-1 slope (line fit from two plates)
  float cal1;       // Stage-1 intercept
  int   chNIR;      // chosen NIR channel for development (default 860nm)
  int   chRed;      // chosen red channel for contrast (default 645nm)
  bool  valid;      // true once a good 2-point fit has been stored
};

struct PersistedCalibration {
  uint32_t magic;
  uint16_t version;
  uint16_t darkOffsets[18];           // Stage 1: black reference
  uint16_t whiteReference[18];        // Stage 2: white normalization baseline
  float    calibrationFactors[18];    // Stage 2: per-channel scale post-normalization
  ColourCalibration colourCal;        // Stage 3: two-plate color line
};

// ===================== Calibration pipeline (firmware) ==========================
// 
// For each raw sensor reading raw[ch]:
//
//   1. Dark offset removal:
//        darkCorrected = raw[ch] - darkOffsets[ch]
//
//   2. White reference normalization:
//        whiteNormalized = darkCorrected / (whiteReference[ch] - darkOffsets[ch])
//
//   3. Per-channel calibration factor (fine tuning):
//        calibrated = whiteNormalized * calibrationFactors[ch]
//
//   4. Color ratio (for roast number):
//        ratio = calibrated[chRed] / calibrated[chNIR]
//        v = colourCal.cal0 * ratio + colourCal.cal1
//        roastNumber = SCALE_2 * v + SCALE_3
//
// The calibrationFactors[ch] defaults to 1.0 and are typically stored after
// the user has run a white-reference measurement. darkOffsets should be 
// measured in a dark environment. whiteReference is measured against a known
// standard (tile or pressed powder).

// ===================== Workflow ================================================
//
// 1. Factory/power-up:
//    - darkOffsets[18] = {0}       (no dark reference yet)
//    - whiteReference[18] = {8000} (no white reference yet)
//    - calibrationFactors[18] = {1.0}
//    - colourCal = factory defaults (cal0, cal1, chNIR, chRed, valid=false)
//
// 2. User: "Measure dark reference" (dark box, lights off)
//    - Store the raw measurements → darkOffsets[18]
//    - Persist to flash
//
// 3. User: "Measure white reference" (known tile, standard illumination)
//    - Store the raw measurements → whiteReference[18]
//    - Persist to flash
//
// 4. User: "Two-plate color calibration"
//    - Place brown disk, press C → sample and store ratio_low
//    - Place red disk, press C → sample and store ratio_high
//    - Compute cal0/cal1, mark valid, persist to flash
//
// 5. Sample measurement:
//    - Read raw values, apply all three stages (dark → white → calibration)
//    - Compute ratio, map to roast number via Tonino scale

#endif // BEANBEAM_CALIBRATION_H
