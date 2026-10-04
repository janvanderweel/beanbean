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
  STATE_CAL_LOW,   // roast-color calibration: waiting to sample the DARK reference
  STATE_CAL_HIGH,  // roast-color calibration: waiting to sample the LIGHT reference
  STATE_ABOUT
};

AppState currentState = STATE_MENU;
int menuSelection = 0; // Tracks the current menu selection

bool sensorReady = false; // true once sensor.begin() succeeds; gates all sensor use

int gain = 0;

static const int gains[4] = { AS7265X_GAIN_1X, AS7265X_GAIN_37X, AS7265X_GAIN_16X, AS7265X_GAIN_64X };
static const char *sgain[4] = { "1X  ", "3.7X", "16X ", "64X " };

// ===================== Optical tuning (illumination + gain) ================
// These two knobs set how much light we put on the sample (bulb current) and
// how much the detector amplifies what it reads back (gain). Adjust them here;
// they are applied at startup and whenever the bulbs are enabled.
//
// IMPORTANT: Light level and gain are part of the measurement setup. If you
// change either one, you MUST re-run the two-plate colour calibration, because
// the red/NIR ratio and the stored fit are only valid for a fixed optical
// configuration.
//
// Prefer more LIGHT over more GAIN: raising bulb current improves the true
// signal-to-noise ratio (more photons), while raising gain just amplifies the
// signal and its noise together. Set gain only as high as needed to use the
// ADC range without saturating (readings pinned near the 16-bit max).

// --- Bulb current ---------------------------------------------------------
// Hardware offers only four discrete levels (2-bit field): 12.5 / 25 / 50 /
// 100 mA. There is no 80 mA step. Power-on default is 12.5 mA (the weakest).
//
// *** IR LED LIMIT: the on-board IR LED (SIR19-21C) is rated ~65 mA DC max. ***
// *** Do NOT drive the IR bulb at 100 mA. Keep the IR bulb at 50 mA or lower. ***
// The White and UV bulbs can go to 100 mA, but we keep all three at a single
// shared level for a consistent, easy-to-reason-about illumination.
static const uint8_t BULB_CURRENT_WHITE = AS7265X_LED_CURRENT_LIMIT_50MA; // 12.5 / 25 / 50 / 100
static const uint8_t BULB_CURRENT_IR    = AS7265X_LED_CURRENT_LIMIT_50MA; // MAX 50 mA — IR LED rated ~65 mA
static const uint8_t BULB_CURRENT_UV    = AS7265X_LED_CURRENT_LIMIT_50MA; // 12.5 / 25 / 50 / 100

