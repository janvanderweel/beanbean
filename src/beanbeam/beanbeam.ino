/*
 * Coffee spectrometer on the Seeed WIO Terminal
 *
 * Read the 18 channels of spectral light over I2C using the Spectral Triad
 * By: Jan van der Weel using Reinhardt Behm's code as a starting point
 * SparkFun Electronics
 * Date: October 25th, 2024
 * License: MIT. See license file for more information but you can
 */

#include <Arduino.h>
#include <Wire.h>
#include <FlashStorage_SAMD.h> // flash-emulated EEPROM on SAMD51 (WIO Terminal)

// Set to 1 to run the raw-vs-calibrated diagnostic once at boot (design §7
// item 1 / REVIEW_ISSUES item 1). Set to 0 once the getter question is settled.
#define CAL_DIAG_ON_BOOT 1

#define LGFX_AUTODETECT
#include "SparkFun_AS7265X.h" // Click here to get the library: http://librarymanager/All#SparkFun_AS7265X
#include "button.h"
/*#include <Arduino_DebugUtils.h>*/
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
AS7265X sensor;
static LGFX lcd;

// Define application states
enum AppState {
  STATE_MENU,
  STATE_MEASURE,
  STATE_CAL_LOW,   // two-plate calibration: waiting to sample the LOW (brown) plate
  STATE_CAL_HIGH,  // two-plate calibration: waiting to sample the HIGH (red) plate
  STATE_ABOUT
};

AppState currentState = STATE_MENU;
int menuSelection = 0; // Tracks the current menu selection

bool sensorReady = false; // true once sensor.begin() succeeds; gates all sensor use

int gain = 0;

static const int gains[4] = { AS7265X_GAIN_1X, AS7265X_GAIN_37X, AS7265X_GAIN_16X, AS7265X_GAIN_64X };
static const char *sgain[4] = { "1X  ", "3.7X", "16X ", "64X " };

void setGain(int i)
{
  gain = i & 0x03;
  sensor.setGain(gains[gain]);
  Serial.print("$G,");
  Serial.print(gain);
  Serial.println(",***********");
  lcd.drawString(sgain[gain], 200, 0);
}

Button bA(WIO_KEY_A), bB(WIO_KEY_B), bC(WIO_KEY_C);
Button S5(WIO_5S_PRESS), S5U(WIO_5S_UP), S5D(WIO_5S_DOWN), S5L(WIO_5S_LEFT), S5R(WIO_5S_RIGHT);

std::uint32_t colors[18];


