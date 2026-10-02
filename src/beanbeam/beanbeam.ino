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

// ===================== Dark/white normalization (optional, not required for V1) ============
// We intentionally keep these arrays but disabled by default. The two-plate calibration
// uses RAW values only; dark/white normalization is a later hardware refinement that needs
// a black enclosure and a white reference tile, not extra roast disks.
static bool useDarkWhiteNormalization = false;

static uint16_t darkOffsets[18] = {
  0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0
};

static uint16_t whiteReference[18] = {
  8000, 8000, 8000, 8000, 8000, 8000,
  8000, 8000, 8000, 8000, 8000, 8000,
  8000, 8000, 8000, 8000, 8000, 8000
};

float calibrationFactors[18];

// ===================== Two-plate (Tonino-lite) colour calibration ==========
static const int   CAL_SAMPLES  = 3;
static const float LOW_TARGET   = 1.5f;
static const float HIGH_TARGET  = 3.7f;
static const float DEFAULT_CAL_0 = 1.011949f;
static const float DEFAULT_CAL_1 = -0.094599f;
static const float SCALE_0 = 0.0f;
static const float SCALE_1 = 0.0f;
static const float SCALE_2 = 102.2727273f;
static const float SCALE_3 = -128.4090909f;
static const int   DEFAULT_CH_RED  = 9;  // ~645nm red
static const int   DEFAULT_CH_NIR  = 15; // ~860nm NIR roast development
static const float MIN_RATIO_SEPARATION = 0.05f;

struct ColourCalibration {
  float cal0;
  float cal1;
  int   chRed;
  int   chNIR;
  bool  valid;
};

ColourCalibration colourCal = {
  DEFAULT_CAL_0, DEFAULT_CAL_1, DEFAULT_CH_RED, DEFAULT_CH_NIR, false
};

static const uint32_t CAL_MAGIC   = 0xB3A11CALu;
static const uint16_t CAL_VERSION = 2;

struct PersistedCalibration {
  uint32_t magic;
  uint16_t version;
  uint16_t darkOffsets[18];
  uint16_t whiteReference[18];
  float    calibrationFactors[18];
  ColourCalibration colourCal;
};

FlashStorage(calStore, PersistedCalibration);

float applyMeasurementPipeline(int ch, uint16_t raw)
{
  if (!useDarkWhiteNormalization) {
    return (float)raw;
  }

  float darkCorrected = (float)raw - (float)darkOffsets[ch];
  float whiteNorm = (float)whiteReference[ch] - (float)darkOffsets[ch];
  if (whiteNorm < 1.0f) whiteNorm = 1.0f;
  float normalized = darkCorrected / whiteNorm;
  return normalized * calibrationFactors[ch];
}

float roastNumberFromRatio(float ratio)
{
  float v = colourCal.cal0 * ratio + colourCal.cal1;
  return SCALE_0 * v * v * v + SCALE_1 * v * v + SCALE_2 * v + SCALE_3;
}

