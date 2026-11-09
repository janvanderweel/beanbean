# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview
**Beanbeam** is an embedded application for the Seeed Studio WIO Terminal. It functions as a **Coffee Spectrometer**, utilizing the **SparkFun AS7265X Spectral Triad** to measure light across 18 spectral channels (410 nm to 940 nm).

The primary feature is identifying roast levels based on the **860nm (Near-IR)** wavelength. The system uses a two-point calibration (Dark vs. Light references) to map raw 860nm readings to an Agtron-based roast-color scale.

## Architecture & Structure
The project follows a simple state-machine architecture for embedded interaction:

- **State Machine (`AppState`):** The application transitions between `STATE_MENU`, `STATE_MEASURE`, `STATE_CAL_LOW`, `STATE_CAL_HIGH`, and `STATE_ABOUT`.
- **Input Handling:** A custom `Button` class (in `button.h`/`button.cpp`) manages debouncing and button detection (A, B, C, and the 5-way S5 keypad).
- **Measurement Logic:** 
    - Measurements are performed as "single-shot" events: a warm-up period, then a series of averaged samples.
    - Raw counts are mapped to a display-ordered array for chart rendering.
- **Calibration Logic:**
    - A linear regression model (`score = slope * raw860 + intercept`) is used to calibrate the 860nm signal.
    - Calibration data is persisted in non-volatile memory via `FlashStorage_SAMD`.
- **Hardware Layer:**
    - **Sensor:** SparkFun AS7265X via I2C.
    - **Display:** LovyanGFX/LGFX for a 1.8"–2.4" SPI/parallel TFT.
    - **Interaction:** WIO Terminal buttons (A, B, C) and S5 5-way keypad.

### Files
- `src/beanbeam/beanbeam.ino`: The main application loop, state machine logic, and calibration routines.
- `src/beanbeam/button.h`: Button class definitions.
- `src/beanbeam/button.cpp`: Button class implementation.
- `LICENSE.md`: Project licensing.

## Development Commands
The project requires the **Arduino** environment.

### Compilation
To compile the project using `arduino-cli`:
```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal src/beanbeam
```

### Running
1. Open the project in the **Arduino IDE** or use `arduino-cli`.
2. Ensure the **Seeed WIO Terminal** board support and **SparkFun AS7265X** library are installed.
3. Upload the sketch.
4. Open the **Serial Monitor** at **115200 baud** to view boot info and roast-color results.

## Key Interaction Notes
- **Button Layout:**
    - **A / B / C:** Navigation and selection.
    - **S5:** 5-way keypad (Up, Down, Press, Left, Right).
- **Roast Calibration:** 
    - Requires two reference points (Dark = 31.2, Light = 91.0).
    - The 860nm signal must be higher for the Light reference to produce a valid calibration.
- **Bulb Safety:** The IR LED is rated for ~65 mA. Ensure `BULB_CURRENT_IR` is kept at 50 mA or lower.