static void initMap();
void setup()
{
  /*int i;
  char s[20];*/
  Serial.begin(115200);
  // Wait briefly for the USB Serial monitor so boot diagnostics are visible.
  // Bounded (~2s) so the device still boots when running standalone.
  for (uint32_t t0 = millis(); !Serial && (millis() - t0) < 2000; ) { }
  Serial.println("AS7265x Spectral Triad");
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(255);
  lcd.setColorDepth(24);
  lcd.fillScreen(0);
  lcd.clear(0);

  // Display the menu
  displayMenu();

  // Initialize current state to MENU
  currentState = STATE_MENU;

  // Sensor initialization. A missing/flaky sensor must NOT freeze the UI:
  // the menu still needs to run so the device is navigable. We record
  // whether the sensor came up and gate sensor-dependent work on it.
  //
  // The AS7265X is a 3-chip device: begin() first checks the master ACKs at
  // 0x49, then reads DEV_SELECT_CONTROL over a virtual-register handshake to
  // confirm the two slave chips. That handshake can time out (returning 0 =>
  // "slaves not detected") if the slaves haven't finished booting when begin()
  // runs. Symptom: an I2C scan finds 0x49 but begin() still returns false.
  // Fix at the source: give the sensor time to power up, then retry begin().
  delay(500); // let all three AS726x chips finish their power-on boot
  for (int attempt = 0; attempt < 5 && !sensorReady; attempt++)
  {
    sensorReady = sensor.begin();
    if (!sensorReady)
    {
      Serial.print("sensor.begin() failed, attempt ");
      Serial.println(attempt + 1);
      delay(300);
    }
  }

  if (!sensorReady)
  {
    Serial.println("Sensor does not appear to be connected. Please check wiring. Continuing without sensor.");

    // Diagnostic: scan the I2C bus so we can see what actually responds.
    // AS7265X default address is 0x49 (7-bit). Nothing listed => wiring/power/
    // pull-up problem on SDA/SCL. 0x49 listed but begin() still fails => the
    // two slave chips aren't detected (check the Triad's internal ribbon/board).
    Wire.begin();
    Serial.println("I2C scan:");
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_RED);
    lcd.drawString("Sensor begin() failed", 5, 5);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString("I2C scan (expect 0x49):", 5, 20);

    int found = 0;
    int lineY = 35;
    for (uint8_t addr = 1; addr < 127; addr++)
    {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0)
      {
        Serial.print("  device at 0x");
        Serial.println(addr, HEX);
        char s[24];
        sprintf(s, "  found 0x%02X", addr);
        lcd.drawString(s, 5, lineY);
        lineY += 12;
        found++;
      }
    }
    if (found == 0)
    {
      Serial.println("  (no I2C devices found)");
      lcd.drawString("  (no I2C devices found)", 5, lineY);
    }
    delay(4000); // leave the scan on-screen long enough to read
    displayMenu();
  }

  if (sensorReady)
  {
    setGain(2);
    sensor.setIndicatorCurrent(AS7265X_INDICATOR_CURRENT_LIMIT_1MA);
    Wire.setClock(400000);

    byte deviceType = sensor.getDeviceType();
    Serial.print("AMS Device Type: 0x");
    Serial.println(deviceType, HEX);

    byte hardwareVersion = sensor.getHardwareVersion();
    Serial.print("AMS Hardware Version: 0x");
    Serial.println(hardwareVersion, HEX);

    byte majorFirmwareVersion = sensor.getMajorFirmwareVersion();
    Serial.print("Major Firmware Version: 0x");
    Serial.println(majorFirmwareVersion, HEX);

    byte patchFirmwareVersion = sensor.getPatchFirmwareVersion();
    Serial.print("Patch Firmware Version: 0x");
    Serial.println(patchFirmwareVersion, HEX);

    byte buildFirmwareVersion = sensor.getBuildFirmwareVersion();
    Serial.print("Build Firmware Version: 0x");
    Serial.println(buildFirmwareVersion, HEX);

#if CAL_DIAG_ON_BOOT
    diagRawVsCalibrated(); // one-time bring-up check for REVIEW_ISSUES item 1
#endif
  }

  Serial.println("A,B,C,D,E,F,G,H,I,J,K,L,R,S,T,U,V,W");
  pinMode(LED_BUILTIN, OUTPUT);
  colors[0] = lcd.color888(0x4b, 0x00, 0x82); // 410 nm - Violet
  colors[1] = lcd.color888(0x8a, 0x2b, 0xe2); // 435 nm - Violet
  colors[2] = lcd.color888(0x00, 0x00, 0xff); // 460 nm - Blue
  colors[3] = lcd.color888(0x00, 0xbf, 0xff); // 485 nm - Blue
  colors[4] = lcd.color888(0x00, 0xff, 0xff); // 510 nm - Cyan
  colors[5] = lcd.color888(0x00, 0xff, 0x00); // 535 nm - Green
  colors[6] = lcd.color888(0xad, 0xff, 0x2f); // 560 nm - Yellow-Green
  colors[7] = lcd.color888(0xff, 0xff, 0x00); // 585 nm - Yellow
  colors[8] = lcd.color888(0xff, 0xa5, 0x00); // 610 nm - Orange
  colors[9] = lcd.color888(0xff, 0x00, 0x00); // 645 nm - Red
  colors[10] = lcd.color888(0xb2, 0x22, 0x22); // 680 nm - Deep Red
  colors[11] = lcd.color888(0x8b, 0x00, 0x00); // 705 nm - Deep Red
  colors[12] = lcd.color888(0x87, 0x00, 0x00); // 730 nm - Near Infrared
  colors[13] = lcd.color888(0x78, 0x00, 0x00); // 760 nm - Near Infrared
  colors[14] = lcd.color888(0x69, 0x00, 0x00); // 810 nm - Near Infrared
  colors[15] = lcd.color888(0x5a, 0x00, 0x00); // 860 nm - Near Infrared
  colors[16] = lcd.color888(0x4b, 0x00, 0x00); // 900 nm - Infrared
  colors[17] = lcd.color888(0x3c, 0x00, 0x00); // 940 nm - Infrared
  initMap();
  loadCalibration(); // override defaults with persisted calibration if present
}

