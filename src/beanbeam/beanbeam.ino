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
  STATE_MENU,               // Main navigation menu
  STATE_MEASURE,            // Result screen: viewing roast card or spectrum chart
  STATE_CAL_LOW,             // roast-color calibration: waiting to sample the DARK reference
  STATE_CAL_HIGH,            // roast-color calibration: waiting to sample the LIGHT reference
  STATE_ABOUT                // Information screen
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
// change either one, you MUST re-run the roast-color calibration, because the
// raw 860nm reading and the stored fit are only valid for a fixed optical
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
    lcd.setFont(&fonts::Font0);
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
// No white/dark normalization yet — the two-point fit is partially
// self-normalizing as long as gain and bulb current are not changed between
// calibrating and measuring.
//
// *** EDIT THESE to set the scores you assign to your two reference roasts. ***
// Agtron scale (low = dark, high = light). These are the known Agtron values
// of the two calibration disks: the DARK disk and the LIGHT disk.
static const float DARK_SCORE  = 31.2f;  // Agtron value of the DARK calibration disk
static const float LIGHT_SCORE = 91.0f;  // Agtron value of the LIGHT calibration disk

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

// Default fit before any user calibration: a flat zero line. It is never shown
// because colourCal.valid is false until a calibration is stored, so an
// uncalibrated unit reports "UNCALIBRATED" rather than a bogus number.
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

static const uint32_t CAL_MAGIC   = 0xB3A11CA1u;
static const uint16_t CAL_VERSION = 4;  // bumped: new Agtron calibration disk values (31.2 / 91.0)

struct PersistedCalibration {
  uint32_t magic;
  uint16_t version;
  ColourCalibration colourCal;   // the roast-color line (slope/intercept + channel)
  // Reserved space for a future dark/white reference normalization pass.
  // Not read or written in V1; bump CAL_VERSION when it starts being used so
  // older blobs are safely ignored.
  float    reserved[18];
};

FlashStorage(calStore, PersistedCalibration);

float roastColorFromRaw(float raw860)
{
  return colourCal.slope * raw860 + colourCal.intercept;
}

// Roast-color buckets on the Agtron scale (low = dark, high = light).
// Thresholds are the upper bound of each bucket:
//   0–30   Very Dark
//   30–40  Dark
//   40–50  Medium-Dark
//   50–60  Medium
//   60–70  Medium-Light
//   70–80  Light
//   80–90  Very Light
//   90–150 Extra Light
struct RoastBucket { float maxNumber; const char *label; uint8_t r, g, b; };
static const RoastBucket roastBuckets[] = {
  {  30.0f, "VERY DARK",    0x3B, 0x24, 0x17 },
  {  40.0f, "DARK",         0x4A, 0x2E, 0x1C },
  {  50.0f, "MEDIUM-DARK",  0x5E, 0x3A, 0x23 },
  {  60.0f, "MEDIUM",       0x77, 0x4A, 0x2D },
  {  70.0f, "MEDIUM-LIGHT", 0x94, 0x61, 0x38 },
  {  80.0f, "LIGHT",        0xB0, 0x7E, 0x4E },
  {  90.0f, "VERY LIGHT",   0xC8, 0x9B, 0x6B },
  { 1e9f,   "EXTRA LIGHT",  0xDF, 0xC1, 0x99 }
};
static const int numRoastBuckets = sizeof(roastBuckets) / sizeof(roastBuckets[0]);

// ---- Roast-color gauge geometry ------------------------------------------
// A horizontal segmented bar painted with one brown per bucket, with a bright
// marker showing where the current reading lands. It lives in the dead space
// to the RIGHT of the big roast number, vertically aligned with it, so it adds
// no vertical pressure on the divider or the spectral chart below.
//
// The gauge maps the Agtron span [20..100]. The lower edge is 20 — just below
// the first zone boundary (30) and below the dark calibration disk (31.2) and
// typical very-dark readings (seen in the low-to-mid 20s) — so VERY DARK gets
// its own visible segment and such readings land ON the bar instead of pinned
// to the far-left edge. A floor of 20 (rather than 0) avoids a lopsided 30-wide near-black dead-zone on the left. Readings below 20 or above 100 still clamp to the bar ends.
static const float GAUGE_MIN      = 20.0f;
static const float GAUGE_MAX      = 100.0f;
static const int   GAUGE_LEFT_X   = 150;   // right half; left half holds number+word
static const int   GAUGE_RIGHT_X  = 314;
static const int   GAUGE_TOP_Y    = 78;    // aligned with the big number band
static const int   GAUGE_HEIGHT   = 20;

