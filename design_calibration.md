# design_calibration.md — Two-plate (Low/High) reference calibration — **V1 "Tonino-lite"**

## 0. Goal (kept deliberately simple)

Give Beanbeam a Tonino-style roast scale: **two reference plates define a
straight line, then each sample maps onto that line as one number.** Nothing
more for V1. The deeper 18-channel colour science (XYZ/sRGB, per-gain
acceptance bands, channel auto-selection) is explicitly deferred — see
§8 *Deferred / out of scope* and Appendix A for the full reasoning.

This replaces the earlier, larger draft. The full "spectrophotometer-grade"
version is preserved as **Appendix A** so nothing is lost, but it is NOT the
V1 plan.

---

## 1. What this is

Tonino calibrates a coffee-colour meter with **two coloured disks** (a darker
"low" reference and a brighter "high" reference). It measures a single scalar
per disk (Tonino: red/blue ratio), fits a 2-point line, and every later reading
is placed on that line as one roast number.

Beanbeam does the same, adapted to the AS7265X:

- The scalar is a **channel ratio** `red_channel / blue_channel` (two chosen
  channels out of the 18), not a raw 2-diode R/B.
- Two plates → two ratios → a Stage-1 line `v = cal0*ratio + cal1`, then a
  fixed Stage-2 polynomial maps `v` → the Tonino roast number (§4).
- After calibration, a sample reading takes **3 samples, averages them**,
  computes the ratio, and reports `y` (the roast number) plus the roast label.

That is the whole feature. Keep it that small.

---

## 2. Reference plates

Two physical disks used as calibration references:

| Plate | Common name | Role | Notes |
| :--- | :--- | :--- | :--- |
| Brown | "Low" reference  | anchors the low point of the line  | darker / more matte |
| Red   | "High" reference | anchors the high point of the line | brighter / stronger red |

The two disks must produce **clearly separated ratios** so the calibration line
is not near-vertical. That separation is the only property we actually depend
on in V1. The specific target ratio values are hardware-dependent and are
**captured during calibration**, not hardcoded (see §5).

---

## 3. Flow (state machine) — three steps, all Button C

```
  MENU ──"Calibrate"──► STATE_CAL_LOW ──C──► STATE_CAL_HIGH ──C──► fit line ──► MENU
                        (place BROWN,        (place RED,           (compute
                         press C to          press C to            cal0/cal1,
                         sample x3)          sample x3)            store, show OK)
```

Steps:

1. **Enter Calibrate.** Prompt: `Place LOW (brown), press C`.
2. On **C**: take **3 samples**, average, store `ratio_low`. Prompt:
   `Place HIGH (red), press C`.
3. On **C**: take **3 samples**, average, store `ratio_high`. Then:
   - Guard: if `ratio_high ≈ ratio_low` → line degenerate → show `Cal FAILED:
     plates too similar`, discard, return to menu.
   - Otherwise compute `cal0`/`cal1`, mark the profile `valid`, show
     `Cal OK`, return to menu.

Button C also collapses to MENU at any point (consistent with the rest of the
UI). No extra sub-menu, no per-plate re-run wizard in V1 — if a plate is wrong,
the operator just re-runs Calibrate.

"**3 measurements**" appears twice on purpose and means the same thing
everywhere: **each act of measuring = 3 samples averaged into one value.** Used
for each calibration plate AND for each post-calibration sample reading. One
constant governs it:

```c
static const int CAL_SAMPLES = 3;   // samples averaged per measurement
```

---

## 4. The math (Tonino's actual two-stage pipeline)

Verified against the Tonino firmware
(`myTonino/Tonino-Firmware`, `Tonino/tonino_tcs3200.{h,cpp}`). Tonino uses **two
stages**, not one line. We reproduce both for parity.

### Stage 1 — calibration line (fitted from the two plates)

Per plate, average `CAL_SAMPLES` samples and form the channel ratio:

```
ratio = value[chRed] / value[chBlue]
```

Tonino pins the two plates to **fixed target ratios** (not 0/100) and fits a line
in ratio→`v` space (Tonino `tonino_tcs3200.h`):