int led = 0;
bool withLed = true;
static const char *freq[18] =
{
  "410", "435", "460", "485", "510", "535",   // ABCDEF
  "560", "585", "610", "645", "680", "705",   // GHRISJ
  "730", "760", "810", "860", "900", "940"    // TUVWKL
};

int rmap[18];
float calibrationFactors[18]; // Multipliers for each channel

// ===================== Two-plate (Tonino-lite) colour calibration ==========
// See design_calibration.md V1. Two reference disks (brown=low, red=high)
// define a line in channel-ratio space; a fixed scale polynomial maps that to
// the Tonino roast number. Constants below are the real Tonino values.

static const int   CAL_SAMPLES  = 3;          // samples averaged per measurement

// Stage-1 target ratios the two plates are pinned to (Tonino tonino_tcs3200.h).
static const float LOW_TARGET   = 1.5f;       // brown disk target r/b
static const float HIGH_TARGET  = 3.7f;       // red   disk target r/b

// Stage-1 factory defaults (Tonino tonino.h) used before any user calibration.
static const float DEFAULT_CAL_0 = 1.011949f;
static const float DEFAULT_CAL_1 = -0.094599f;

// Stage-2 fixed scale polynomial (Tonino tonino.h DEFAULT_SCALE_*).
// Leading two terms are 0, so it is effectively linear.
static const float SCALE_0 = 0.0f;
static const float SCALE_1 = 0.0f;
static const float SCALE_2 = 102.2727273f;
static const float SCALE_3 = -128.4090909f;

// Chosen red/blue channels (indices into values[]/freq[]). Verify on hardware
// (design §5/§7): red channel must read higher than blue on both disks.
static const int   DEFAULT_CH_RED  = 9;       // ~645 nm (red)
static const int   DEFAULT_CH_BLUE = 2;       // ~460 nm (blue)

// Reject a near-vertical (degenerate) fit (design §3).
static const float MIN_RATIO_SEPARATION = 0.05f;

// The fitted two-plate colour line + chosen channels (persisted).
struct ColourCalibration {
  float cal0;    // Stage-1 slope
  float cal1;    // Stage-1 intercept
  int   chRed;   // chosen red channel index
  int   chBlue;  // chosen blue channel index
  bool  valid;   // true once a good 2-point fit has been stored
};

ColourCalibration colourCal = {
  DEFAULT_CAL_0, DEFAULT_CAL_1, DEFAULT_CH_RED, DEFAULT_CH_BLUE, false
};

// Persisted blob (white-reference factors + colour line) — design §6a.
static const uint32_t CAL_MAGIC   = 0xB3A11CALu; // "is this ours?"
static const uint16_t CAL_VERSION = 1;

struct PersistedCalibration {
  uint32_t magic;
  uint16_t version;
  float    calibrationFactors[18];
  ColourCalibration colourCal;
};

FlashStorage(calStore, PersistedCalibration); // reserves one flash slot

// Map an averaged channel ratio to the Tonino roast number (Stage 1 + Stage 2).
float roastNumberFromRatio(float ratio)
{
  float v = colourCal.cal0 * ratio + colourCal.cal1;
  return SCALE_0 * v * v * v + SCALE_1 * v * v + SCALE_2 * v + SCALE_3;
}

