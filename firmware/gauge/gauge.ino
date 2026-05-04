#include <Adafruit_TinyUSB.h>   // keeps USB serial alive on nRF52840
#include <math.h>
#include <bluefruit.h>          // Adafruit nRF52 BLE
#include <Adafruit_LittleFS.h>  // For flash storage
#include <InternalFileSystem.h> // For flash storage
#include <AccelStepper.h>       // For VID6606 stepper driver

using namespace Adafruit_LittleFS_Namespace;

// ---------------- hardware ----------------
// Boost converter control
const int boostPin = 1; // Controls 2N2222A transistor for 5V boost

// VID6606 stepper driver pins
const int enabPin = 2;  // RESET/ENABLE pin
const int dirPin = 3;   // DIR pin
const int stepPin = 4;  // STEP pin
const int NTC_PIN = A0; // thermistor input

// Power button
const int buttonPin = 7; // Momentary button (other leg to GND)

// Create stepper instance (step/direction driver)
AccelStepper myStepper(AccelStepper::DRIVER, stepPin, dirPin);

// ---------------- NTC params (match your working code) ----------------
const float SERIES_R      = 10000.0f;      // 10 kΩ series resistor
const float NTC_R0        = 100000.0f;     // 100 kΩ at 25 °C
const float NTC_T0_C      = 25.0f;         // nominal temp in °C
const float NTC_BETA      = 3950.0f;       // from datasheet
const float ADC_VREF      = 3.3f;          // ADC reference voltage
const int   ADC_MAX       = 4095;          // 12-bit ADC

// ---------------- stepper settings ----------------
const int STEPPER_MAX_SPEED = 2000;      // steps per second
const int STEPPER_ACCELERATION = 2400;   // steps per second^2

// ---------------- state ----------------
bool calibrationMode = false;  // false = regular mode, true = calibration mode
bool tempTestMode = false;     // false = silent, true = continuous temp printing
// Power management
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT_MS = 600000; // 10 minutes

// Button debounce
unsigned long lastButtonCheck = 0;
bool lastButtonState = HIGH;   // Pull-up: HIGH = not pressed
bool buttonPressed = false;

// ---------------- ZONE-BASED CALIBRATION FOR ROCKET GAUGE ----------------
// Actual dial: 0-150°C with green zone at 92-97°C (non-linear scale)
// Dial positions: 0, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 150
const int MAX_CAL_COLD = 3;   // 90-92°C (red zone left, compressed on dial)
const int MAX_CAL_GREEN = 15; // 92-97°C (GREEN ZONE - expanded on dial, high precision)
const int MAX_CAL_HOT = 3;    // 97-101°C (red zone right, compressed on dial)

// Separate calibration arrays for each zone
float calCold_T[MAX_CAL_COLD], calGreen_T[MAX_CAL_GREEN], calHot_T[MAX_CAL_HOT];
int calCold_S[MAX_CAL_COLD], calGreen_S[MAX_CAL_GREEN], calHot_S[MAX_CAL_HOT];
int calColdCount = 0, calGreenCount = 0, calHotCount = 0;

// ---------------- Non-linear dial mapping ----------------
// Lookup table for known dial positions (will be calibrated)
// Dial shows: 0, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 150
// Initial estimates - these will be replaced by calibration
const int DIAL_POINTS = 14;
const float dialTemps[DIAL_POINTS] = {0, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 150};
// Initial step estimates (will be calibrated):
// 0-90: compressed (0-375 steps)
// 90-101: expanded (375-2625 steps) - critical range
// 101-150: compressed (2625-3000 steps)
int dialSteps[DIAL_POINTS] = {0, 375, 500, 750, 1125, 1500, 1875, 2250, 2625, 2700, 2775, 2850, 2925, 3000};

// ---------------- home offset ----------------
int homeOffset = 20;  // Default 20 steps forward after hitting stop

// ---------------- max steps ----------------
int maxSteps = 3000;  // Maximum steps for full dial range (configurable via MAXSTEPS command)

// ---------------- Flash Storage ----------------
File calFile(InternalFS);
const char calFilename[] = "/calibration_rocket.dat";

// ---------------- BLE UART ----------------
BLEUart bleuart;                 // Nordic UART Service
String usbLineBuf, bleLineBuf;   // line buffers for command parsing

// ---------------- Battery Monitor ----------------
// Note: Not using the Xiao library due to incorrect ADC constants
// Implementing our own battery reading based on official examples

