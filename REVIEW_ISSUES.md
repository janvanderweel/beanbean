# Beanbeam Spectrometer — Code Review

Status **ALPHA (V0.1)** · branch `wavelength-to-strings` (working alias
`next_next`) · file `src/beanbeam/beanbeam.ino` (Arduino sketch, ~475 LOC) plus
`src/beanbeam/button.{cpp,h}`.

A coffee spectrometer on a **Seeed WIO Terminal** driving a **SparkFun
AS7265X** Spectral Triad (18-channel I2C sensor) and an **LGFX/Lovyan TFT**
display.

> **Cannot be built or unit-tested on this host.** It is not standard C++: it is
> an Arduino `.ino` that needs the SparkFun `AS7265X` library and the WIO
> Terminal toolchain. No compiler/SDK is available for verification here. Every
> "verified" claim below is derived from source inspection, not compilation.
> Anything that touches sensor/serial/clock behaviour must be re-confirmed on
> hardware.

## 1. Already-applied refactors (committed diff `+99 / -98`)

| Change | Issue | Fix | Status |
| :--- | :--- | :--- | :--- |
| `setup()` `pinMode` | WIO_KEY A/B/C were set `INPUT_PULLUP` in setup, then re-initialized in each `Button::poll()`. | Removed the `pinMode(...)` calls in `setup()`. | ✅ Real. `Button` owns pin config. |
| `performMeasurement` | All 18 channels spelled out as separate `Serial.print`/`showValue` calls. | Rewritten as a function-pointer loop. | ✅ Real in `.ino`. |
| `displayMenu` highlight | Selection highlight drawn with text color only (low contrast). | Added a green background fill + black text for the active row. | ✅ Real cosmetic. |
| Calibration | `performCalibration()` was a stub (delay only). | `runCalibrationSequence()` averages 5 samples → per-channel scale factors. | ✅ Real (see ⚠️ item 1). |
| Return-to-menu | Mixed / inconsistent ways to leave sub-states. | Unified: Button C collapses any non-MENU sub-state to MENU. | ✅ Real. |
| `showPins()` | Unused diagnostic function. | Deleted. | ✅ Real. |
| `STATE_CALIBRATE_RUNNING` | Enum value added but never consumed. | Removed the enum value, the stray assignment, and the unused `pins[]` array. | ✅ Applied this round (see §2). |
| `scanI2C()` | Boots every power-up, prints 128 dots to Serial, constructs an `Adafruit_I2CDevice(0..127)` before `sensor.begin()`. | Removed the function, its `setup()` call, and the now-unused `#include <Adafruit_I2CDevice.h>`. | ✅ Applied this round (see §2). |

A `STATE_CALIBRATE_READY` sub-state exists, but note the calibration flow is
**button-triggered from the menu** (`STATE_CALIBRATE_READY` → Button C →
`runCalibrationSequence`), not "Calibrate" → sub-menu as the item 6 summary
implies. The on-screen prompts (`"Place White Ref"`, `"Press C to Calib"`) are
drawn by `displayCalibrateScreen()`, which is **only shown when leaving the menu
into `STATE_CALIBRATE_READY`** — so the prompts *can* appear, but only if the
user enters via the menu in the right order.

## 2. Findings / backlog

**Severity tags: HIGH / MEDIUM / LOW. All "verified" below.**

### 1. Calibration averaging may use cumulative totals (HIGH)
`runCalibrationSequence` and the legacy `performMeasurement` both average
**cumulative** calibrated readings per channel, then divide by the previous
total to derive a scaling factor. On the AS7265X the `getCalibratedX()` getters
*accumulate* the sensor's raw ADC onto a running value; some channels compare
against the prior total and can throw `NVM_CORRUPTION` when the previous read is
`0`. Averaging cumulative values is therefore **not a valid white-reference
calibration** and will produce wrong scale factors.

Possible fixes: read `getRawX()` (and average fresh samples), or if `getDeltaX`
exists, average delta-from-previous. **Needs library + hardware confirmation**
before editing the math — do **not** refactor in place blindly.

### 2. `withLed` is a dead, write-only-default branch (MEDIUM)
`withLed` is declared `bool withLed = true;` and is **never written anywhere**;
both `performMeasurement()` and `runCalibrationSequence()` read it only to pick
`takeMeasurementsWithBulb()` vs `takeMeasurements()`, so the "no-bulb" branch is
only reachable if something later flips the flag. The flag should either toggle
from a menu setting or be deleted with a note. **(Unapplied — decision pending.)**

### 3. Calibration prompts are prompt-driven, not modal (MEDIUM)
"Calibrate" sets `STATE_CALIBRATE_READY` and draws "Place White Ref / Press C to
Calib". Calibration then runs on Button C. There is no on-screen state machine
that validates white ref placement before running — the prompts are advisory, and
the flow collapses immediately on the next Button C. Consider surfacing them as
a clear step sequence (place → measure → run) with state-guarded prompts.

### 4. `calibrationFactors` never persisted (MEDIUM)
Scales are `static` locals that reset to `1.0` on boot and also live in a local
array no caller reads. No white-reference profile is ever saved (flash/EEPROM/EPS),
so recalibration or a power cycle silently loses the correction.

### 5. `STATE_CALIBRATE_RUNNING` was dead (LOW) — **now removed**
The enum value was assigned once and read once, but the two references were in
different branches that never met; it was an inconsistent state token. Removed.

### 6. I2C scan spam (LOW) — **now removed**
`setup()` scanned I2C 0–127 (printing `<<< scanI2C`…`scanI2C >>>` to Serial) once
per boot before the sensor was even initialized. Done.

### 7. Unused `pins[8]` array (LOW) — **now removed**
Defined but never indexed after `showPins()` was deleted. Removed.

### 8. Button debounce timing type (LOW)
`Button` stores the raw pin read in `bool` members but keeps its sole use of
`time` as `uint32_t` with only a self-comparison. Unit-correct for a debouncer,
but the name implies ms and the field is unused — cosmetic cleanup.

### 9. Real-time blockiness (LOW–MED)
`takeMeasurements()` blocks with a hard wait while all 18 channels are read,
`delay(2000)` after calibrate, `delay(200)` button debounce. Correct for a cheap
MCU one-shot, worth noting before any continuous/real-time firmware goal.

## 3. Notable good practice
- Clean, readable diff for this round (`+99 / -98`) — large rewrite kept legible.
- The `getters[]` function-pointer array makes the 18-channel loop maintainable.
- `.DS_Store` files present but correctly git-ignored.

## 4. Open questions
- Should I proceed on **(2)**: keep the `-withBulb` hardware path only, or add a
  menu toggle that makes `-withLed` reachable? (Recommended: **remove** the branch.)
- **Item 1** must be verified against the AS7265X library on hardware before any
  change to the averaging/scaling math.