// Map the Tonino roast number to one of 7 roast-level labels.
// Tonino convention: HIGHER number = LIGHTER roast. Thresholds are the upper
// bound of each (darker) bucket and are TUNABLE — adjust on real roasts.
// (Provisional; calibrated on the two disks, not on graded roast samples yet.)
struct RoastBucket { float maxNumber; const char *label; };
static const RoastBucket roastBuckets[] = {
  {  75.0f, "ULTRA DARK" },
  {  95.0f, "DARK" },
  { 110.0f, "MEDIUM-DARK" },
  { 125.0f, "MEDIUM" },
  { 140.0f, "MEDIUM-LIGHT" },
  { 155.0f, "LIGHT" },
  { 1e9f,   "ULTRA LIGHT" }   // everything above the last threshold
};
static const int numRoastBuckets = sizeof(roastBuckets) / sizeof(roastBuckets[0]);

const char *roastLabel(float number)
{
  for (int i = 0; i < numRoastBuckets; ++i) {
    if (number <= roastBuckets[i].maxNumber) return roastBuckets[i].label;
  }
  return roastBuckets[numRoastBuckets - 1].label;
}
// ===========================================================================

static void initMap()
{
  static const int map[18] = {
    0, 1, 2, 3, 4, 5,
    6, 7, 12, 8, 13, 9,
    14, 15, 16, 17, 10, 11
  };
  for (int n = 0; n < 18; ++n)
  {
    calibrationFactors[n] = 1.0; // Initialize factors to 1.0
    for (int i =  0; i < 18; ++i)
    {
      if (map[i] == n)
      {
        rmap[n] = i;
        break;
      }
    }
  }
}

float values[18];
bool measurementPending = false;

// --- Display layout (screen is 320x240, rotation 1) ---------------------
// Bottom half: vertical spectral bar chart.
// Top half: processed-data panel (roast label + development score).
// NOTE: the panel currently shows placeholders only. Real calibration and
// roast-score computation are intentionally deferred to later sessions;
// nothing here computes a stored metric — bar scaling is UI-only (relative
// to the current max), the raw/calibrated channel values are untouched.

static const int CHART_BASELINE_Y = 222; // y of the bar baseline (bars grow up)
static const int CHART_TOP_Y      = 128; // highest a full-scale bar reaches
static const int CHART_LEFT_X     = 6;   // left margin of the chart
static const int CHART_RIGHT_X    = 314; // right margin of the chart

// Spectral bands: consecutive channel ranges [first,last] with a short label.
// Channels are in ascending-wavelength order (index matches freq[]/values[]).
struct SpectralBand {
  int first;          // first channel index in the band
  int last;           // last channel index in the band
  const char *label;  // short band label drawn under the group
};

static const SpectralBand bands[] = {
  {  0,  1, "UV" },    // 410, 435 nm
  {  2,  3, "BLU" },   // 460, 485 nm
  {  4,  6, "GRN" },   // 510, 535, 560 nm
  {  7,  8, "YEL" },   // 585, 610 nm
  {  9, 11, "RED" },   // 645, 680, 705 nm
  { 12, 15, "NIR" },   // 730, 760, 810, 860 nm  (near-infrared)
  { 16, 17, "IR" }     // 900, 940 nm            (infrared)
};
static const int numBands = sizeof(bands) / sizeof(bands[0]);

void enableBulbs()
{
  sensor.enableBulb(AS7265x_LED_WHITE);
  sensor.enableBulb(AS7265x_LED_IR);
  sensor.enableBulb(AS7265x_LED_UV);
}

void disableBulbs()
{
  sensor.disableBulb(AS7265x_LED_WHITE);
  sensor.disableBulb(AS7265x_LED_IR);
  sensor.disableBulb(AS7265x_LED_UV);
}

// Draw the STATIC labels of the top-half processed-data panel (drawn once when
// entering the measure screen). The dynamic values are drawn by showScore().
void showResults()
{
  const int panelTop = 20;

  lcd.setTextSize(1);
  lcd.setTextColor(lcd.color888(160, 160, 160));
  lcd.drawString("ROAST", CHART_LEFT_X, panelTop);
  lcd.drawString("TONINO #", CHART_LEFT_X, panelTop + 52);

  // Faint divider between the results panel and the chart.
  lcd.drawFastHLine(0, CHART_TOP_Y - 6, 320, lcd.color888(48, 48, 48));
}

