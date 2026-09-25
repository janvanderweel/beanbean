/*

 * Coffee spectrometer on the Seeed WIO Terminal
 * 
 * Read the 18 channels of spectral light over I2C using the Spectral Triad
 * By: Jan van der Weel using Reinhardt Behm's code as a starting pint
 * SparkFun Electronics
 * Date: October 25th, 2024
 * License: MIT. See license file for more information but you can
 */

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

void showValues()
{
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

// Function to perform measurement
void performMeasurement() {
  if (currentState != STATE_MEASURE) return;
  
  lcd.setTextSize(1);
  
  // Visual indicator that measurement is active
  lcd.setTextColor(TFT_RED);
  lcd.drawString("LIVE", 260, 0);
  lcd.setTextColor(TFT_WHITE);
  
  led = !led;
  digitalWrite(LED_BUILTIN, led);
  
  if (withLed) {
    sensor.takeMeasurementsWithBulb();
  } else {
    sensor.takeMeasurements(); // This is a hard wait while all 18 channels are measured
  }
  
  // Mapping of calibrated value getters to allow loop-based reading
  float (*getters[])() = {
    sensor.getCalibratedA, sensor.getCalibratedB, sensor.getCalibratedC,
    sensor.getCalibratedD, sensor.getCalibratedE, sensor.getCalibratedF,
    sensor.getCalibratedG, sensor.getCalibratedH, sensor.getCalibratedI,
    sensor.getCalibratedJ, sensor.getCalibratedK, sensor.getCalibratedL,
    sensor.getCalibratedR, sensor.getCalibratedS, sensor.getCalibratedT,
    sensor.getCalibratedU, sensor.getCalibratedV, sensor.getCalibratedW
  };

  Serial.print("$L,");
  for (int n = 0; n < 18; n++) {
    float v = getters[n]();
    v *= calibrationFactors[n]; // Apply calibration factor
    showValue(n, v);
    Serial.print(v);
    if (n < 17) Serial.print(",");
  }
  
  showValues();
  Serial.println();
  
  Serial.print("$T,");
  int oneSensorTemp = sensor.getTemperature(); // Returns the temperature of master IC
  Serial.print(oneSensorTemp);
  float threeSensorTemp = sensor.getTemperatureAverage(); // Returns the average temperature of all three ICs
  Serial.print(",");
  Serial.print(threeSensorTemp, 2);
  Serial.println();
  
  {
    char s[20];
    sprintf(s, "%4.1f°C", threeSensorTemp);
    lcd.drawString(s, 250, 0);
  }
  
  // Return to menu handled in loop()
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
    // Progress bar
    int barW = (s + 1) * (200 / samples);
    lcd.fillRect(20, 110, barW, 10, TFT_GREEN);
    
    lcd.drawString("Sample " + String(s+1) + "/" + String(samples), 20, 80);
    if (withLed) sensor.takeMeasurementsWithBulb();
    else sensor.takeMeasurements();
    
    float (*getters[])() = {
      sensor.getCalibratedA, sensor.getCalibratedB, sensor.getCalibratedC,
      sensor.getCalibratedD, sensor.getCalibratedE, sensor.getCalibratedF,
      sensor.getCalibratedG, sensor.getCalibratedH, sensor.getCalibratedI,
      sensor.getCalibratedJ, sensor.getCalibratedK, sensor.getCalibratedL,
      sensor.getCalibratedR, sensor.getCalibratedS, sensor.getCalibratedT,
      sensor.getCalibratedU, sensor.getCalibratedV, sensor.getCalibratedW
    };
    
    for (int i = 0; i < 18; i++) {
      sums[i] += getters[i]();
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
  // Check button presses using digitalRead
  if (bA()) {
    Serial.println("5 Way Up Pressed");
    menuSelection = (menuSelection + 2) % 3; // Move up in the menu
    displayMenu();
    Serial.println("A Key pressed");
    delay(200); // Debouncing delay
  }
  else if (bB()) {
    Serial.println("5 Way Down Pressed");
    menuSelection = (menuSelection + 1) % 3; // Move down in the menu
    displayMenu();
    Serial.println("B Key pressed");
    delay(200); // Debouncing delay
  }
  else if (bC()) {
    Serial.println("5 Way Press Pressed");
    
    // Select the current menu item
    switch (menuSelection) {
      case 0: // Measure
        currentState = STATE_MEASURE;
        lcd.fillScreen(TFT_BLACK);
        break;
      case 1: // Calibrate
        currentState = STATE_CALIBRATE_READY;
        lcd.fillScreen(TFT_BLACK);
        displayCalibrateScreen();
        break;
      case 2: // About
        currentState = STATE_ABOUT;
        lcd.fillScreen(TFT_BLACK);
        displayAboutScreen();
        break;
    }
    delay(200); // Debouncing delay
  }

  // Handle state-specific actions
  if (currentState == STATE_MEASURE) {
    performMeasurement();
  } else if (currentState == STATE_CALIBRATE_READY) {
    if (bC()) {
      runCalibrationSequence();
      currentState = STATE_MENU;
      displayMenu();
    }
  }
  
  // Global return to menu: Press Button C in any sub-state
  if (currentState != STATE_MENU && bC()) {
    currentState = STATE_MENU;
    displayMenu();
  }
}