struct RoastBucket { float maxNumber; const char *label; };
static const RoastBucket roastBuckets[] = {
  {  75.0f, "ULTRA DARK" },
  {  95.0f, "DARK" },
  { 110.0f, "MEDIUM-DARK" },
  { 125.0f, "MEDIUM" },
  { 140.0f, "MEDIUM-LIGHT" },
  { 155.0f, "LIGHT" },
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
    calibrationFactors[n] = 1.0;
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
bool measurementPending = false;

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

void showResults()
{
  const int panelTop = 20;

  lcd.setTextSize(1);
  lcd.setTextColor(lcd.color888(160, 160, 160));
  lcd.drawString("ROAST", CHART_LEFT_X, panelTop);
  lcd.drawString("TONINO #", CHART_LEFT_X, panelTop + 52);
  lcd.drawFastHLine(0, CHART_TOP_Y - 6, 320, lcd.color888(48, 48, 48));
}

void showScore(float ratio)
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

  float number = roastNumberFromRatio(ratio);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(roastLabel(number), CHART_LEFT_X, labelY);

  char s[16];
  sprintf(s, "%.0f", number);
  lcd.setTextSize(3);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(s, CHART_LEFT_X, numY);
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

void performMeasurement() {
  if (currentState != STATE_MEASURE) return;

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

    if (withLed) enableBulbs();

    sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT);
    measurementPending = true;
    return;
  }

  if (!sensor.dataAvailable()) return;

  if (withLed) disableBulbs();

  uint16_t (AS7265X::*rawGetters[])() = {
    &AS7265X::getA, &AS7265X::getB, &AS7265X::getC,
    &AS7265X::getD, &AS7265X::getE, &AS7265X::getF,
    &AS7265X::getG, &AS7265X::getH, &AS7265X::getI,
    &AS7265X::getJ, &AS7265X::getK, &AS7265X::getL,
    &AS7265X::getR, &AS7265X::getS, &AS7265X::getT,
    &AS7265X::getU, &AS7265X::getV, &AS7265X::getW
  };

  Serial.print("$L,");
  for (int n = 0; n < 18; n++) {
    uint16_t raw = (sensor.*rawGetters[n])();
    rawValues[n] = (float)raw;
    float v = applyMeasurementPipeline(n, raw);
    showValue(n, v);
    Serial.print(v);
    if (n < 17) Serial.print(",");
  }

  showValues();

  {
    float red = values[rmap[colourCal.chRed]];
    float nir = values[rmap[colourCal.chNIR]];
    float ratio = (nir > 0.0f) ? (red / nir) : 0.0f;
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

void loadCalibration() {
  PersistedCalibration p;
  calStore.read(p);
  if (p.magic == CAL_MAGIC && p.version == CAL_VERSION) {
    for (int i = 0; i < 18; i++) {
      darkOffsets[i] = p.darkOffsets[i];
      whiteReference[i] = p.whiteReference[i];
      calibrationFactors[i] = p.calibrationFactors[i];
    }
    colourCal = p.colourCal;
    Serial.println("Calibration loaded from flash (two-plate raw fit + optional normalization). ");
  } else {
    Serial.println("No valid calibration in flash; using defaults.");
  }
}

void saveCalibration() {
  PersistedCalibration p;
  p.magic = CAL_MAGIC;
  p.version = CAL_VERSION;
  for (int i = 0; i < 18; i++) {
    p.darkOffsets[i] = darkOffsets[i];
    p.whiteReference[i] = whiteReference[i];
    p.calibrationFactors[i] = calibrationFactors[i];
  }
  p.colourCal = colourCal;
  calStore.write(p);

  PersistedCalibration back;
  calStore.read(back);
  if (back.magic == CAL_MAGIC && back.colourCal.valid == colourCal.valid) {
    Serial.println("Calibration saved to flash (verified). ");
  } else {
    Serial.println("WARNING: calibration flash write did not verify.");
  }
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
    float v = applyMeasurementPipeline(n, raw);
    values[rmap[n]] = v;
  }
}

float samplePlateRatio() {
  float rSum = 0.0f;
  float nirSum = 0.0f;

  for (int s = 0; s < CAL_SAMPLES; ++s) {
    sampleChannelsOnce();
    rSum += rawValues[colourCal.chRed];
    nirSum += rawValues[colourCal.chNIR];
    delay(150);
  }

  float rAvg = rSum / CAL_SAMPLES;
  float nirAvg = nirSum / CAL_SAMPLES;
  return (nirAvg > 0.0f) ? (rAvg / nirAvg) : 0.0f;
}

void displayCalLowScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Place LOW (brown)", 10, 40);
  lcd.drawString("Press C to sample", 10, 80);
}

void displayCalHighScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Place HIGH (red)", 10, 40);
  lcd.drawString("Press C to sample", 10, 80);
}

void doCalLow() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.drawString("Sampling LOW...", 10, 40);
  float ratioLowTmp = samplePlateRatio();
  Serial.print("Cal LOW ratio="); Serial.println(ratioLowTmp, 5);
  // This is the raw ratio fit; no dark/white stage is required for V1.
  ratioLow = ratioLowTmp;
  currentState = STATE_CAL_HIGH;
  displayCalHighScreen();
}

void doCalHigh() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.drawString("Sampling HIGH...", 10, 40);
  float ratioHigh = samplePlateRatio();
  Serial.print("Cal HIGH ratio="); Serial.println(ratioHigh, 5);

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