// Draw the DYNAMIC roast label + Tonino number for the given channel ratio.
// Called each measurement. Erases its own regions before redrawing so stale
// (longer) text is cleared. If no valid calibration exists, shows dashes.
void showScore(float ratio)
{
  const int panelTop = 20;
  const int labelY = panelTop + 12;
  const int numY   = panelTop + 64;

  // Erase the two value regions (label row + number row).
  lcd.fillRect(CHART_LEFT_X, labelY, 320 - CHART_LEFT_X, 20, TFT_BLACK);
  lcd.fillRect(CHART_LEFT_X, numY,   320 - CHART_LEFT_X, 24, TFT_BLACK);

  if (!colourCal.valid) {
    lcd.setTextSize(2);
    lcd.setTextColor(lcd.color888(160, 160, 160));
    lcd.drawString("uncalibrated", CHART_LEFT_X, labelY);
    lcd.setTextSize(3);
    lcd.drawString("---", CHART_LEFT_X, numY);
    return;
  }

  float number = roastNumberFromRatio(ratio);

  // Roast label (size 2 so long names like "MEDIUM-LIGHT" fit the width).
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(roastLabel(number), CHART_LEFT_X, labelY);

  // Tonino number (big).
  char s[16];
  sprintf(s, "%.0f", number);
  lcd.setTextSize(3);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(s, CHART_LEFT_X, numY);
}

// Draw the spectral bar chart in the BOTTOM half of the screen.
// Bars are vertical (grow upward from CHART_BASELINE_Y), one per channel,
// scaled relative to the current maximum (UI-only scaling). Per-channel
// numbers are gone; short band labels are printed under each group instead.
void showValues(){
  float maxv = 0;
  for (int n = 0; n < 18; ++n)
  {
    if (values[n] > maxv)
    {
      maxv = values[n];
    }
  }
  if (maxv < 100.)
  {
    maxv = 100.;
  }

  const std::uint32_t gray = lcd.color888(64, 64, 64);
  const int chartW  = CHART_RIGHT_X - CHART_LEFT_X; // usable width
  const int chartH  = CHART_BASELINE_Y - CHART_TOP_Y; // full-scale bar height
  const int pitch   = chartW / 18;                  // per-channel column width
  const int barW    = pitch - 2;                    // leave a 2px gap

  for (int n = 0; n < 18; ++n)
  {
    int colX = CHART_LEFT_X + n * pitch;

    float value = values[n] / maxv;
    int h = (int)(value * chartH);
    h = constrain(h, 0, chartH);

    // Unfilled (background) portion above the bar.
    int gapH = chartH - h;
    if (gapH > 0)
    {
      lcd.fillRect(colX, CHART_TOP_Y, barW, gapH, gray);
    }
    // Filled bar, grown up from the baseline.
    if (h > 0)
    {
      lcd.fillRect(colX, CHART_BASELINE_Y - h, barW, h, colors[n]);
    }
  }
}

// Draw the static band labels + separators under the chart baseline.
// Called once when entering the measure screen (labels never change).
void drawChartLabels()
{
  const int chartW = CHART_RIGHT_X - CHART_LEFT_X;
  const int pitch  = chartW / 18;

  lcd.setTextSize(1);
  const int labelY = CHART_BASELINE_Y + 4;
  for (int b = 0; b < numBands; ++b)
  {
    int startX = CHART_LEFT_X + bands[b].first * pitch;
    int endX   = CHART_LEFT_X + (bands[b].last + 1) * pitch;
    int midX   = (startX + endX) / 2;

    // Centre the (short) label under its group.
    int labelW = lcd.textWidth(bands[b].label);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString(bands[b].label, midX - labelW / 2, labelY);

    // Thin tick between bands.
    if (b < numBands - 1)
    {
      lcd.drawFastVLine(endX - 1, CHART_BASELINE_Y + 1, 2, lcd.color888(90, 90, 90));
    }
  }
}