// --- Gain -----------------------------------------------------------------
// Index into gains[]: 0 = 1X, 1 = 3.7X, 2 = 16X, 3 = 64X.
// With bulbs raised to 50 mA, 16X is a reasonable starting point. If channels
// saturate, drop to 1 (3.7X); if still weak, raise toward 3 (64X).
static const int DEFAULT_GAIN_INDEX = 1; // 16X

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
  Serial.begin(115200);
  for (uint32_t t0 = millis(); !Serial && (millis() - t0) < 2000; ) { }
  Serial.println("AS7265x Spectral Triad");
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(255);
  lcd.setColorDepth(24);
  lcd.fillScreen(0);
  lcd.clear(0);

  displayMenu();
  currentState = STATE_MENU;

  delay(500);
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
    delay(4000);
    displayMenu();
  }

  if (sensorReady)
  {
    setGain(DEFAULT_GAIN_INDEX);
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
  loadCalibration();
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

// ===================== Roast-color calibration (raw 860nm, two-point) =======
// V1: single-channel near-IR reflectance proxy. We read the raw 860nm count
// off two roasted reference samples (a DARK one and a LIGHT one), give each a
// score, and fit a straight line through them:
//
//     score = slope * raw860 + intercept
//
// Darker roasts reflect LESS 860nm light (lower raw count) and score lower.
// No white/dark normalization yet (see design_calibration.md) — the two-point
// fit is partially self-normalizing as long as gain and bulb current are not
// changed between calibrating and measuring.
//
// *** EDIT THESE to set the scores you assign to your two reference roasts. ***
// Behaves roughly like an Agtron scale (low = dark, high = light).
static const float DARK_SCORE  = 80.0f;  // score for the DARK reference roast
static const float LIGHT_SCORE = 106.0f;  // score for the LIGHT reference roast

static const int   CAL_SAMPLES  = 3;

// ===================== Single-shot measurement timing =====================
// A measurement is triggered by pressing C in the measure screen. It runs:
//   1. a warm-up pause with the bulbs ON so the LEDs/sensor stabilise,
//   2. MEASURE_SAMPLES readings that are averaged per channel,
//   3. the averaged result is shown and HELD, and the bulbs are turned OFF.
// Press C again to take a new reading; press B to return to the menu.
static const uint32_t MEASURE_WARMUP_MS = 3000; // warm-up pause before sampling (ms)
static const int      MEASURE_SAMPLES   = 3;    // readings averaged per measurement
// Rough wall-clock cost of one sample (sensor read + the 150 ms settle delay).
// Used only to estimate the on-screen countdown total; biased a little high so
// the countdown never finishes before the real reading is ready.
static const uint32_t MEASURE_SAMPLE_MS = 450;  // estimated time per sample (ms)

// Default fit before any user calibration: identity-ish line so an
// uncalibrated unit still reads *something*. Overwritten on first calibration.
static const float DEFAULT_SLOPE     = 0.0f;
static const float DEFAULT_INTERCEPT = 0.0f;
static const int   DEFAULT_CH_NIR    = 15; // ~860nm near-IR, roast development
// Minimum separation between the two references for a usable fit. The 860nm
// raw counts on this hardware are small (tens, not thousands), so the guard is
// RELATIVE: the LIGHT reference must exceed the DARK one by at least this
// fraction of the DARK reading, with a tiny absolute floor to reject pure
// noise when both readings are near zero.
static const float MIN_RAW_SEPARATION_FRAC = 0.10f; // 10% brighter than DARK
static const float MIN_RAW_SEPARATION_ABS  = 3.0f;  // noise floor (counts)

struct ColourCalibration {
  float slope;      // score = slope * raw860 + intercept
  float intercept;
  int   chNIR;      // channel index used as the roast-development signal (860nm)
  bool  valid;      // true once a good 2-point fit has been stored
};

ColourCalibration colourCal = {
  DEFAULT_SLOPE, DEFAULT_INTERCEPT, DEFAULT_CH_NIR, false
};

static const uint32_t CAL_MAGIC   = 0xB3A11CALu;
static const uint16_t CAL_VERSION = 3;  // bumped: slope/intercept layout

struct PersistedCalibration {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved0[18];  // darkOffsets — kept for future use but not applied in V1
  uint16_t reserved1[18];  // whiteReference — kept for future use but not applied in V1
  float    reserved2[18];  // calibrationFactors — kept for future use but not applied in V1
  ColourCalibration colourCal;
};

FlashStorage(calStore, PersistedCalibration);

float roastColorFromRaw(float raw860)
{
  return colourCal.slope * raw860 + colourCal.intercept;
}

// Roast-color buckets on the DARK_SCORE..LIGHT_SCORE scale (low = dark).
// Thresholds are the upper bound of each bucket; tune to taste.
struct RoastBucket { float maxNumber; const char *label; };
static const RoastBucket roastBuckets[] = {
  {  60.0f, "ULTRA DARK" },
  {  75.0f, "DARK" },
  {  85.0f, "MEDIUM-DARK" },
  { 100.0f, "MEDIUM" },
  { 115.0f, "MEDIUM-LIGHT" },
  { 125.0f, "LIGHT" },
  { 1e9f,   "ULTRA LIGHT" }
};
static const int numRoastBuckets = sizeof(roastBuckets) / sizeof(roastBuckets[0]);

const char *roastLabel(float number)
{
  for (int i = 0; i < numRoastBuckets; ++i) {
    if (number <= roastBuckets[i].maxNumber) return roastBuckets[i].label;
  }
  return roastBuckets[numRoastBuckets - 1].label;
}

static void initMap()
{
  static const int map[18] = {
    0, 1, 2, 3, 4, 5,
    6, 7, 12, 8, 13, 9,
    14, 15, 16, 17, 10, 11
  };
  for (int n = 0; n < 18; ++n)
  {
    for (int i = 0; i < 18; ++i)
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
float rawValues[18];

static const int CHART_BASELINE_Y = 222;
static const int CHART_TOP_Y      = 128;
static const int CHART_LEFT_X     = 6;
static const int CHART_RIGHT_X    = 314;

struct SpectralBand {
  int first;
  int last;
  const char *label;
};

static const SpectralBand bands[] = {
  {  0,  1, "UV" },
  {  2,  3, "BLU" },
  {  4,  6, "GRN" },
  {  7,  8, "YEL" },
  {  9, 11, "RED" },
  { 12, 15, "NIR" },
  { 16, 17, "IR" }
};
static const int numBands = sizeof(bands) / sizeof(bands[0]);

void enableBulbs()
{
  // Set each bulb's drive current, then turn it on. Current is configured by
  // the BULB_CURRENT_* constants above. NOTE: never raise BULB_CURRENT_IR to
  // 100 mA — the on-board IR LED is rated ~65 mA DC max (keep it <= 50 mA).
  sensor.setBulbCurrent(BULB_CURRENT_WHITE, AS7265x_LED_WHITE);
  sensor.enableBulb(AS7265x_LED_WHITE);
  sensor.setBulbCurrent(BULB_CURRENT_IR, AS7265x_LED_IR);
  sensor.enableBulb(AS7265x_LED_IR);
  sensor.setBulbCurrent(BULB_CURRENT_UV, AS7265x_LED_UV);
  sensor.enableBulb(AS7265x_LED_UV);
}

void disableBulbs()
{
  sensor.disableBulb(AS7265x_LED_WHITE);
  sensor.disableBulb(AS7265x_LED_IR);
  sensor.disableBulb(AS7265x_LED_UV);
}

void showResults()
{
  const int panelTop = 20;

  lcd.setTextSize(1);
  lcd.setTextColor(lcd.color888(160, 160, 160));
  lcd.drawString("ROAST", CHART_LEFT_X, panelTop);
  lcd.drawString("ROAST COLOR", CHART_LEFT_X, panelTop + 52);
  lcd.drawFastHLine(0, CHART_TOP_Y - 6, 320, lcd.color888(48, 48, 48));
}

void showScore(float raw860)
{
  const int panelTop = 20;
  const int labelY = panelTop + 12;
  const int numY   = panelTop + 64;

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

  float number = roastColorFromRaw(raw860);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(roastLabel(number), CHART_LEFT_X, labelY);

  char s[16];
  sprintf(s, "%.0f", number);
  lcd.setTextSize(3);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(s, CHART_LEFT_X, numY);
}

// Draws progress feedback in the same panel the roast label/number use, so a
// running measurement visibly replaces the (stale) previous result. The final
// showScore() call later overwrites this with the real reading.
void showProgress(const char *label, const char *big)
{
  const int panelTop = 20;
  const int labelY = panelTop + 12;
  const int numY   = panelTop + 64;

  lcd.fillRect(CHART_LEFT_X, labelY, 320 - CHART_LEFT_X, 20, TFT_BLACK);
  lcd.fillRect(CHART_LEFT_X, numY,   320 - CHART_LEFT_X, 24, TFT_BLACK);

  lcd.setTextSize(2);
  lcd.setTextColor(lcd.color888(160, 160, 160));
  lcd.drawString(label, CHART_LEFT_X, labelY);

  lcd.setTextSize(3);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(big, CHART_LEFT_X, numY);
}

// Redraws the measurement countdown, but only when the whole-seconds value
// actually changes (to avoid flicker). `lastShownSecs` is updated in place so
// the caller can keep calling this cheaply inside tight loops. The remaining
// time is derived from millis() so one call covers warm-up and sampling alike.
void drawCountdown(uint32_t startMs, uint32_t totalEstMs, int &lastShownSecs)
{
  uint32_t elapsed   = millis() - startMs;
  uint32_t remaining = (elapsed < totalEstMs) ? (totalEstMs - elapsed) : 0;
  int secsLeft = (int)((remaining + 999) / 1000); // round up to whole seconds
  if (secsLeft == lastShownSecs) return;
  lastShownSecs = secsLeft;

  char big[8];
  sprintf(big, "%ds", secsLeft);
  showProgress("READING", big);
}

void showValues(){
  float maxv = 0;
  for (int n = 0; n < 18; ++n)
  {
    if (values[n] > maxv)
    {
      maxv = values[n];
    }
  }
  if (maxv < 100.) maxv = 100.;

  const std::uint32_t gray = lcd.color888(64, 64, 64);
  const int chartW  = CHART_RIGHT_X - CHART_LEFT_X;
  const int chartH  = CHART_BASELINE_Y - CHART_TOP_Y;
  const int pitch   = chartW / 18;
  const int barW    = pitch - 2;

  for (int n = 0; n < 18; ++n)
  {
    int colX = CHART_LEFT_X + n * pitch;

    float value = values[n] / maxv;
    int h = (int)(value * chartH);
    h = constrain(h, 0, chartH);

    int gapH = chartH - h;
    if (gapH > 0)
    {
      lcd.fillRect(colX, CHART_TOP_Y, barW, gapH, gray);
    }
    if (h > 0)
    {
      lcd.fillRect(colX, CHART_BASELINE_Y - h, barW, h, colors[n]);
    }
  }
}

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

    int labelW = lcd.textWidth(bands[b].label);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString(bands[b].label, midX - labelW / 2, labelY);

    if (b < numBands - 1)
    {
      lcd.drawFastVLine(endX - 1, CHART_BASELINE_Y + 1, 2, lcd.color888(90, 90, 90));
    }
  }
}

void drawMeasureScreen()
{
  // The first reading auto-starts right after this (see the menu handler), so
  // performMeasurement() takes over the top-right area with "MEASURING" and the
  // countdown immediately. No entry prompt is drawn here to avoid a brief flash.
  lcd.fillScreen(TFT_BLACK);
  showResults();
  showScore(0.0f);
  drawChartLabels();
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE);
}

