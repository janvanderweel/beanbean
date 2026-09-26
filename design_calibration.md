# design_calibration.md — Two-plate (Low/High) reference calibration

## 1. What this is

Beanbeam reads a 18-channel spectrometer. This doc adds an **optional two-plate
calibration** so the meter maps a sample to colour/temperature instead of just
printing raw per-wavelength bars. It adapts **Toneone's two-reference-plate
scheme** (two coloured calibration disks) onto the AS7265X.

### Toneone vs AS7265X — don't conflate them

Toneone's scheme is built around the **TCS3200** colour sensor:
it measures only **red** and **blue** channels, and colour = `R/B` ratio. The
two disks (brown = "low", red = "high") are named in the library constants and
define the two points of a calibration line.

The **AS7265X** is not a 2-channel colour sensor — it is an 18-channel
spectrophotometer (410 → 940 nm). So the same *idea* ("two known reflectance
disks → define a scale") applies, but the math must be expressed against the 18
channels instead of just R/B. See §4.

## 2. Reference plates (what you have)

Two physical disks, used as calibration references:

| Plate | Common name | Role in fit | Target R/B ratio |
| :--- | :--- | :--- | :--- |
| **Brown** | "Low" reference | anchors the low point | `~1.5` |
| **Red**   | "High" reference  | anchors the high point | `~3.7` |

Target ratios are fixed constants (tuned on Toneone). The disks are assumed to
be reasonably stable; `brown ≈ low` and `red ≈ high` are the *target* R/B values
the fit tries to reproduce, and from those two points we derive the calibration
coefficients.

> `brown` is the darker/more-matte disk; `red` is the brighter/stronger-red disk.
> The red disk gives the higher red-to-blue ratio (≈ 3.7), the brown disk the
> lower one (≈ 1.5). The two colours must produce clearly separated R/B values so
> the calibration line isn't near-vertical.

## 3. The flow (state machine)

Calibration requires the operator to place each plate in front of the sensor:

```
            Button C (after step)          Button C
  ┌───────────────────┐   place disk +   ┌───────────────────┐   lift & replace
  │ STATE_CALIBRATE_READY │ ───────────► │   STATE_PLATE_MEASURE │ ────────────────►
  │   (waiting for disk)  │   (place)     │    (measure 1 plate) │  (swap disks)
  └───────────────────┘                  └───────────────────┘
        ▲                                      │
        │   done                              │
        └─────────────────────────────────────┘
```

Steps:

1. Enter Calibrate. Prompt **"Place LOW (brown)"**.
2. Measure. Steps the to **Low** (default) or **High/Red** if the next button is
   pressed, or auto-selects based on prompt. Measure `N` samples, average.
3. Prompt **"Place HIGH (red)"** → same measurement.
4. Lift & swap the disks. Measure the second plate.
5. The code confirms each plate was measured within its **acceptance window**
   (§5). If a plate is off-target, it warns and can reject/re-run.
6. Compute calibration line (§5). Save into a new `profile[]`.

> **Scope note:** this is a replacement *in addition to* the per-channel
> white-reference correction. The per-channel `calibrationFactors[]` correction
> is still needed to turn cumulative/relative readings into physical-ish units.
> So calibration = white reference (existing) + two-plate colour line (this doc).

## 4. The math (ADAPTED from R/B → 18 channels)

Toneone fits **red/blue ratio**:

```
rb_low  = redavg / blueavg     # brown disk
rb_high = redavg / blueavg     # red disk
cal[0]  = (HIGH_TARGET - LOW_TARGET) / (rb_high - rb_low)
cal[1]  = LOW_TARGET  - cal[0] * rb_low
v       = cal[0] * (r/b) + cal[1]
T       = scale[0]*v^3 + scale[1]*v^2 + scale[2]*v + scale[3]
```

On the AS7265X "red" and "blue" are no longer raw 2-channel values — **every
channel is a wavelength**. The analog of R/B is a **narrowband ratio** computed
from selected channels. The design picks two calibration channels that best
separate the two disks (mirrors the red/blue separation Toneone relies on).

Let the two chosen channels be `ChRed` and `ChBlue` (see §6). Define per plate:

```
rx = raw(ChRed),  bx = raw(ChBlue)          # averages of the plate's N samples
ratio = rx / bx
```

Two-point line is identical in spirit; `x` is now the channel-ratio `rx/bx`:

```
cal0 = (HIGH_TARGET - LOW_TARGET) / (ratio_high - ratio_low)
cal1 =  LOW_TARGET  - cal0 * ratio_low
y     = cal0 * ratio  + cal1                       # the calibrated "v" value
```

`y` can then be rendered as a colour bar / temperature scale just like Toneone.
We **drop the cubic T polynomial** by default: with an 18-channel spectrum we
can compute any colour from ALL channels (e.g. XYZ / sRGB), which is more robust
than a 2-point fit. Keeping Toneone's cubic is an option for backwards parity —
see §8.

## 5. Acceptance windows (Toneone constants, carried over)

Each plate must fall inside its target band to be accepted:

| Plate | red channel target band | blue channel target band |
| :--- | :--- | :--- |
| **LOW (brown)** | `abs(r - 2600) < 2100` | `abs(b - 1600) < 1500` |
| **HIGH (red)**  | `abs(r - 15000) < 7000` | `abs(b - 3600) < 2100` |

- `r`, `b` are the averaged (and per-channel calibrated) values of `ChRed`/`ChBlue`.
- These thresholds were tuned for the **TCS3200's** raw range. The AS7265X ADC
  scale differs (and gains are 1x/3.7x/16x/64x). **These numbers MUST be
  re-derived on the AS7265X before shipping** — see §9. Until verified, treat
  them as the *template* (same names/structure: `LOW_*, HIGH_*, *_RANGE_*`).

Two sanity checks when the fit would be degenerate:
- If `ratio_high ≈ ratio_low` → calibration line near-vertical → warn, reject.
- If either window fails by a lot → refuse to compute; let operator re-place disk.

## 6. Which AS7265X channels to use

Toneone uses the TCS3200's only red and blue photodiodes. The AS7265X has an
18-channel array; we should **not** hardcode arbitrary channels but instead:

- Default to a sensible pair that maximally separates red from blue (e.g. the
  `H` channel ≈ 645 nm for "red" and `F` ≈ 535 nm for "blue", or the factory
  "red"/"blue" bands the AS7265X firmware exposes), then
- Optionally let calibration store the pair, so it's tunable/validated later.

Channel choice is the biggest hardware-specific decision → verify the channel
R/B contrast on the specific disks before committing. See §9.

## 7. Data structures

Add a separate colour-profile table (do not mix into `getters[]` polling):

```c
// Per chosen channel: raw + calibrated, averaged over the plate samples.
typedef struct {
  float calibrated;        // averaged calibrated reading on the chosen channel
  float raw;               // (if using getRawX — see REVIEW_ISSUES item 1)
  bool  accepted;          // inside its acceptance window?
} plateMeasurement_t;

// Two-point calibration line (red/blue channel-ratio domain).
typedef struct {
  // For "red" disk
  float ratio;             // r/b
  bool  accepted;
  // For "brown" disk
  float ratio_low;
  bool  accepted_low;
  // Fits line: y = slope * ratio + intercept
  float slope;             // calibrationFactor equivalent for the ratio
  float intercept;
  int  ChRed;              // chosen red channel index
  int  ChBlue;             // chosen blue channel index
} colour_calibration_t;
```

`colour_calibration_t` lives alongside `float calibrationFactors[18]` at file
scope (currently the profile is a set of locals that never persists — see
REVIEW_ISSUES §4 item 4; this adds the same persistence gap, call out at §8).

## 8. Interaction with the existing calibration

- **Keep** `calibrationFactors[18]` (per-channel white-reference correction).
- **Add** `colour_calibration_t` (R/B line) for the optional two-plate mode.
- Measurement reads the same channels via the existing `getters[]` polling with
  `.getCalibrated*`, but for the colour path you additionally grab the two chosen
  channels per sample to build `ratio`.
- Do **not** remove the per-channel code; the two systems are independent axes.

## 9. What MUST be verified on hardware (do the work before coding)

The design is shaped by Toneone values that are **TCS3200-specific**. Before any
code change, confirm on the AS7265X / actual disks:

1. **Channel R/B contrast** — does `ChRed`≫`ChBlue` on both disks? If not, pick
   different channels (or note it as a limitation).
2. **Acceptance constants** — re-derive `LOW_*/HIGH_*/*_RANGE_*` for the AS7265X
   raw ADC scale (and per gain).
3. **Cumulative-reading bug** — REVIEW_ISSUES item 1 says `getCalibratedX()`
   may accumulate; averaging cumulative values is invalid. If true, read
   `getRawX()` (`getDeltaX` as a fallback) instead, and average fresh samples —
   applies to *both* the white-reference and the two-plate math.
4. **Gain** — choose/optimize a gain (setup uses gain 2 = 16x) that keeps both
   disks within range without saturating.
5. **Persistence** — store the fitted profile (`slope/intercept` + channel pair)
   in flash/EEPROM so recalibration isn't lost on power cycle (extends item 4).

## 10. Out of scope (for now)

- XYZ/sRGB derivation, colour rendering on the TFT, temperature output via T —
  optional follow-ups. Default is to reproduce Toneone's ratio→colour bar.
- Multi-level fit (>2 plates) — this is explicitly a **two-point** calibration.