// Draw the full measure screen chrome once (results panel + chart labels).
// The live bars are drawn every frame by showValues().
void drawMeasureScreen()
{
  lcd.fillScreen(TFT_BLACK);
  showResults();
  showScore(0.0f); // initial state (dashes / uncalibrated) until first reading
  drawChartLabels();
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE);
}
void showValue(int n, float  value)
{
  values[rmap[n]] = value;
}

// Function to display the menu
void displayMenu() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);

  String menuItems[3] = { "Measure", "Calibrate", "About" };

  for (int i = 0; i < 3; i++) {
    if (i == menuSelection) {
      // Highlight selected item with a background fill for better UX
      lcd.fillRect(5, 40 + i * 30, 200, 25, TFT_GREEN);
      lcd.setTextColor(TFT_BLACK);
      lcd.drawString(">", 5, 40 + i * 30);
    }
    else {
      lcd.setTextColor(TFT_WHITE);
      lcd.drawString(" ", 5, 40 + i * 30);
    }
    lcd.drawString(menuItems[i], 20, 40 + i * 30);
  }
}

// Function to display the about screen
void displayAboutScreen() {
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("About", 20, 40);
  lcd.setTextSize(2);
  lcd.drawString("Beanbeam V0.1 alpha", 20, 80);
}

// Function to perform measurement without blocking the main loop.
void performMeasurement() {
  if (currentState != STATE_MEASURE) return;

  // No sensor: don't touch I2C (would block/hang). Show a notice instead.
  if (!sensorReady) {
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_RED);
    lcd.drawString("NO SENSOR", 220, 0);
    lcd.setTextColor(TFT_WHITE);
    return;
  }

  if (!measurementPending) {
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_RED);
    lcd.drawString("LIVE", 260, 0);
    lcd.setTextColor(TFT_WHITE);

    led = !led;
    digitalWrite(LED_BUILTIN, led);

    if (withLed) {
      enableBulbs();
    }

    sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT);
    measurementPending = true;
    return;
  }

  if (!sensor.dataAvailable()) {
    return;
  }

  if (withLed) {
    disableBulbs();
  }

  float (AS7265X::*getters[])() = {
    &AS7265X::getCalibratedA, &AS7265X::getCalibratedB, &AS7265X::getCalibratedC,
    &AS7265X::getCalibratedD, &AS7265X::getCalibratedE, &AS7265X::getCalibratedF,
    &AS7265X::getCalibratedG, &AS7265X::getCalibratedH, &AS7265X::getCalibratedI,
    &AS7265X::getCalibratedJ, &AS7265X::getCalibratedK, &AS7265X::getCalibratedL,
    &AS7265X::getCalibratedR, &AS7265X::getCalibratedS, &AS7265X::getCalibratedT,
    &AS7265X::getCalibratedU, &AS7265X::getCalibratedV, &AS7265X::getCalibratedW
  };

  Serial.print("$L,");
  for (int n = 0; n < 18; n++) {
    float v = (sensor.*getters[n])();
    v *= calibrationFactors[n];
    showValue(n, v);
    Serial.print(v);
    if (n < 17) Serial.print(",");
  }

  showValues();

  // Roast score from the two chosen channels' ratio (design §4).
  {
    float b = values[colourCal.chBlue];
    float ratio = (b > 0.0f) ? (values[colourCal.chRed] / b) : 0.0f;
    showScore(ratio);
    Serial.print("$R,");
    Serial.print(ratio, 5);
    Serial.print(",");
    Serial.println(colourCal.valid ? roastNumberFromRatio(ratio) : 0.0f, 1);
  }

  Serial.println();

  Serial.print("$T,");
  int oneSensorTemp = sensor.getTemperature();
  Serial.print(oneSensorTemp);
  float threeSensorTemp = sensor.getTemperatureAverage();
  Serial.print(",");
  Serial.print(threeSensorTemp, 2);
  Serial.println();

  {
    char s[20];
    sprintf(s, "%4.1f°C", threeSensorTemp);
    lcd.drawString(s, 250, 0);
  }

  measurementPending = false;
}