```c
static const float LOW_TARGET  = 1.5f;   // brown disk target r/b  (Tonino LOW_TARGET)
static const float HIGH_TARGET = 3.7f;   // red   disk target r/b  (Tonino HIGH_TARGET)
```

```
cal0 = (HIGH_TARGET - LOW_TARGET) / (ratio_high - ratio_low)
cal1 =  LOW_TARGET - cal0 * ratio_low
v    =  cal0 * ratio + cal1              // calibrated ratio for a sample
```

(Tonino's factory defaults before any user calibration are
`cal0 = 1.011949`, `cal1 = -0.094599` — use these as the pre-calibration
fallback so the meter still reads something sane un-calibrated.)

### Stage 2 — scale polynomial (fixed, maps v → Tonino number)

Tonino then maps `v` to the displayed roast number with a fixed cubic
(`tonino.h` DEFAULT_SCALE_*):

```c
static const float SCALE_0 = 0.0f;          // Tonino DEFAULT_SCALE_0
static const float SCALE_1 = 0.0f;          // Tonino DEFAULT_SCALE_1
static const float SCALE_2 = 102.2727273f;  // Tonino DEFAULT_SCALE_2
static const float SCALE_3 = -128.4090909f; // Tonino DEFAULT_SCALE_3
```

```
T = SCALE_0*v^3 + SCALE_1*v^2 + SCALE_2*v + SCALE_3
```

Because `SCALE_0 == SCALE_1 == 0`, this is **effectively linear**:
`T = 102.2727273 * v - 128.4090909`. The Tonino scale runs roughly **0 (very
dark) to ~140 (very light)**; typical roasts land ~60–130. Keeping the cubic
form (with the two leading terms zero) means we can drop in Tonino's real cubic
later without changing code shape.

### Summary

```
ratio → [Stage 1: v = cal0*ratio + cal1] → [Stage 2: T = poly(v)] → roast number
```

Stage 1 is what calibration fits (from your two disks). Stage 2 is a fixed
Tonino-derived constant. The only V1 guard is the degenerate-line check from §3
(`ratio_high ≈ ratio_low` → reject). No acceptance windows.

> **Note on parity vs. our chart:** `T` is the Tonino roast *number*. The 7
> roast *labels* (ultra light … ultra dark) the UI shows are just bucketed `T`
> ranges — a separate UI concern, decided in the score session, not here.

---

## 5. Channel choice

Tonino has only red + blue photodiodes and defines colour as `red/blue`. The
AS7265X has 18 channels, so we pick **one red-ish and one blue-ish channel** and
store the pair in the profile so it is tunable later without changing the math:

```c
static const int DEFAULT_CH_RED  = 9;   // ~645 nm (red)   — index into values[]
static const int DEFAULT_CH_BLUE = 2;   // ~460 nm (blue)  — index into values[]
```

(Indices are into the existing ascending-wavelength `values[]`/`freq[]` arrays:
645 nm is index 9, 460 nm is index 2.) The pair is stored in the profile
(§6). Auto-selecting the best-separating pair is **deferred** (Appendix A §6).

> **Verify on hardware (§7):** confirm `value[chRed] > value[chBlue]` on BOTH
> disks and that the two disks give clearly different ratios. If not, change the
> two constants above — no other code changes needed.

---

## 6. Data structure (minimal)

Only the **Stage 1** fitted line (`cal0`/`cal1`) + the channel pair persist. The
Stage 2 scale polynomial is a fixed Tonino-derived global constant (§4), not
per-calibration, so it is NOT stored here. Per-plate ratios are transient (used
during the fit, then discarded).

```c
struct ColourCalibration {
  float cal0;        // Stage-1 slope     (from §4; factory default 1.011949)
  float cal1;        // Stage-1 intercept (from §4; factory default -0.094599)
  int   chRed;       // chosen red channel index  (defaults to DEFAULT_CH_RED)
  int   chBlue;      // chosen blue channel index (defaults to DEFAULT_CH_BLUE)
  bool  valid;       // true once a good 2-point fit has been stored
};

ColourCalibration colourCal;   // file scope, next to calibrationFactors[18]
```