#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
#define VBAT_DIVIDER_COMP (2.96F)          // XIAO voltage divider compensation (1510/510)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

#define BAT_CHARGE_STATE 23 // LOW for charging, HIGH not charging

// Helpers to print to both USB Serial and BLE UART
void bothPrint(const String& s){
  Serial.print(s);
  if(Bluefruit.connected()){
    bleuart.print(s);
  }
}
void bothPrint(const char* s){
  Serial.print(s);
  if(Bluefruit.connected()){
    bleuart.print(s);
  }
}
void bothPrint(float v, int digits){
  Serial.print(v, digits);
  if(Bluefruit.connected()){
    bleuart.print(v, digits);
  }
}
void bothPrint(int v){
  Serial.print(v);
  if(Bluefruit.connected()){
    bleuart.print(v);
  }
}
void bothPrintln(const String& s=""){
  Serial.println(s);
  if(Bluefruit.connected()){
    bleuart.println(s);
  }
}
void bothPrintf(const char* fmt, ...){
  char buf[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  if(Bluefruit.connected()){
    bleuart.print(buf);
  }
}

// ---------- motor drive ----------
void moveToSteps(int target) {
  // Move to target position with AccelStepper (non-blocking)
  myStepper.moveTo(target);
}

// ---------- homing ----------
void homeToStop(){
  // Move CCW far enough to definitely hit the mechanical stop
  // Motor will stall against stop, which is fine - this ensures zero position
  myStepper.runToNewPosition(-4000);

  // Set this as zero, then move forward by the calibrated offset
  myStepper.setCurrentPosition(0);
  myStepper.runToNewPosition(homeOffset);
  myStepper.setCurrentPosition(0);  // This is now our zero position

  bothPrint("Homed with offset: ");
  bothPrintln(String(homeOffset));
}

// ---------- button handling ----------
void checkButton() {
  unsigned long now = millis();
  if (now - lastButtonCheck < 50) return; // 50ms debounce interval
  lastButtonCheck = now;

  bool currentState = digitalRead(buttonPin);
  if (lastButtonState == HIGH && currentState == LOW) {
    buttonPressed = true; // Falling edge: button pressed
  }
  lastButtonState = currentState;
}

// ---------- power management ----------
void sleepNow(){
  bothPrintln("Going to sleep...");
  delay(100); // Allow message to send

  // De-energize motor: disable driver and release all pins to stop coil current
  myStepper.disableOutputs();
  digitalWrite(enabPin, LOW);
  digitalWrite(dirPin, LOW);
  digitalWrite(stepPin, LOW);
  delay(50);

  // Turn off boost
  digitalWrite(boostPin, LOW);

  // Disable battery voltage divider
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH);

  // Disconnect BLE if connected
  if(Bluefruit.connected()){
    Bluefruit.disconnect(0);
    delay(200);
  }

  // Stop BLE advertising
  Bluefruit.Advertising.stop();
  delay(200);

  // Turn off Serial
  Serial.end();

  // Configure button pin for wake from System OFF
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[buttonPin],
                           NRF_GPIO_PIN_PULLUP,
                           NRF_GPIO_PIN_SENSE_LOW);

  // Shut down SoftDevice to release hardware ownership, then
  // write directly to the power register - guaranteed, cannot fail
  sd_softdevice_disable();
  NRF_POWER->SYSTEMOFF = 1;

  // Unreachable - chip is off
  while(1);
}

// ---------- Flash storage functions ----------
void saveCalibration(){
  if(calFile.open(calFilename, FILE_O_WRITE)){
    // Save home offset
    calFile.write((uint8_t*)&homeOffset, sizeof(homeOffset));

    // Save max steps
    calFile.write((uint8_t*)&maxSteps, sizeof(maxSteps));
    
    // Save cold zone (85-92°C)
    calFile.write((uint8_t*)&calColdCount, sizeof(calColdCount));
    if(calColdCount > 0) {
      calFile.write((uint8_t*)calCold_T, sizeof(float) * calColdCount);
      calFile.write((uint8_t*)calCold_S, sizeof(int) * calColdCount);
    }
    
    // Save green zone (92-98°C)
    calFile.write((uint8_t*)&calGreenCount, sizeof(calGreenCount));
    if(calGreenCount > 0) {
      calFile.write((uint8_t*)calGreen_T, sizeof(float) * calGreenCount);
      calFile.write((uint8_t*)calGreen_S, sizeof(int) * calGreenCount);
    }
    
    // Save hot zone (98-105°C)
    calFile.write((uint8_t*)&calHotCount, sizeof(calHotCount));
    if(calHotCount > 0) {
      calFile.write((uint8_t*)calHot_T, sizeof(float) * calHotCount);
      calFile.write((uint8_t*)calHot_S, sizeof(int) * calHotCount);
    }
    
    calFile.close();
    bothPrintln("Calibration saved to flash");
  }else{
    bothPrintln("Failed to save calibration");
  }
}