// ===================== Persistence (design §6a) ============================
// Load persisted calibration if the flash slot holds a valid, matching blob;
// otherwise keep the compiled-in defaults (calibrationFactors[]=1.0 from
// initMap(), colourCal = factory defaults).
void loadCalibration() {
  PersistedCalibration p;
  calStore.read(p); // FlashStorage_SAMD: read via out-parameter
  if (p.magic == CAL_MAGIC && p.version == CAL_VERSION) {
    for (int i = 0; i < 18; i++) calibrationFactors[i] = p.calibrationFactors[i];
    colourCal = p.colourCal;
    Serial.println("Calibration loaded from flash.");
  } else {
    Serial.println("No valid calibration in flash; using defaults.");
  }
}

// Write current calibration to flash. Only call after a successful calibration
// (flash wear) — never per loop/measurement.
void saveCalibration() {
  PersistedCalibration p;
  p.magic = CAL_MAGIC;
  p.version = CAL_VERSION;
  for (int i = 0; i < 18; i++) p.calibrationFactors[i] = calibrationFactors[i];
  p.colourCal = colourCal;
  calStore.write(p);

  // Read-back verification (bring-up aid, design §6a).
  PersistedCalibration back;
  calStore.read(back);
  if (back.magic == CAL_MAGIC && back.colourCal.valid == colourCal.valid) {
    Serial.println("Calibration saved to flash (verified).");
  } else {
    Serial.println("WARNING: calibration flash write did not verify.");
  }
}

// ===================== Shared sampling =====================================
// Take one fresh measurement into values[] via the existing getters + rmap.
// NOTE (REVIEW_ISSUES item 1 / design §7): if getCalibratedX() proves to be
// cumulative on hardware, switch the getters[] below to the raw getters
// getA()..getW() (uint16_t) — this is the single source of truth for channel
// reads. Use diagRawVsCalibrated() to decide.
void sampleChannelsOnce() {
  if (withLed) sensor.takeMeasurementsWithBulb();
  else sensor.takeMeasurements();

  float (AS7265X::*getters[])() = {
    &AS7265X::getCalibratedA, &AS7265X::getCalibratedB, &AS7265X::getCalibratedC,
    &AS7265X::getCalibratedD, &AS7265X::getCalibratedE, &AS7265X::getCalibratedF,
    &AS7265X::getCalibratedG, &AS7265X::getCalibratedH, &AS7265X::getCalibratedI,
    &AS7265X::getCalibratedJ, &AS7265X::getCalibratedK, &AS7265X::getCalibratedL,
    &AS7265X::getCalibratedR, &AS7265X::getCalibratedS, &AS7265X::getCalibratedT,
    &AS7265X::getCalibratedU, &AS7265X::getCalibratedV, &AS7265X::getCalibratedW
  };
  for (int n = 0; n < 18; n++) {
    showValue(n, (sensor.*getters[n])()); // showValue applies rmap into values[]
  }
}

// Average CAL_SAMPLES fresh reads and return the chosen red/blue channel ratio.
float samplePlateRatio() {
  float rSum = 0, bSum = 0;
  for (int s = 0; s < CAL_SAMPLES; s++) {
    sampleChannelsOnce();
    rSum += values[colourCal.chRed];
    bSum += values[colourCal.chBlue];
    delay(150);
  }
  float rAvg = rSum / CAL_SAMPLES;
  float bAvg = bSum / CAL_SAMPLES;
  return (bAvg > 0.0f) ? (rAvg / bAvg) : 0.0f;
}

// ===================== Diagnostic (design §7 item 1) =======================
// Print getRaw vs getCalibrated for one channel across several reads so we can
// see on hardware whether getCalibratedX() accumulates. Run from the serial /
// bring-up; safe, read-only.
void diagRawVsCalibrated() {
  if (!sensorReady) { Serial.println("diag: no sensor"); return; }
  Serial.println("diag: reads of channel H (~645nm) raw vs calibrated:");
  for (int i = 0; i < 5; i++) {
    if (withLed) sensor.takeMeasurementsWithBulb();
    else sensor.takeMeasurements();
    Serial.print("  raw="); Serial.print(sensor.getA()); // A = 410nm master ch
    Serial.print(" cal="); Serial.println(sensor.getCalibratedA());
    delay(200);
  }
}