That's the whole profile. Compare to the earlier 8-field struct that stored
both plates' raw readings — not needed for V1. The roast number is then
`T = poly(cal0*ratio + cal1)` using the fixed `SCALE_*` constants from §4.

---

## 6a. Persistence (EEPROM → flash on the WIO Terminal)

Yes — calibration should survive a power cycle, so it must be stored in
non-volatile memory. Two important hardware facts:

- **The WIO Terminal's SAMD51 has no true EEPROM.** Persistence is done by
  **emulating EEPROM in flash**. The standard Arduino library for this on
  SAMD21/SAMD51 is **`FlashStorage`** (`cmaglie/FlashStorage`), which the
  official Arduino docs recommend for exactly this case. A hardened variant
  (`Xorlent/SAMD_SafeFlashStorage`) adds protection against corruption if power
  is lost mid-write.
- **We cannot copy Tonino's persistence code.** Tonino runs on an AVR with real
  `EEPROM.h` (and even does redundant write cycles — `EEPROM_REDUNDANT_CYCLES`).
  On the SAMD51 that API isn't the right one; use `FlashStorage`.

### What to persist

Store one small blob with a version tag and a magic marker so a blank/older
flash is detected and safely ignored (falls back to defaults):

```c
struct PersistedCalibration {
  uint32_t magic;            // e.g. 0xB3A11CAL — "is this ours?"
  uint16_t version;          // bump when the layout changes
  float    calibrationFactors[18]; // existing white-reference correction (item 4)
  ColourCalibration colourCal;     // the two-plate colour line (§6)
  uint32_t crc;              // optional integrity check over the bytes above
};
```

Persisting BOTH the white-reference factors AND `colourCal` in one blob closes
REVIEW_ISSUES item 4 (white-reference never saved) at the same time.

### Sketch of the flow

```c
#include <FlashStorage.h>
FlashStorage(calStore, PersistedCalibration);   // reserves a flash slot

// boot: load if valid, else defaults
PersistedCalibration p = calStore.read();
if (p.magic == CAL_MAGIC && p.version == CAL_VERSION /* && crc ok */) {
  memcpy(calibrationFactors, p.calibrationFactors, sizeof(calibrationFactors));
  colourCal = p.colourCal;
} else {
  // initMap() already sets calibrationFactors[]=1.0; set colourCal defaults
  colourCal = { DEFAULT_CAL_0, DEFAULT_CAL_1, DEFAULT_CH_RED, DEFAULT_CH_BLUE, false };
}

// after a successful calibration: fill p, set magic/version/crc, then
calStore.write(p);   // commits to flash
```

### Caveats (must respect on hardware)

- **Flash wear.** SAMD flash endurance is limited (~10k–100k writes). Only write
  on an explicit successful calibration — **never** per measurement or per loop.
- **Write cost / blocking.** A flash write erases a page and blocks briefly; it
  is fine as a one-off at end-of-calibration, not in the hot path.
- **A re-flash of the sketch may erase the emulated-EEPROM region** depending on
  the upload; treat stored calibration as "survives power cycles," and expect it
  may need re-doing after a firmware update. Note this in the UI/README.
- **Verify the write actually persisted** by reading it back once after
  `write()` during bring-up.

Until `FlashStorage` is wired in, V1 may keep `colourCal` in RAM and require
recalibration on boot — but the intent is flash persistence per this section.

---

## 7. What MUST be verified on hardware (before writing the fit math)

These are the only hardware unknowns V1 depends on. Resolve them first.

1. **Cumulative-getter question (blocking — REVIEW_ISSUES item 1).** If
   `getCalibratedX()` accumulates, averaging it is invalid and BOTH the existing
   white-reference math and this ratio are built on bad data. Confirm with a
   diagnostic (print `getRawX` vs `getCalibratedX` for one channel over several
   reads). If calibrated accumulates, read `getRawX()` for the ratio instead.
   **Do not write the fit until this is settled.**