void loadCalibration(){
  if(calFile.open(calFilename, FILE_O_READ)){
    // Load home offset
    int bytesRead = calFile.read((uint8_t*)&homeOffset, sizeof(homeOffset));
    if(bytesRead == sizeof(homeOffset)){
      bothPrint("Loaded home offset: ");
      bothPrintln(String(homeOffset));
    }

    // Load max steps
    bytesRead = calFile.read((uint8_t*)&maxSteps, sizeof(maxSteps));
    if(bytesRead == sizeof(maxSteps)){
      bothPrint("Loaded max steps: ");
      bothPrintln(String(maxSteps));
    }
    
    // Load cold zone
    calFile.read((uint8_t*)&calColdCount, sizeof(calColdCount));
    if(calColdCount > 0 && calColdCount <= MAX_CAL_COLD){
      calFile.read((uint8_t*)calCold_T, sizeof(float) * calColdCount);
      calFile.read((uint8_t*)calCold_S, sizeof(int) * calColdCount);
    }
    
    // Load green zone
    calFile.read((uint8_t*)&calGreenCount, sizeof(calGreenCount));
    if(calGreenCount > 0 && calGreenCount <= MAX_CAL_GREEN){
      calFile.read((uint8_t*)calGreen_T, sizeof(float) * calGreenCount);
      calFile.read((uint8_t*)calGreen_S, sizeof(int) * calGreenCount);
    }
    
    // Load hot zone
    calFile.read((uint8_t*)&calHotCount, sizeof(calHotCount));
    if(calHotCount > 0 && calHotCount <= MAX_CAL_HOT){
      calFile.read((uint8_t*)calHot_T, sizeof(float) * calHotCount);
      calFile.read((uint8_t*)calHot_S, sizeof(int) * calHotCount);
    }
    
    calFile.close();
    bothPrintln("Calibration loaded from flash");

  }else{
    bothPrintln("No saved calibration found, using defaults");
    homeOffset = 20;
    maxSteps = 3000;
  }
}

// ---------- calibration helpers ----------
void sortZone(float temps[], int steps[], int count){
  for(int i=1;i<count;i++){
    float t=temps[i]; int s=steps[i]; int j=i-1;
    while(j>=0 && temps[j]>t){ 
      temps[j+1]=temps[j]; steps[j+1]=steps[j]; j--; 
    }
    temps[j+1]=t; steps[j+1]=s;
  }
}

// Helper function for interpolation
int interpolatePoints(float t, float temps[], int steps[], int count) {
  if (count == 0) return 0;
  if (count == 1) return steps[0];
  
  // Check if before first point
  if (t <= temps[0]) return steps[0];
  // Check if after last point
  if (t >= temps[count-1]) return steps[count-1];
  
  // Find bounding points
  for(int i = 0; i < count - 1; i++) {
    if (temps[i] <= t && temps[i+1] >= t) {
      float ratio = (t - temps[i]) / (temps[i+1] - temps[i]);
      return steps[i] + int(ratio * (steps[i+1] - steps[i]) + 0.5);
    }
  }
  
  return steps[count - 1];
}