// ===================== Two-plate calibration flow ==========================
float ratioLow = 0.0f; // captured from the brown plate before fitting

// Screen prompting for the LOW (brown) plate.
void displayCalLowScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Place LOW (brown)", 10, 40);
  lcd.drawString("Press C to sample", 10, 80);
}

// Screen prompting for the HIGH (red) plate.
void displayCalHighScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Place HIGH (red)", 10, 40);
  lcd.drawString("Press C to sample", 10, 80);
}

// Sample the low plate, store ratio, advance to the high-plate state.
void doCalLow() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.drawString("Sampling LOW...", 10, 40);
  ratioLow = samplePlateRatio();
  Serial.print("Cal LOW ratio="); Serial.println(ratioLow, 5);
  currentState = STATE_CAL_HIGH;
  displayCalHighScreen();
}

// Sample the high plate, fit the line, guard, persist, return to menu.
void doCalHigh() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.drawString("Sampling HIGH...", 10, 40);
  float ratioHigh = samplePlateRatio();
  Serial.print("Cal HIGH ratio="); Serial.println(ratioHigh, 5);

  // Degenerate-line guard (design §3/§4).
  if (fabs(ratioHigh - ratioLow) < MIN_RATIO_SEPARATION) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_RED);
    lcd.drawString("Cal FAILED:", 10, 40);
    lcd.drawString("plates too similar", 10, 70);
    Serial.println("Calibration rejected: ratios too close.");
    delay(2500);
    currentState = STATE_MENU;
    displayMenu();
    return;
  }

  colourCal.cal0 = (HIGH_TARGET - LOW_TARGET) / (ratioHigh - ratioLow);
  colourCal.cal1 = LOW_TARGET - colourCal.cal0 * ratioLow;
  colourCal.valid = true;
  Serial.print("cal0="); Serial.print(colourCal.cal0, 5);
  Serial.print(" cal1="); Serial.println(colourCal.cal1, 5);

  saveCalibration();

  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_GREEN);
  lcd.drawString("Cal OK", 10, 40);
  lcd.setTextColor(TFT_WHITE);
  delay(1500);
  currentState = STATE_MENU;
  displayMenu();
}

void loop() {
  const bool keyAPressed = bA();
  const bool keyBPressed = bB();
  const bool keyCPressed = bC();
  const bool centerPressed = S5();
  const bool confirmPressed = keyCPressed || centerPressed;

  if (currentState == STATE_MENU) {
    if (keyAPressed) {
      Serial.println("A Key pressed");
      menuSelection = (menuSelection + 2) % 3;
      displayMenu();
    }
    else if (keyBPressed) {
      Serial.println("B Key pressed");
      menuSelection = (menuSelection + 1) % 3;
      displayMenu();
    }
    else if (confirmPressed) {
      Serial.println("Confirm pressed");
      switch (menuSelection) {
        case 0:
          currentState = STATE_MEASURE;
          measurementPending = false;
          drawMeasureScreen();
          break;
        case 1:
          currentState = STATE_CAL_LOW;
          displayCalLowScreen();
          break;
        case 2:
          currentState = STATE_ABOUT;
          lcd.fillScreen(TFT_BLACK);
          displayAboutScreen();
          break;
      }
    }
  }
  else if (currentState == STATE_MEASURE) {
    if (confirmPressed) {
      currentState = STATE_MENU;
      measurementPending = false;
      displayMenu();
    } else {
      performMeasurement();
    }
  }
  else if (currentState == STATE_CAL_LOW) {
    if (confirmPressed) {
      doCalLow();   // samples brown plate, advances to STATE_CAL_HIGH
    }
  }
  else if (currentState == STATE_CAL_HIGH) {
    if (confirmPressed) {
      doCalHigh();  // samples red plate, fits + persists, returns to menu
    }
  }
  else if (currentState == STATE_ABOUT) {
    if (confirmPressed) {
      currentState = STATE_MENU;
      displayMenu();
    }
  }
}