2. **Channel contrast (§5).** `chRed > chBlue` on both disks, and the two disks
   give clearly separated ratios. Adjust the two channel constants if not.
3. **Gain.** Pick a gain (setup currently uses gain 2 = 16x) that keeps both
   disks in range without saturating either channel.
4. **Persistence (see §6a).** Store `colourCal` **and** `calibrationFactors[18]`
   in flash via `FlashStorage` so they survive a power cycle (closes
   REVIEW_ISSUES item 4). Verify a written blob reads back correctly, and
   confirm whether a sketch re-flash wipes the emulated-EEPROM region on this
   board. If persistence isn't wired yet, V1 keeps it in RAM and recalibrates on
   boot — call that out in the UI.

---

## 8. Deferred / out of scope for V1

- Acceptance windows / the TCS3200 constants (`2600`, `15000`, `*_RANGE_*`).
  They are meaningless on the AS7265X ADC scale and can't be trusted without
  re-derivation; the degenerate-line guard is the only check V1 keeps.
- Automatic best-channel-pair selection.
- XYZ / sRGB colour derivation and TFT colour rendering.
- A non-trivial cubic. Note: V1 **keeps** Tonino's Stage-2 scale polynomial (§4)
  but with its two leading terms zero (so it is effectively linear). Fitting a
  genuinely cubic v→number curve (or a separate temperature `T` output) is what
  is deferred here, not the scale step itself.
- Multi-plate (>2) fits.

All of the above are captured in Appendix A for when V1 is proven.

---

## Appendix A — Full spectrophotometer-grade design (original, deferred)

> This is the earlier, larger design. It is the long-term vision, NOT the V1
> plan. Kept verbatim-in-spirit so the reasoning is not lost.

**A.1 Tonino vs AS7265X.** Tonino is built around the TCS3200 colour sensor: it
measures only red and blue channels, colour = R/B ratio, two disks (brown/red)
define the calibration line. The AS7265X is an 18-channel spectrophotometer
(410 → 940 nm), so "two known reflectance disks → define a scale" still applies,
but the math is against selected channels, and richer colour can be computed
from all 18 channels.

**A.2 Acceptance windows (Tonino constants, TCS3200-specific).**

| Plate | red target band | blue target band |
| :--- | :--- | :--- |
| LOW (brown) | abs(r - 2600) < 2100  | abs(b - 1600) < 1500 |
| HIGH (red)  | abs(r - 15000) < 7000 | abs(b - 3600) < 2100 |

These MUST be re-derived on the AS7265X (different ADC scale + gains) before
they mean anything. Two degeneracy checks: reject if `ratio_high ≈ ratio_low`
(near-vertical line) or if a window fails badly (bad placement).

**A.3 Cubic temperature mapping (Tonino parity option).**
`T = scale[0]*v^3 + scale[1]*v^2 + scale[2]*v + scale[3]` — optional, for
backwards parity with Tonino's temperature output. V1 drops it.

**A.4 Channel auto-selection.** Rather than hardcoding red/blue channels, scan
for the channel pair that maximally separates the two disks, or use the factory
"red"/"blue" bands the firmware exposes. Biggest hardware-specific decision;
validate contrast on the actual disks first.

**A.5 Richer colour.** With 18 channels, compute XYZ → sRGB for a true colour
swatch on the TFT, which is more robust than a 2-point ratio fit. Optional
follow-up.

**A.6 Fuller profile struct** (the original 8-field version storing both plates'
raw + calibrated readings and per-plate `accepted` flags) — only needed if the
acceptance windows and re-run wizard are implemented.

---

## 9. Interaction with existing calibration (unchanged from original intent)

- Keep `calibrationFactors[18]` (per-channel white-reference correction) — it is
  a **separate axis** from the two-plate colour line.
- The two systems are independent: white-reference normalises per-channel
  magnitude; the two-plate line maps a channel ratio to a roast number.
- The colour path reads the two chosen channels straight out of the existing
  `values[]` (populated by the normal 18-channel sampling loop) — no parallel
  sampling path.
- Do not remove the per-channel code.