// Non-linear temperature to step mapping using dial lookup table
// Uses calibration data if available, otherwise interpolates from dial positions
int stepsForTemp(float t) {
  // Clamp to dial range
  if (t < 0.0) return 0;
  if (t > 150.0) return maxSteps;
  
  // Check for calibration data in critical zones (92-97°C green zone)
  if (t >= 92.0 && t <= 97.0) {
    // GREEN ZONE - use calibration if available
    if (calGreenCount > 0) {
      return interpolatePoints(t, calGreen_T, calGreen_S, calGreenCount);
    }
  }
  else if (t >= 90.0 && t < 92.0) {
    // Cold zone (90-92°C) - use calibration if available
    if (calColdCount >= 2) {
      return interpolatePoints(t, calCold_T, calCold_S, calColdCount);
    }
  }
  else if (t > 97.0 && t <= 101.0) {
    // Hot zone (97-101°C) - use calibration if available
    if (calHotCount >= 2) {
      return interpolatePoints(t, calHot_T, calHot_S, calHotCount);
    }
  }
  
  // No calibration data or outside calibrated zones - use dial lookup table
  // Find the two dial points that bracket this temperature
  for (int i = 0; i < DIAL_POINTS - 1; i++) {
    if (t >= dialTemps[i] && t <= dialTemps[i + 1]) {
      // Linear interpolation between dial points
      float ratio = (t - dialTemps[i]) / (dialTemps[i + 1] - dialTemps[i]);
      int steps = dialSteps[i] + int(ratio * (dialSteps[i + 1] - dialSteps[i]) + 0.5);
      return steps;
    }
  }

  // Fallback (shouldn't reach here due to clamping above)
  return maxSteps;
}

void listCal(){
  bothPrintln("=== ROCKET GAUGE CALIBRATION ===");
  
  bothPrint("COLD ZONE (90-92C red): ");
  bothPrint(calColdCount); bothPrintln(" points");
  for(int i=0;i<calColdCount;i++){
    bothPrint("  "); bothPrint(calCold_T[i],1);
    bothPrint("C -> "); bothPrintln(String(calCold_S[i]));
  }
  
  bothPrint("GREEN ZONE (92-97C): ");
  bothPrint(calGreenCount); bothPrintln(" points");
  for(int i=0;i<calGreenCount;i++){
    bothPrint("  "); bothPrint(calGreen_T[i],1);
    bothPrint("C -> "); bothPrintln(String(calGreen_S[i]));
  }
  
  bothPrint("HOT ZONE (97-101C red): ");
  bothPrint(calHotCount); bothPrintln(" points");
  for(int i=0;i<calHotCount;i++){
    bothPrint("  "); bothPrint(calHot_T[i],1);
    bothPrint("C -> "); bothPrintln(String(calHot_S[i]));
  }
  
  bothPrint("HOME OFFSET: ");
  bothPrintln(String(homeOffset));

  bothPrint("MAX STEPS: ");
  bothPrintln(String(maxSteps));
}

// ---------- Battery read ----------
float readBatteryVoltage() {
  // Enable battery voltage reading
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);

  // Set the analog reference to 3.0V (default = 3.6V)
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);

  // Let the ADC settle
  delay(1);

  // Read battery voltage
  float raw = analogRead(PIN_VBAT);

  // Disable battery voltage reading
  digitalWrite(VBAT_ENABLE, HIGH);

  // Restore ADC settings
  analogReference(AR_DEFAULT);
  analogReadResolution(12);

  // Convert to mV with voltage divider compensation
  return raw * REAL_VBAT_MV_PER_LSB;
}

bool isBatteryCharging() {
  pinMode(BAT_CHARGE_STATE, INPUT);
  return digitalRead(BAT_CHARGE_STATE) == LOW;
}

// ---------- NTC read ----------
// Voltage divider: Series resistor to 3V3, NTC to GND
float readNTCTemp(){
  int adc = analogRead(NTC_PIN);

  // Safety check: if ADC reading is too low, return a safe default
  if(adc < 10) {
    return 25.0f;  // Return room temperature as safe default
  }

  float v = adc * ADC_VREF / ADC_MAX;

  // Safety check: if voltage is near VREF, return safe default
  if(v >= (ADC_VREF - 0.01f)) {
    return 25.0f;  // Return room temperature as safe default
  }

  float r = SERIES_R * v / (ADC_VREF - v);  // Series resistor to 3V3, NTC to GND
  
  // Safety check: if resistance is invalid, return safe default
  if(r <= 0.0f || r > 10000000.0f || isnan(r) || isinf(r)) {
    return 25.0f;  // Return room temperature as safe default
  }
  
  float tK = 1.0f / (logf(r / NTC_R0) / NTC_BETA + 1.0f / (NTC_T0_C + 273.15f));
  float tC = tK - 273.15f;
  
  // Safety check: if temperature is invalid, return safe default
  if(tC < -50.0f || tC > 200.0f || isnan(tC) || isinf(tC)) {
    return 25.0f;  // Return room temperature as safe default
  }
  
  return tC;
}

// ---------- BLE setup helpers ----------
void startAdv(){
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(0);
  Bluefruit.Advertising.start(0);
}

// Include command interface after all function definitions
#include "commands.h"

