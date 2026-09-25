# Beanbeam — Coffee Spectrometer

A small embedded program that reads the **18-channel Spectrum Triad
[AS7265X](https://www.spektral.com/product/spectrum-triad-4-2-as7265x-ultraviolet-visible-near-infrared-spectrophotometer/)** and renders per-wavelength light bars on a TFT screen.

This is alpha code, might run at all!

Originally written for a **Seeed Studio WIO Terminal** (running the
[Arduino](https://www.arduino.cc/) / [IDE](https://www.arduino.cc/en/software) sketch `src/beanbeam/beanbeam.ino`), with an optional later port to the Grove Pi. It is an early **alpha (V0.1)** — the logic (menus, calibration, display) is the focus, not a production-grade instrument.

Made by **Jan van der Weel**, derived from **[Reinhardt Behm's](https://github.com/SinKang)** Spectral Triad library code. The hardware sensor and most libraries are from **SparkFun Electronics**.

---

## What it does

| Feature | Description |
| :--- | :--- |
| **18-channel spectral read** | 18 AS7265X channels (410 nm → 940 nm, incl. near-infrared) polled over I2C. |
| **Live bars & values** | For each wavelength a bar graph is drawn on the TFT, scaled per measurement. |
| **Per-channel calibration factors** | White-reference scaling factors (`calibrationFactors[18]`) can be computed and stored in RAM. |
| **Gain selection** | Four fixed sensor gains (`1x / 3.7x / 16x / 64x`) from a menu. |
| **Multi-touch navigation** | The **A / B / C** buttons and the 5-way **S5** buttons drive the menu and states. |
| **Calibration flow** | Menu → *Calibrate* → place white reference → Button C triggers the multi-sample calibration. |
| **About / info screens** | Device type, hardware/firmware versions printed on boot and shown on an About screen. |

---

## Hardware required

- **WIO Terminal (or compatible)** — the Arduino board the sketch runs on.
- **SparkFun AS7265X Spectral Triad** — connected via **I2C** (SCL/SDA), e.g. 400 kHz.
- **1.8"–2.4" SPI/parallel TFT display** — driven by Lovyan GFX/LGFX (via a breakout on the WIO).
- **Button matrix** — the WIO's push buttons (A/B/C) and the **5-way S5** keypad.

> **No build/SDK for the WIO Terminal is available on this host.** This repo holds
> source only; verifying/simulating requires the Arduino toolchain plus the
> SparkFun AS7265X library installed on the target machine.

---

## Repository layout

```
src/beanbeam/
├── beanbeam.ino     # Main sketch: setup()/loop(), menus, calibration, display
├── button.h         # Header: Button class (poll-based debounced button)
└── button.cpp       # Implementation: debouncing, edge/level detection
clean_func.txt       # Scratch reference copy of an earlier performMeasurement() (do not compile directly)
measurement_func.txt # Scratch reference copy (identical intent); not part of the build
REVIEW_ISSUES.md     # Code review: applied refactors + severity-ranked backlog + verification gate
LICENSE.md           # MIT license for this project
```

`.DS_Store` files are git-ignored.

---

## Build

1. Install the **Arduino IDE** (or PlatformIO) and add the **Seeed WIO Terminal**
   board and the **SparkFun AS7265X** library (`Library Manager → Search:
   SparkFun AS7265X`), plus any display library variants (Lovyan GFX).
2. Create a new **WIO Terminal sketch** and paste `src/beanbeam/beanbeam.ino`.
   `button.h` / `button.cpp` are compiled as regular source files in the same
   sketch.
3. Upload and open **Serial Monitor** at **115200 baud** (boot prints device
   type, firmware, and the wavelength labels, then the menu).

### Wiring notes to check first

| Pin group | What it should be |
| :--- | :--- |
| AS7265X **SCL/SDA** | I2C SCL/SDA, address 0x**39** (default), 400 kHz. Run I2C scan on boot. |
| TFT | Enabled for Lovyan `/ LGFX_AUTODETECT` on a display-capable SPI port. |
| WIO **A / B / C** | On-board push buttons (already `INPUT_PULLUP` via `Button` polling). |
| **S5** (5-way) | See [Button layout](#button-layout). |

If the board boots but reports the sensor as "not connected," check the AS7265X
pull-ups, address (commonly 0x39), and that VCC/GND are powered.

---

## How to use it

On the TFT main menu navigate with **A** (up) / **B** (down):

| Button | Action |
| :--- | :--- |
| **A** | Scroll menu / sub-menus up. |
| **B** | Scroll menu / sub-menus down. |
| **C** | Enter selected action; also returns to the main menu from any state. |
| 5-way **S5** keys | See [Button layout](#button-layout). |

- **Measure** → continuously samples the 18 wavelengths and draws live bars.
- **Calibrate** → enter the calibration state, place a white reference, then
  press **C** to run the multi-sample white-reference calibration.
- **About** → shows build/version info.
- Press **C** at any time to return to the menu.

### Button layout

The **A / B / C** labels are the WIO Terminal's on-board buttons. The
**5-way S5** keypad uses the standard up / down / central-press / left / right
positions; the central "PRESS" button is the primary select/confirm.

| Position | Key | Instance |
| :--- | :--- | :--- |
| Up | WIO_KEY_A | `bA` |
| Down | WIO_KEY_B | `bB` |
| Centre | WIO_KEY_C | `bC` |
| 5-way up | `WIO_5S_UP` | `S5U` |
| 5-way press | `WIO_5S_PRESS` | `S5` |
| 5-way down | `WIO_5S_DOWN` | `S5D` |
| 5-way left | `WIO_5S_LEFT` | `S5L` |
| 5-way right | `WIO_5S_RIGHT` | `S5R` |

> **Note:** as of V0.1 the five `S5*` buttons are **instantiated but not yet
> wired into the state machine** — only the A/B/C buttons drive navigation. If
> you are reassigning the S5 keys (e.g. to zoom or add channels), see
> [`REVIEW_ISSUES.md`](REVIEW_ISSUES.md).

---

## Calibration

`performCalibration()` / `runCalibrationSequence()` take several samples while a
white reference is in place (`takeMeasurementsWithBulb()`) and store the inverse
of the average per channel into `calibrationFactors[18]`.

> **Verify on hardware.** The current averaging reads the sensor's cumulative
> calibrated totals, which may not be a valid white-reference measurement — the
> exact getter (`getRawX`/`getDeltaX`/`getCalibratedX`) to average over must be
> confirmed against the AS7265X library on the target. See
> [`REVIEW_ISSUES.md` → Item 1](REVIEW_ISSUES.md).

---

## License

Dual-attribution.

- This project is released under the **MIT License** — see
  [`LICENSE.md`](LICENSE.md).
- The **SparkFun AS7265X** sensor code is `MIT`; the original Spectral Triad code
  is **Reinhardt Behm's** — please respect its own
  license/attribution.

## Contributing

This is an alpha; contributions are welcome as PRs. See
[`REVIEW_ISSUES.md`](REVIEW_ISSUES.md) for the current backlog and open
questions (dead `withLed` branch, persistence of calibration factors, hardware
flow for `S5`).