const char *roastLabel(float number)
{
  for (int i = 0; i < numRoastBuckets; ++i) {
    if (number <= roastBuckets[i].maxNumber) return roastBuckets[i].label;
  }
  return roastBuckets[numRoastBuckets - 1].label;
}

// Index of the bucket a number falls in (same lookup as roastLabel, but
// returns the index so callers can also read the bucket's colour).
int roastBucketIndex(float number)
{
  for (int i = 0; i < numRoastBuckets; ++i) {
    if (number <= roastBuckets[i].maxNumber) return i;
  }
  return numRoastBuckets - 1;
}

// A legible TEXT tint derived from a bucket's fill brown. The darker browns are
// almost invisible on the black panel, so each channel is lifted to a minimum
// brightness while keeping the warm hue. Used to colour the big roast label so
// the word itself echoes the gauge colour without becoming unreadable.
std::uint32_t roastLabelTint(int bucketIndex)
{
  const RoastBucket &b = roastBuckets[bucketIndex];
  auto lift = [](uint8_t c) -> uint8_t {
    int v = (int)c + 90;          // raise the floor so dark browns stay visible
    if (v > 255) v = 255;
    return (uint8_t)v;
  };
  return lcd.color888(lift(b.r), lift(b.g), lift(b.b));
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

// ---- Measure-screen view mode --------------------------------------------
// The measure screen has two views, toggled with the A button:
//   - Roast view (default): a clean, centred result card (label + number +
//     gauge) with no spectral chart.
//   - Spectrum view: the 18-channel diagnostic bar chart plus a compact roast
//     readout, for inspecting the raw sensor signal.
// The last reading is cached so A can redraw either view without re-measuring.
bool  spectrumView = false;   // false = roast card, true = spectral chart
bool  haveReading  = false;   // a measurement has completed since entering
float lastRaw860   = 0.0f;    // cached 860nm of the last reading, for redraws

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

// ===================== UI typography & layout =============================
// The display is 320x240 (landscape). To stop font sizes "leaking" between
// screens, every screen selects a named font AND resets the integer text
// scale to 1 before drawing. setFont() alone does NOT reset the scale in
// LovyanGFX, so a stray setTextSize(2) left over from another screen would
// multiply the next font — that was the source of the giant, overlapping text.
//
// Use ONE mechanism for size: the named fonts below. Never call setTextSize()
// with anything other than 1.
//
//   UI_FONT_SMALL  (Font0) : ~8 px  — captions, band labels, hints
//   UI_FONT_BODY   (Font2) : ~16 px — menu items, roast label, body text
//   UI_FONT_TITLE  (Font4) : ~26 px — the big Agtron number / headline
#define UI_FONT_SMALL (&fonts::Font0)
#define UI_FONT_BODY  (&fonts::Font2)
#define UI_FONT_TITLE (&fonts::Font4)

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

// Select a font and force the integer scale back to 1. Call this instead of a
// bare setFont() so no previous screen's textSize can bleed through.
static void uiSetFont(const lgfx::IFont *f)
{
  lcd.setTextSize(1);
  lcd.setFont(f);
}

// A simple full-screen two-line message (centred). Used by the calibration and
// status screens so they share one consistent look instead of each hand-rolling
// fonts and coordinates (which is how size/spacing drifted between screens).
// `line2` may be nullptr for a single-line message.
static void drawCentered(const char *s, int cy);  // fwd decl (defined below)
void uiMessageScreen(const char *line1, const char *line2, std::uint32_t color)
{
  lcd.fillScreen(TFT_BLACK);
  uiSetFont(UI_FONT_BODY);
  lcd.setTextColor(color);
  if (line2) {
    drawCentered(line1, 90);
    drawCentered(line2, 130);
  } else {
    drawCentered(line1, 110);
  }
}

void showResults()
{
  const int panelTop = 6;

  uiSetFont(UI_FONT_SMALL);
  lcd.setTextColor(lcd.color888(150, 150, 150));
  lcd.drawString("ROAST COLOR", CHART_LEFT_X, panelTop);
  lcd.drawFastHLine(0, CHART_TOP_Y - 6, SCREEN_W, lcd.color888(48, 48, 48));
}

// Maps an Agtron number to an x pixel inside a gauge bar [leftX,rightX],
// clamped to the ends so extrapolated readings (below GAUGE_MIN / above
// GAUGE_MAX) pin to the edge.
static int gaugeValueToX(float number, int leftX, int rightX)
{
  float t = (number - GAUGE_MIN) / (GAUGE_MAX - GAUGE_MIN);
  t = constrain(t, 0.0f, 1.0f);
  return leftX + (int)(t * (rightX - leftX) + 0.5f);
}

// Draws the segmented roast-color bar at an arbitrary rectangle: one brown per
// bucket sized by its Agtron span, zone-boundary ticks (no numbers) with
// DARK/LIGHT end anchors beneath, and — when `number` is finite — a bright,
// high-contrast triangle marker at the reading. Pass NAN to draw the bar as a
// bare legend (uncalibrated: scale visible, no marker). The bar deliberately
// carries no figures: the Agtron number lives in the big readout, so the bar
// is purely about roast LEVEL.
//   leftX/rightX : horizontal span of the bar
//   topY         : top of the bar
//   height       : bar thickness
void drawRoastGaugeAt(float number, int leftX, int rightX, int topY, int height)
{
  const int barBottom = topY + height;
  const int barW      = rightX - leftX;

  // Clear the whole gauge area (bar + marker strip above + labels below) so a
  // new reading fully erases the previous marker without ghosting.
  lcd.fillRect(leftX - 6, topY - 12, barW + 14, height + 30, TFT_BLACK);

  // --- Segments: each bucket's [lower,upper] span mapped to pixels ----------
  // A bucket's lower bound is the previous bucket's threshold; the first
  // bucket's lower bound is the gauge minimum. The last bucket is open-ended
  // (threshold 1e9), so its upper is clamped to the gauge max.
  for (int i = 0; i < numRoastBuckets; ++i) {
    float lower = (i == 0) ? GAUGE_MIN : roastBuckets[i - 1].maxNumber;
    float upper = roastBuckets[i].maxNumber;
    if (upper > GAUGE_MAX) upper = GAUGE_MAX;
    if (upper <= lower) continue;   // zone lies entirely outside the drawn span

    int x0 = gaugeValueToX(lower, leftX, rightX);
    int x1 = gaugeValueToX(upper, leftX, rightX);
    lcd.fillRect(x0, topY, x1 - x0, height, lcd.color888(roastBuckets[i].r, roastBuckets[i].g, roastBuckets[i].b));
  }
  if (!isnan(number)) {
    int mx = gaugeValueToX(number, leftX, rightX);
    int halfW = 3;
    int triTop = topY - 4;
    int apexY = topY - 8;
    lcd.fillTriangle(mx - halfW, triTop, mx + halfW, triTop, mx, apexY, TFT_WHITE);
    lcd.drawFastVLine(mx, topY, 4, TFT_WHITE);
  }
}

void drawRoastGauge(float number)
{
  drawRoastGaugeAt(number, GAUGE_LEFT_X, GAUGE_RIGHT_X, GAUGE_TOP_Y, GAUGE_HEIGHT);
}

// ---- Roast view (clean centred result card) ------------------------------
// Full-width, centred layout used as the default measure view. No spectral
// chart: just the header, the big roast label tinted with its zone colour,
// the Agtron number, and a wide gauge. `raw860` is the cached reading; pass NAN as
// `raw860` only via haveReading==false handling (the caller decides).
// ---- Roast card layout ----------------------------------------------------
// A clean, vertically balanced result card. Values are chosen for the 320x240
// panel: the big number sits above its roast-level word, with the gauge a
// comfortable margin below. All text is horizontally centred.
static const int CARD_NUM_Y    = 64;   // big Agtron number (Font4, baseline-ish top)
static const int CARD_LABEL_Y  = 118;  // roast-level word (Font2) under the number
static const int CARD_GAUGE_X0 = 30;
static const int CARD_GAUGE_X1 = 290;
static const int CARD_GAUGE_Y  = 180;
static const int CARD_GAUGE_H  = 14;

static void drawCentered(const char *s, int cy)
{
  int w = lcd.textWidth(s);
  lcd.drawString(s, (SCREEN_W - w) / 2, cy);
}

void drawRoastCard(float raw860, bool hasReading)
{
  lcd.fillScreen(TFT_BLACK);

  if (!colourCal.valid) {
    lcd.setTextColor(lcd.color888(170, 170, 170));
    uiSetFont(UI_FONT_BODY);
    drawCentered("UNCALIBRATED", CARD_NUM_Y + 12);
    uiSetFont(UI_FONT_SMALL);
    drawCentered("Calibrate first", CARD_LABEL_Y + 8);
    drawRoastGaugeAt(NAN, CARD_GAUGE_X0, CARD_GAUGE_X1, CARD_GAUGE_Y, CARD_GAUGE_H);
    return;
  }

  if (!hasReading) {
    lcd.setTextColor(lcd.color888(170, 170, 170));
    uiSetFont(UI_FONT_TITLE);
    drawCentered("READY", CARD_NUM_Y);
    uiSetFont(UI_FONT_BODY);
    drawCentered("Press C to read", CARD_LABEL_Y);
    drawRoastGaugeAt(NAN, CARD_GAUGE_X0, CARD_GAUGE_X1, CARD_GAUGE_Y, CARD_GAUGE_H);
    return;
  }

  float number = roastColorFromRaw(raw860);
  int   bi     = roastBucketIndex(number);

  // Big Agtron number in white.
  char s[16];
  sprintf(s, "%.0f", number);
  lcd.setTextColor(TFT_WHITE);
  uiSetFont(UI_FONT_TITLE);
  drawCentered(s, CARD_NUM_Y);

  // Roast-level word beneath, tinted with its zone colour (lifted for legibility).
  lcd.setTextColor(roastLabelTint(bi));
  uiSetFont(UI_FONT_BODY);
  drawCentered(roastLabel(number), CARD_LABEL_Y);

  // Wide centred gauge.
  drawRoastGaugeAt(number, CARD_GAUGE_X0, CARD_GAUGE_X1, CARD_GAUGE_Y, CARD_GAUGE_H);
}

void showScore(float raw860)
{
  const int labelY = 28;   // roast-level word, under the "ROAST COLOR" caption
  const int numY   = 50;   // big Agtron number

  lcd.fillRect(CHART_LEFT_X, labelY, 130, 18, TFT_BLACK);
  lcd.fillRect(CHART_LEFT_X, numY,   130, 32, TFT_BLACK);

  if (!colourCal.valid) {
    uiSetFont(UI_FONT_BODY);
    lcd.setTextColor(lcd.color888(170, 170, 170));
    lcd.drawString("uncal", CHART_LEFT_X, labelY);
    uiSetFont(UI_FONT_TITLE);
    lcd.drawString("---", CHART_LEFT_X, numY);
    drawRoastGauge(NAN);   // show the scale as a legend, no marker
    return;
  }

  float number = roastColorFromRaw(raw860);
  int   bi     = roastBucketIndex(number);

  uiSetFont(UI_FONT_BODY);
  lcd.setTextColor(roastLabelTint(bi));
  lcd.drawString(roastLabel(number), CHART_LEFT_X, labelY);

  char s[16];
  sprintf(s, "%.0f", number);
  uiSetFont(UI_FONT_TITLE);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(s, CHART_LEFT_X, numY);

  drawRoastGauge(number);
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

  uiSetFont(UI_FONT_SMALL);
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
  uiSetFont(UI_FONT_SMALL);
  lcd.setTextColor(TFT_WHITE);
}

// Repaints the measure screen in whichever view is currently selected, using
// the cached reading (lastRaw860 / the values[] bars) — it never re-measures.
// This is the single place that owns the full-screen draw for a result, so the
// A-button toggle and the end of performMeasurement() both route through it.
//   - Roast card (spectrumView == false): clean centred label/number/gauge.
//   - Spectrum view (spectrumView == true): the 18-channel chart + roast panel.
// When no reading has been taken yet, the roast card shows its READY prompt and
// the spectrum view shows an empty chart frame.
void renderMeasureView()
{
  if (!spectrumView) {
    drawRoastCard(lastRaw860, haveReading);
    return;
  }

  // Spectrum view: chart frame + roast readout. Draw the bars only once a
  // reading exists; otherwise just the empty frame and a zeroed score panel.
  drawMeasureScreen();
  if (haveReading) {
    showValues();
    showScore(lastRaw860);
  }
}

void showValue(int n, float value)
{
  values[rmap[n]] = value;
}

void displayMenu() {
  lcd.fillScreen(TFT_BLACK);

  const char *menuItems[3] = { "Measure", "Calibrate", "About" };

  uiSetFont(UI_FONT_BODY);
  const int rowH   = 40;   // row pitch (font is ~16px tall; leaves clear gaps)
  const int rowX   = 14;   // left padding for the item text
  const int firstY = 40;   // top of the first row
  const int barX   = 6;
  const int barW   = SCREEN_W - 12;

  // Title.
  uiSetFont(UI_FONT_SMALL);
  lcd.setTextColor(lcd.color888(150, 150, 150));
  lcd.drawString("BEANBEAM", rowX, 12);

  uiSetFont(UI_FONT_BODY);
  for (int i = 0; i < 3; i++) {
    int rowY = firstY + i * rowH;
    if (i == menuSelection) {
      // Selection bar sized to the row, with the item text vertically centred.
      lcd.fillRoundRect(barX, rowY - 4, barW, rowH - 10, 4, TFT_GREEN);
      lcd.setTextColor(TFT_BLACK);
    } else {
      lcd.setTextColor(TFT_WHITE);
    }
    lcd.drawString(menuItems[i], rowX, rowY);
  }
}

void displayAboutScreen() {
  lcd.fillScreen(TFT_BLACK);

  uiSetFont(UI_FONT_SMALL);
  lcd.setTextColor(lcd.color888(150, 150, 150));
  lcd.drawString("ABOUT", 14, 12);

  uiSetFont(UI_FONT_TITLE);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Beanbeam", 14, 70);

  uiSetFont(UI_FONT_BODY);
  lcd.setTextColor(lcd.color888(170, 170, 170));
  lcd.drawString("V0.1 alpha", 14, 110);

  uiSetFont(UI_FONT_SMALL);
  lcd.setTextColor(lcd.color888(120, 120, 120));
  lcd.drawString("Press C to go back", 14, 210);
}

// A view-agnostic, full-screen progress card shown while a measurement runs.
// Because the measure screen can be in either the roast-card or spectrum view,
// the progress UI gets its own clean full-screen layout (centred "MEASURING" +
// an N-seconds countdown) rather than reusing either view's status band, so it
// never collides with the card's centred text or the chart. The finished
// reading is painted afterwards by renderMeasureView().
void drawMeasuringProgress(int secsLeft)
{
  lcd.fillScreen(TFT_BLACK);
  uiSetFont(UI_FONT_BODY);
  lcd.setTextColor(lcd.color888(160, 160, 160));
  drawCentered("MEASURING", 70);

  char big[8];
  sprintf(big, "%ds", secsLeft);
  uiSetFont(UI_FONT_TITLE);
  lcd.setTextColor(TFT_WHITE);
  drawCentered(big, 110);

  uiSetFont(UI_FONT_SMALL);
  lcd.setTextColor(lcd.color888(120, 120, 120));
  drawCentered("B = cancel", 165);
}

// Recomputes the whole-seconds remaining and repaints the progress card only
// when that value changes (to avoid flicker). Replaces the old spectrum-band
// countdown so warm-up and sampling share one view-agnostic progress screen.
void drawMeasureCountdown(uint32_t startMs, uint32_t totalEstMs, int &lastShownSecs)
{
  uint32_t elapsed   = millis() - startMs;
  uint32_t remaining = (elapsed < totalEstMs) ? (totalEstMs - elapsed) : 0;
  int secsLeft = (int)((remaining + 999) / 1000); // round up to whole seconds
  if (secsLeft == lastShownSecs) return;
  lastShownSecs = secsLeft;
  drawMeasuringProgress(secsLeft);
}

// Single-shot measurement triggered by pressing C in the measure screen.
// Blocking by design: warm-up pause -> average MEASURE_SAMPLES readings ->
// hold the result on screen with the bulbs OFF. During the ~MEASURE_WARMUP_MS
// warm-up the B button is polled so the user can abort back to the menu; the
// short sampling burst that follows runs to completion.
void performMeasurement() {
  if (currentState != STATE_MEASURE) return;

  if (!sensorReady) {
    uiMessageScreen("NO SENSOR", "check wiring", TFT_RED);
    return;
  }

  // --- Warm-up: bulbs on, let the LEDs and sensor stabilise ---------------
  led = !led;
  digitalWrite(LED_BUILTIN, led);
  if (withLed) enableBulbs();

  // One unified countdown across warm-up AND sampling, so it ticks smoothly in
  // whole seconds to zero and the result lands right as it finishes. The total
  // is an estimate (warm-up + per-sample allowance) biased slightly high, so
  // the real reading is always ready at or before the counter hits zero. The
  // progress card (drawn by drawMeasureCountdown) is full-screen and view-
  // agnostic, so it cleanly replaces whatever view was on screen.
  const uint32_t totalEstMs = MEASURE_WARMUP_MS + (uint32_t)MEASURE_SAMPLES * MEASURE_SAMPLE_MS;
  const uint32_t startMs = millis();
  int lastShownSecs = -1;

  // Warm-up wait, redrawing the countdown ~20x/sec (it only repaints on a
  // whole-second change, so this is cheap). Buttons are polled every pass so a
  // B press during this multi-second window aborts cleanly instead of being
  // dropped; the abort turns the bulbs off and returns to the menu.
  {
    uint32_t warmEnd = startMs + MEASURE_WARMUP_MS;
    while ((int32_t)(millis() - warmEnd) < 0) {
      drawMeasureCountdown(startMs, totalEstMs, lastShownSecs);
      if (bB()) {                      // abort measurement, back to menu
        if (withLed) disableBulbs();
        led = 0;
        digitalWrite(LED_BUILTIN, led);
        currentState = STATE_MENU;
        displayMenu();
        return;
      }
      delay(50);
    }
  }

  // --- Sampling: average MEASURE_SAMPLES readings per channel -------------
  float sums[18] = { 0.0f };
  for (int s = 0; s < MEASURE_SAMPLES; ++s) {
    drawMeasureCountdown(startMs, totalEstMs, lastShownSecs);
    sampleChannelsOnce();            // fills rawValues[] with the bulbs on
    for (int n = 0; n < 18; ++n) sums[n] += rawValues[n];
    delay(150);
  }

  // Bulbs off now that sampling is done; the result stays frozen on screen.
  if (withLed) disableBulbs();
  led = 0;
  digitalWrite(LED_BUILTIN, led);

  // Store the averaged channels into both the raw-ordered and display-ordered
  // arrays so the chart and the roast-color reading use the same averaged data.
  Serial.print("$L,");
  for (int n = 0; n < 18; ++n) {
    float avg = sums[n] / MEASURE_SAMPLES;
    rawValues[n] = avg;
    showValue(n, avg);             // values[rmap[n]] = avg
    Serial.print((uint16_t)(avg + 0.5f));
    if (n < 17) Serial.print(",");
  }

  // chNIR is a display-order index into values[] (ascending wavelength),
  // 860nm = index 15. values[] is already display-ordered here, so read it
  // directly — do NOT apply rmap (that would land on 760nm).
  float raw860 = values[colourCal.chNIR];

  // Cache the reading so the A-button view toggle can redraw either view
  // without re-measuring.
  lastRaw860  = raw860;
  haveReading = true;

  Serial.print("$R,");
  Serial.print(raw860, 1);
  Serial.print(",");
  Serial.println(colourCal.valid ? roastColorFromRaw(raw860) : 0.0f, 1);

  Serial.println();

  Serial.print("$T,");
  int oneSensorTemp = sensor.getTemperature();
  Serial.print(oneSensorTemp);
  float threeSensorTemp = sensor.getTemperatureAverage();
  Serial.print(",");
  Serial.print(threeSensorTemp, 2);
  Serial.println();

  // Bulbs are already off; paint the finished reading in whichever view is
  // active (roast card or spectrum chart). renderMeasureView() owns the full
  // screen draw for the chosen view.
  renderMeasureView();

  // In the spectrum view there is a status band at the top for the temperature
  // and the re-measure prompt; the roast card is a clean full-screen layout and
  // owns its own prompt, so only decorate the spectrum view here.
  if (spectrumView) {
    // Top-right: temperature (small) and a one-line control hint beneath it.
    // This corner is clear — the caption/number/gauge occupy the left and
    // centre of the top band.
    uiSetFont(UI_FONT_SMALL);
    char s[20];
    sprintf(s, "%4.1f C", threeSensorTemp);
    lcd.setTextColor(lcd.color888(170, 170, 170));
    lcd.drawString(s, 274, 4);

    lcd.setTextColor(lcd.color888(130, 130, 130));
    lcd.drawString("C read  B menu  A view", 150, 28);
    lcd.setTextColor(TFT_WHITE);
  }
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
// Shared by live measurement and roast-color calibration so both sample the
// reference/sample under identical illumination conditions. Caller is
// responsible for turning the bulbs off afterwards.
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
float sampleReferenceRaw860() {
  uiSetFont(UI_FONT_BODY);
  lcd.setTextColor(lcd.color888(170, 170, 170));
  drawCentered("Warming up...", 150);
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
  uiMessageScreen("Place DARK ref", "Press C to sample", TFT_WHITE);
}

void displayCalHighScreen() {
  uiMessageScreen("Place LIGHT ref", "Press C to sample", TFT_WHITE);
}

float calibrationRaw860Low = 0.0f;  // raw 860nm of the DARK reference

void doCalLow() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  uiMessageScreen("Sampling DARK...", nullptr, TFT_WHITE);
  calibrationRaw860Low = sampleReferenceRaw860();
  Serial.print("Cal DARK raw860="); Serial.println(calibrationRaw860Low, 1);
  currentState = STATE_CAL_HIGH;
  displayCalHighScreen();
}

void doCalHigh() {
  if (!sensorReady) { currentState = STATE_MENU; displayMenu(); return; }
  uiMessageScreen("Sampling LIGHT...", nullptr, TFT_WHITE);
  float raw860High = sampleReferenceRaw860();
  Serial.print("Cal LIGHT raw860="); Serial.println(raw860High, 1);

  // The LIGHT reference must read a higher raw 860nm count than the DARK one
  // (lighter roast = more near-IR reflectance), and the two must be clearly
  // separated so the fitted line is not near-vertical. Separation is judged
  // relative to the DARK reading (counts are small on this hardware), with a
  // small absolute floor so near-zero noise still fails.
  float minSep = calibrationRaw860Low * MIN_RAW_SEPARATION_FRAC;
  if (minSep < MIN_RAW_SEPARATION_ABS) minSep = MIN_RAW_SEPARATION_ABS;
  if (raw860High - calibrationRaw860Low < minSep) {
    uiMessageScreen("Cal FAILED", "refs too similar", TFT_RED);
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

  uiMessageScreen("Cal OK", nullptr, TFT_GREEN);
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
          // Enter the measure screen fresh: default to the clean roast card and
          // clear any cached reading, then render the view before auto-starting
          // the first reading. renderMeasureView() owns the full-screen draw.
          spectrumView = false;
          haveReading  = false;
          renderMeasureView();
          performMeasurement();   // start the first reading right away
          break;
        case 1:
          currentState = STATE_CAL_LOW;
          displayCalLowScreen();
          break;
        case 2:
          currentState = STATE_ABOUT;
          displayAboutScreen();
  }
    }
  }
  else if (currentState == STATE_MEASURE) {
    // A toggles the view (roast card <-> spectrum chart) and redraws from the
    // cached reading without re-measuring. B returns to the menu; C (or the
    // 5-way press) takes a new single-shot reading. Otherwise the last result
    // stays on screen.
    if (keyAPressed) {
      spectrumView = !spectrumView;
      renderMeasureView();
    } else if (keyBPressed) {
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