// ---------- setup ----------
void setup(){
  // Button pin first - needed for wake-hold detection
  pinMode(buttonPin, INPUT_PULLUP);

  // Wait for button release (prevents immediate re-sleep if held from wake)
  while (digitalRead(buttonPin) == LOW) {
    delay(10);
  }
  delay(50); // Extra debounce after release

  // Initialize boost converter control
  pinMode(boostPin, OUTPUT);
  digitalWrite(boostPin, HIGH);
  delay(100); // Wait for boost to stabilize

  // Initialize VID6606 stepper driver pins
  pinMode(enabPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(stepPin, OUTPUT);

  // Enable the VID6606 driver
  digitalWrite(enabPin, LOW);
  delay(100);
  digitalWrite(enabPin, HIGH);

  pinMode(NTC_PIN, INPUT);
  analogReadResolution(12);

  // Configure stepper speed and acceleration
  myStepper.setMaxSpeed(STEPPER_MAX_SPEED);
  myStepper.setAcceleration(STEPPER_ACCELERATION);

  // USB Serial with timeout for battery
  Serial.begin(115200);
  unsigned long startTime = millis();
  while(!Serial && millis() - startTime < 1000) {
    delay(10);
  }

  // Initialize flash storage
  InternalFS.begin();

  // BLE setup
  Bluefruit.autoConnLed(true);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("Rocket Gauge");
  bleuart.begin();
  startAdv();

  // Load calibration
  loadCalibration();

  // Check reset reason for debug logging (must use SoftDevice API, not direct register)
  uint32_t resetReason = 0;
  sd_power_reset_reason_get(&resetReason);
  sd_power_reset_reason_clr(resetReason);

  bothPrintln("=== ROCKET ESPRESSO GAUGE ===");
  if (resetReason & 0x00010000) {
    bothPrintln("Woke from deep sleep (GPIO)");
  } else if (resetReason == 0) {
    bothPrintln("Power-on reset");
  } else {
    bothPrint("Reset reason: 0x");
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lX", (unsigned long)resetReason);
    bothPrintln(buf);
  }

  bothPrintln("Homing...");
  homeToStop();

  lastActivityTime = millis();
  lastButtonCheck = millis();  // Prevent immediate button trigger
  lastButtonState = digitalRead(buttonPin); // Sync to actual pin state
  bothPrintln("Ready! Type HELP for commands");
}

// ---------- loop ----------
void loop(){
  // IMPORTANT: Must call run() continuously for AccelStepper to work
  myStepper.run();

  // Check button - sleep on press (skip first 2s after boot)
  if (millis() > 2000) {
    checkButton();
  }
  if (buttonPressed) {
    buttonPressed = false;
    sleepNow(); // Does not return
  }

  // Auto-sleep after inactivity
  if (millis() - lastActivityTime >= SLEEP_TIMEOUT_MS) {
    bothPrintln("Auto-sleep: 10 min inactivity");
    sleepNow(); // Does not return
  }

  // Read commands
  pumpUsbSerial();
  pumpBleSerial();

  // Auto temperature control only in regular mode (already awake if we got here)
  if(!calibrationMode){
    static unsigned long lastUpdate=0;
    unsigned long now=millis();
    if(now - lastUpdate >= 167){
      lastUpdate=now;

      float t = readNTCTemp();
      int target = stepsForTemp(t);
      int currentPos = myStepper.currentPosition();

      // Safety check: ensure target is within valid range
      if(target < 0) target = 0;
      if(target > maxSteps) target = maxSteps;

      // Only move if difference is significant and target is reasonable
      if(abs(target - currentPos) > 2 && target >= 0 && target <= maxSteps){
        moveToSteps(target);
      }

      // Print temp continuously if in TEMP TEST mode
      if(tempTestMode){
        bothPrint("Temp: ");
        bothPrint(t,1);
        bothPrint("C -> ");
        bothPrint(target);
        bothPrint(" steps");

        // Show zone (dial range: 0-150°C, green zone: 92-97°C)
        if(t < 0) bothPrint(" (Below dial)");
        else if(t < 90) bothPrint(" (Below critical)");
        else if(t < 92) bothPrint(" (COLD)");
        else if(t <= 97) bothPrint(" (GREEN)");
        else if(t <= 101) bothPrint(" (HOT)");
        else if(t <= 150) bothPrint(" (Above critical)");
        else bothPrint(" (Above dial)");

        bothPrintln("");
      }
    }
  }
}