void showValue(int n, float value)
{
  values[rmap[n]] = value;
}

void displayMenu() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);

  String menuItems[3] = { "Measure", "Calibrate", "About" };

  for (int i = 0; i < 3; i++) {
    if (i == menuSelection) {
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

void displayAboutScreen() {
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("About", 20, 40);
  lcd.setTextSize(2);
  lcd.drawString("Beanbeam V0.1 alpha", 20, 80);
}

// Single-shot measurement triggered by pressing C in the measure screen.
// Blocking by design: warm-up pause -> average MEASURE_SAMPLES readings ->
// hold the result on screen with the bulbs OFF. The buttons are not polled
// during the ~MEASURE_WARMUP_MS warm-up; that is an accepted trade-off for a
// short, deliberate single reading (see MEASURE_WARMUP_MS).
void performMeasurement() {
  if (currentState != STATE_MEASURE) return;

  if (!sensorReady) {
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_RED);
    lcd.drawString("NO SENSOR", 220, 0);
    lcd.setTextColor(TFT_WHITE);
    return;
  }

  // --- Warm-up: bulbs on, let the LEDs and sensor stabilise ---------------
  led = !led;
  digitalWrite(LED_BUILTIN, led);
  if (withLed) enableBulbs();

  lcd.setTextSize(1);
  lcd.setTextColor(TFT_RED);
  lcd.fillRect(220, 0, 100, 10, TFT_BLACK);
  lcd.drawString("MEASURING", 220, 0);
  lcd.setTextColor(TFT_WHITE);

  // One unified countdown across warm-up AND sampling, so it ticks smoothly in
  // whole seconds to zero and the result lands right as it finishes. The total
  // is an estimate (warm-up + per-sample allowance) biased slightly high, so
  // the real reading is always ready at or before the counter hits zero.
  const uint32_t totalEstMs = MEASURE_WARMUP_MS + (uint32_t)MEASURE_SAMPLES * MEASURE_SAMPLE_MS;
  const uint32_t startMs = millis();
  int lastShownSecs = -1;

  // Warm-up wait, redrawing the countdown ~20x/sec (it only repaints on a
  // whole-second change, so this is cheap).
  {
    uint32_t warmEnd = startMs + MEASURE_WARMUP_MS;
    while ((int32_t)(millis() - warmEnd) < 0) {
      drawCountdown(startMs, totalEstMs, lastShownSecs);
      delay(50);
    }
  }

  // --- Sampling: average MEASURE_SAMPLES readings per channel -------------
  float sums[18] = { 0.0f };
  for (int s = 0; s < MEASURE_SAMPLES; ++s) {
    drawCountdown(startMs, totalEstMs, lastShownSecs);
    sampleChannelsOnce();            // fills rawValues[] with the bulbs on
    for (int n = 0; n < 18; ++n) sums[n] += rawValues[n];
    delay(150);
  }

  // Bulbs off now that sampling is done; the result stays frozen on screen.
  if (withLed) disableBulbs();
  led = 0;
  digitalWrite(LED_BUILTIN, led);

  // Store the averaged channels into both the raw-ordered and display-ordered
  // arrays so the chart and the ratio use the same averaged data.
  Serial.print("$L,");
  for (int n = 0; n < 18; ++n) {
    float avg = sums[n] / MEASURE_SAMPLES;
    rawValues[n] = avg;
    showValue(n, avg);             // values[rmap[n]] = avg
    Serial.print((uint16_t)(avg + 0.5f));
    if (n < 17) Serial.print(",");
  }

  showValues();

  {
    // chNIR is a display-order index into values[] (ascending wavelength),
    // 860nm = index 15. values[] is already display-ordered here, so read it
    // directly — do NOT apply rmap (that would land on 760nm).
    float raw860 = values[colourCal.chNIR];
    showScore(raw860);
    Serial.print("$R,");
    Serial.print(raw860, 1);
    Serial.print(",");
    Serial.println(colourCal.valid ? roastColorFromRaw(raw860) : 0.0f, 1);
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

  // Replace the "MEASURING" banner with a prompt to take another reading.
  lcd.setTextSize(1);
  lcd.setTextColor(lcd.color888(160, 160, 160));
  lcd.fillRect(220, 0, 100, 10, TFT_BLACK);
  lcd.drawString("C=again B=menu", 200, 0);
  lcd.setTextColor(TFT_WHITE);
}

void loadCalibration() {
  PersistedCalibration p;
  calStore.read(p);
  if (p.magic == CAL_MAGIC && p.version == CAL_VERSION) {
    colourCal = p.colourCal;
    Serial.println("Calibration loaded from flash (roast-color raw 860nm V1).");
  } else {
    Serial.println("No valid calibration in flash; using defaults.");
  }
}

void saveCalibration() {
  PersistedCalibration p;
  p.magic = CAL_MAGIC;
  p.version = CAL_VERSION;
  p.colourCal = colourCal;
  calStore.write(p);

  PersistedCalibration back;
  calStore.read(back);
  if (back.magic == CAL_MAGIC && back.colourCal.valid == colourCal.valid) {
    Serial.println("Calibration saved to flash (verified).");
  } else {
    Serial.println("WARNING: calibration flash write did not verify.");
  }
}

// Turn the bulbs on and wait MEASURE_WARMUP_MS so the LEDs/sensor stabilise.
// Shared by live measurement and two-plate calibration so both sample the
// plate/sample under identical illumination conditions. Caller is responsible
// for turning the bulbs off afterwards.
void warmUpBulbs() {
  if (withLed) enableBulbs();
  delay(MEASURE_WARMUP_MS);
}

void sampleChannelsOnce() {
  if (withLed) sensor.takeMeasurementsWithBulb();
  else sensor.takeMeasurements();

  uint16_t (AS7265X::*rawGetters[])() = {
    &AS7265X::getA, &AS7265X::getB, &AS7265X::getC,
    &AS7265X::getD, &AS7265X::getE, &AS7265X::getF,
    &AS7265X::getG, &AS7265X::getH, &AS7265X::getI,
    &AS7265X::getJ, &AS7265X::getK, &AS7265X::getL,
    &AS7265X::getR, &AS7265X::getS, &AS7265X::getT,
    &AS7265X::getU, &AS7265X::getV, &AS7265X::getW
  };

  for (int n = 0; n < 18; ++n) {
    uint16_t raw = (sensor.*rawGetters[n])();
    rawValues[n] = (float)raw;
  }
}

// Averages CAL_SAMPLES raw 860nm readings off the reference currently under
// the sensor, warming up exactly like a live measurement so the reference is
// sampled under the same illumination the real readings use.
float samplePlateRaw860() {
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Warming up...", 10, 110);
  warmUpBulbs();

  // Read 860nm identically to the measurement path: sampleChannelsOnce() fills
  // rawValues[] in sensor order, then showValue() copies each channel into the
  // display-ordered values[] (values[rmap[n]] = rawValues[n]). 860nm is
  // display index chNIR (15), so read values[chNIR] directly after mapping.
  float nirSum = 0.0f;
  for (int s = 0; s < CAL_SAMPLES; ++s) {
    sampleChannelsOnce();
    for (int n = 0; n < 18; ++n) showValue(n, rawValues[n]);
    nirSum += values[colourCal.chNIR];
    delay(150);
  }

  if (withLed) disableBulbs();

  return nirSum / CAL_SAMPLES;
}

void displayCalLowScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Place DARK ref", 10, 40);
  lcd.drawString("Press C to sample", 10, 80);
}

void displayCalHighScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Place LIGHT ref", 10, 40);
  lcd.drawString("Press C to sample", 10, 80);
}

