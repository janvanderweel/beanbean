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
  STATE_CALIBRATE_READY,
  STATE_ABOUT
};

AppState currentState = STATE_MENU;
int menuSelection = 0; // Tracks the current menu selection

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

  // Sensor initialization
  if (sensor.begin() == false)
  {
    Serial.println("Sensor does not appear to be connected. Please check wiring. Freezing...");
    while (1);
  }
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
  char  s[20];
  const  int x = 70;
  for (int n = 0; n <  18; ++n)
  {
    float value = values[n];
    int y = n * 12 + 12;
    int ww = 310 - x;
    sprintf(s, "%6.1f ", value);
    value =  value / maxv;
    lcd.drawString(freq[n], 0, y);
    lcd.drawString(s, 30, y);
    int w  = value * ww;
    w = constrain(w, 0, ww);
    lcd.fillRect(x + w + 1, y, ww - w - 1, 10, gray);
    lcd.fillRect(x, y, w, 10, colors[n]);
  }
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

// Function to display the calibration screen
void displayCalibrateScreen() {
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Calibration", 20, 40);
  lcd.setTextSize(2);
  lcd.drawString("Calibrating sensor...", 20, 80);
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

// Function to perform calibration
void performCalibration() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Place White Ref", 20, 40);
  lcd.drawString("Press C to Calib", 20, 80);
}

void runCalibrationSequence() {
  lcd.fillScreen(TFT_BLACK);
  lcd.drawString("Calibrating...", 20, 40);
  Serial.println("Starting calibration...");
  
  float sums[18] = {0};
  int samples = 5;
  
  for (int s = 0; s < samples; s++) {
    int barW = (s + 1) * (200 / samples);
    lcd.fillRect(20, 110, barW, 10, TFT_GREEN);
    lcd.drawString("Sample " + String(s+1) + "/" + String(samples), 20, 80);
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
    
    for (int i = 0; i < 18; i++) {
      sums[i] += (sensor.*getters[i])();
    }
    delay(200);
  }
  
  const float targetRef = 100.0;
  for (int i = 0; i < 18; i++) {
    float avg = sums[i] / samples;
    calibrationFactors[i] = (avg > 0) ? (targetRef / avg) : 1.0;
  }
  
  Serial.println("Calibration complete.");
  lcd.fillScreen(TFT_BLACK);
  lcd.drawString("Calibration Done!", 20, 40);
  delay(2000);
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
          lcd.fillScreen(TFT_BLACK);
          break;
        case 1:
          currentState = STATE_CALIBRATE_READY;
          lcd.fillScreen(TFT_BLACK);
          displayCalibrateScreen();
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
  else if (currentState == STATE_CALIBRATE_READY) {
    if (confirmPressed) {
      runCalibrationSequence();
      currentState = STATE_MENU;
      displayMenu();
    }
  }
  else if (currentState == STATE_ABOUT) {
    if (confirmPressed) {
      currentState = STATE_MENU;
      displayMenu();
    }
  }
}
