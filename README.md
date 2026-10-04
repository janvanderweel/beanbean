# Beanbeam — Coffee Spectrometer

A small embedded program that reads the **18-channel Spectrum Triad
[AS7265X](https://www.spektral.com/product/spectrum-triad-4-2-as7265x-ultraviolet-visible-near-infrared-spectrophotometer/)** and renders per-wavelength light bars on a TFT screen.

This is early **alpha (V0.1)** code — the focus is the logic (menus,
calibration, display) rather than a production-grade instrument.

Written for a **Seeed Studio WIO Terminal**, running the
[Arduino](https://www.arduino.cc/) / [IDE](https://www.arduino.cc/en/software)
sketch `src/beanbeam/beanbeam.ino`.

Made by **Jan van der Weel**. The hardware sensor and most libraries are from **SparkFun Electronics**.

---

## What it does

| Feature | Description |
| :--- | :--- |
| **18-channel spectral read** | 18 AS7265X channels (410 nm → 940 nm, incl. near-infrared) polled over I2C. |
| **Live bars & values** | For each wavelength a bar graph is drawn on the TFT, scaled per measurement. |
| **Roast color** | The 860 nm (near-IR) reading is mapped to a roast-color number + label via a two-point fit. |
| **Gain selection** | Four fixed sensor gains (`1x / 3.7x / 16x / 64x`) available in firmware. |
| **Button navigation** | The **A / B / C** buttons drive the menu and states (5-way centre press also confirms). |
| **Calibration flow** | Menu → *Calibrate* → sample a DARK reference, then a LIGHT reference; the firmware fits the line and stores it in flash. |
| **About / info screens** | Device type, hardware/firmware versions printed on boot and shown on an About screen. |

---

## Hardware required

- **WIO Terminal (or compatible)** — the Arduino board the sketch runs on.
- **SparkFun AS7265X Spectral Triad** — connected via **I2C** (SCL/SDA), e.g. 400 kHz.
- **1.8"–2.4" SPI/parallel TFT display** — driven by Lovyan GFX/LGFX (via a breakout on the WIO).
- **Button matrix** — the WIO's push buttons (A/B/C) and the **5-way S5** keypad.

> Building requires the Arduino toolchain (Arduino IDE or `arduino-cli`) with the
> Seeed WIO Terminal board support and the SparkFun AS7265X, FlashStorage_SAMD,
> and LovyanGFX libraries installed. See [Build](#build) below.

---

## Repository layout

```
src/beanbeam/
├── beanbeam.ino     # Main sketch: setup()/loop(), menus, calibration, display
├── button.h         # Header: Button class (poll-based debounced button)
└── button.cpp       # Implementation: debouncing, edge/level detection
LICENSE.md           # MIT license for this project
```

`.DS_Store` files are git-ignored.

---

## Build

1. Install the **Arduino IDE** (or `arduino-cli`) and add the **Seeed WIO
   Terminal** board support (`Seeeduino:samd`).
2. Install the required libraries via the Library Manager:
   **SparkFun Spectral Triad AS7265x**, **FlashStorage_SAMD**, and **LovyanGFX**.
3. Open `src/beanbeam/beanbeam.ino`. `button.h` / `button.cpp` compile as part
   of the same sketch.
4. Upload and open the **Serial Monitor** at **115200 baud** (boot prints device
   type, firmware, and the wavelength labels, then the menu).

With `arduino-cli` the compile command is:

```
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal src/beanbeam
```

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

- **Measure** → samples the 18 wavelengths, draws the spectral bars, and shows
  the roast-color number/label from the 860nm reading.
- **Calibrate** → two-point roast-color calibration: place the **DARK**
  reference and press **C**, then the **LIGHT** reference and press **C**. The
  firmware fits a line through the two 860nm readings.
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
> wired into the state machine** — only the A/B/C buttons drive navigation
> (the 5-way centre press also acts as confirm).

---

## Calibration

Roast color is a single-channel **860nm** (near-IR) measurement mapped to a
roast-color number by a two-point linear fit:

```
score = slope * raw860 + intercept
```

Calibration samples two roasted references — a **DARK** one and a **LIGHT** one
— each averaged over `CAL_SAMPLES` readings under the measurement illumination.
You assign the two scores in the firmware (edit `DARK_SCORE` / `LIGHT_SCORE`
near the top of the calibration section), and the fit is stored in flash so it
survives a power cycle. Darker roasts reflect less 860nm light and score lower,
so the LIGHT reference must read a clearly higher 860nm count than the DARK one
or the fit is rejected.

> **Optical setup matters.** The fit is only valid for a fixed gain, bulb
> current, and sample distance. If you change any of those, re-run calibration.
> There is no white/dark normalization yet, so the two-point fit is only
> partially self-normalizing against bulb aging — re-calibrate if readings
> drift.

---

## License

Dual-attribution.

- This project is released under the **MIT License** — see
  [`LICENSE.md`](LICENSE.md).
- The **SparkFun AS7265X** sensor code is `MIT`; the original Spectral Triad code
  is **Reinhardt Behm's** — please respect its own
  license/attribution.

## Contributing

This is an alpha; contributions are welcome as PRs. Known open areas: wiring the
`S5` 5-way keys into the state machine, a dark/white reference normalization
pass for the 860nm reading, and raising gain so the 860nm signal uses more of
the ADC range.