float calibrationRaw860Low = 0.0f;  // raw 860nm of the DARK reference

void doCalLow() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.drawString("Sampling DARK...", 10, 40);
  calibrationRaw860Low = samplePlateRaw860();
  Serial.print("Cal DARK raw860="); Serial.println(calibrationRaw860Low, 1);
  currentState = STATE_CAL_HIGH;
  displayCalHighScreen();
}

void doCalHigh() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.drawString("Sampling LIGHT...", 10, 40);
  float raw860High = samplePlateRaw860();
  Serial.print("Cal LIGHT raw860="); Serial.println(raw860High, 1);

  // The LIGHT reference must read a higher raw 860nm count than the DARK one
  // (lighter roast = more near-IR reflectance), and the two must be clearly
  // separated so the fitted line is not near-vertical. Separation is judged
  // relative to the DARK reading (counts are small on this hardware), with a
  // small absolute floor so near-zero noise still fails.
  float minSep = calibrationRaw860Low * MIN_RAW_SEPARATION_FRAC;
  if (minSep < MIN_RAW_SEPARATION_ABS) minSep = MIN_RAW_SEPARATION_ABS;
  if (raw860High - calibrationRaw860Low < minSep) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_RED);
    lcd.drawString("Cal FAILED:", 10, 40);
    lcd.drawString("refs too similar", 10, 70);
    Serial.println("Calibration rejected: raw860 readings too close (or LIGHT < DARK).");
    delay(2500);
    currentState = STATE_MENU;
    displayMenu();
    return;
  }

  // Fit score = slope * raw860 + intercept through the two reference points:
  //   (calibrationRaw860Low, DARK_SCORE) and (raw860High, LIGHT_SCORE)
  colourCal.slope = (LIGHT_SCORE - DARK_SCORE) / (raw860High - calibrationRaw860Low);
  colourCal.intercept = DARK_SCORE - colourCal.slope * calibrationRaw860Low;
  colourCal.valid = true;
  Serial.print("slope="); Serial.print(colourCal.slope, 6);
  Serial.print(" intercept="); Serial.println(colourCal.intercept, 3);

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
          drawMeasureScreen();
          performMeasurement();   // start the first reading right away
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
    // B returns to the menu; C (or the 5-way press) takes a new single-shot
    // reading. Nothing happens otherwise — the last result stays on screen.
    if (keyBPressed) {
      currentState = STATE_MENU;
      displayMenu();
    } else if (confirmPressed) {
      performMeasurement();
    }
  }
  else if (currentState == STATE_CAL_LOW) {
    if (confirmPressed) {
      doCalLow();
    }
  }
  else if (currentState == STATE_CAL_HIGH) {
    if (confirmPressed) {
      doCalHigh();
    }
  }
  else if (currentState == STATE_ABOUT) {
    if (confirmPressed) {
      currentState = STATE_MENU;
      displayMenu();
    }
  }
}
