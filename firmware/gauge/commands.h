// ---------- Command Interface ----------
// This file handles all user interface commands via Serial and BLE

// Forward declarations - these functions are defined in gauge.ino
void homeToStop();
float readNTCTemp();
float readBatteryVoltage();
bool isBatteryCharging();
int stepsForTemp(float t);
void moveToSteps(int target);
void saveCalibration();
void sortZone(float temps[], int steps[], int count);
void listCal();

// ---------- command processing ----------
void processCommand(String s){
  s.trim();
  if(s.length()==0) return;

  // Reset inactivity timer on any command
  lastActivityTime = millis();

  if(s.equalsIgnoreCase("HOME")){
    bothPrintln("Homing...");
    homeToStop();
    bothPrintln("Homed");
    return;
  }

  // Mode switching
  if(s.startsWith("MODE")){
    String modeArg = s.substring(4);
    modeArg.trim();
    modeArg.toUpperCase();

    if(modeArg == "CAL" || modeArg == "CALIBRATION"){
      calibrationMode = true;
      bothPrintln("Calibration mode: ON (auto updates disabled)");
    }
    else if(modeArg == "WORK" || modeArg == "RUN" || modeArg == "NORMAL"){
      calibrationMode = false;
      bothPrintln("Regular mode: ON (auto updates enabled)");
    }
    else {
      // Show current mode
      if(calibrationMode){
        bothPrintln("Current mode: CALIBRATION (auto updates disabled)");
      } else {
        bothPrintln("Current mode: REGULAR (auto updates enabled)");
      }
    }
    return;
  }

  // Temperature monitoring mode
  if(s.equalsIgnoreCase("TEMPTEST")){
    tempTestMode = !tempTestMode;
    if(tempTestMode){
      bothPrintln("TEMP TEST mode: ON (continuous temp printing)");
    } else {
      bothPrintln("TEMP TEST mode: OFF (silent operation)");
    }
    return;
  }

  // Set home offset (CAL mode only)
  if(s.startsWith("OFFSET ")){
    if(!calibrationMode){
      bothPrintln("OFFSET only available in CAL mode. Use: MODE CAL");
      return;
    }
    int newOffset = s.substring(7).toInt();
    if(newOffset >= 0 && newOffset <= 100){
      homeOffset = newOffset;
      saveCalibration();
      bothPrint("Home offset set to: ");
      bothPrintln(String(homeOffset));
    }else{
      bothPrintln("Offset must be 0-100");
    }
    return;
  }

  // Set max steps (CAL mode only)
  if(s.startsWith("MAXSTEPS ")){
    if(!calibrationMode){
      bothPrintln("MAXSTEPS only available in CAL mode. Use: MODE CAL");
      return;
    }
    int newMaxSteps = s.substring(9).toInt();
    if(newMaxSteps >= 100 && newMaxSteps <= 10000){
      maxSteps = newMaxSteps;
      saveCalibration();
      bothPrint("Max steps set to: ");
      bothPrintln(String(maxSteps));
    }else{
      bothPrintln("Max steps must be 100-10000");
    }
    return;
  }

  // Show current temperature
  if(s.equalsIgnoreCase("TEMP")){
    float t = readNTCTemp();
    bothPrint("Current temp: ");
    bothPrint(t, 1);
    bothPrint(" C");

    // Show zone (dial range: 0-150°C, green zone: 92-97°C)
    if(t < 0) bothPrintln(" (Below dial)");
    else if(t < 90) bothPrintln(" (Below critical range)");
    else if(t < 92) bothPrintln(" (COLD - Red zone)");
    else if(t <= 97) bothPrintln(" (OPTIMAL - Green zone)");
    else if(t <= 101) bothPrintln(" (HOT - Red zone)");
    else if(t <= 150) bothPrintln(" (Above critical range)");
    else bothPrintln(" (Above dial)");

    return;
  }

  // Battery status
  if(s.equalsIgnoreCase("BATTERY") || s.equalsIgnoreCase("BAT")){
    uint32_t vbat = (uint32_t)readBatteryVoltage();
    bool charging = isBatteryCharging();

    bothPrint("Battery: ");
    bothPrint(vbat);
    bothPrint(" mV (");
    bothPrint(vbat / 1000.0, 2);
    bothPrint(" V)");

    if(charging){
      bothPrintln(" - CHARGING");
    } else {
      bothPrintln(" - Not charging");
    }

    // Battery percentage estimate (LiPo 3.7V nominal)
    // 4.2V = 100%, 3.7V = 50%, 3.4V = 10%, 3.0V = 0%
    int percent = 0;
    if(vbat >= 4200) percent = 100;
    else if(vbat >= 3700) percent = 50 + (vbat - 3700) * 50 / 500;
    else if(vbat >= 3400) percent = 10 + (vbat - 3400) * 40 / 300;
    else if(vbat >= 3000) percent = (vbat - 3000) * 10 / 400;

    bothPrint("Estimated: ");
    bothPrint(percent);
    bothPrintln("%");

    return;
  }

  // Debug NTC readings
  if(s.equalsIgnoreCase("DEBUG")){
    int adc = analogRead(NTC_PIN);
    float v = adc * ADC_VREF / ADC_MAX;
    float r = SERIES_R * v / (ADC_VREF - v);

    bothPrint("ADC: "); bothPrintln(String(adc));
    bothPrint("Voltage: "); bothPrint(v, 3); bothPrintln(" V");
    bothPrint("Resistance: "); bothPrint(r, 1); bothPrintln(" ohms");
    bothPrint("Series R: "); bothPrint(SERIES_R, 1); bothPrintln(" ohms");

    float t = readNTCTemp();
    bothPrint("Temperature: "); bothPrint(t, 1); bothPrintln(" C");

    return;
  }

  // Smart zone-based calibration for Rocket gauge (CAL mode only)
  // Actual dial: 0-150°C, but calibration focuses on 90-101°C (critical range)
  if(s.startsWith("CAL ")){
    if(!calibrationMode){
      bothPrintln("CAL only available in CAL mode. Use: MODE CAL");
      return;
    }
    float t = s.substring(4).toFloat();

    if (t < 90.0 || t > 101.0) {
      bothPrintln("Temperature outside calibration range (90-101C). Full dial range is 0-150C.");
      return;
    }
    
    if (t >= 90.0 && t < 92.0) {
      // Cold zone (90-92°C, red left)
      if (calColdCount >= MAX_CAL_COLD) {
        bothPrintln("Cold zone full! Use CLEAR COLD to reset.");
      } else {
        calCold_T[calColdCount] = t;
        calCold_S[calColdCount] = myStepper.currentPosition();
        calColdCount++;
        sortZone(calCold_T, calCold_S, calColdCount);
        bothPrint("COLD zone: ");
        bothPrint(t, 1);
        bothPrint("C at "); bothPrintln(String(myStepper.currentPosition()));
        saveCalibration();
      }
    } 
    else if (t >= 92.0 && t <= 97.0) {
      // GREEN ZONE - most important! (92-97°C)
      if (calGreenCount >= MAX_CAL_GREEN) {
        bothPrintln("Green zone full! Use CLEAR GREEN to reset.");
      } else {
        calGreen_T[calGreenCount] = t;
        calGreen_S[calGreenCount] = myStepper.currentPosition();
        calGreenCount++;
        sortZone(calGreen_T, calGreen_S, calGreenCount);
        bothPrint("GREEN zone: ");
        bothPrint(t, 1);
        bothPrint("C at "); bothPrintln(String(myStepper.currentPosition()));
        saveCalibration();
      }
    } 
    else if (t > 97.0 && t <= 101.0) {
      // Hot zone (97-101°C, red right)
      if (calHotCount >= MAX_CAL_HOT) {
        bothPrintln("Hot zone full! Use CLEAR HOT to reset.");
      } else {
        calHot_T[calHotCount] = t;
        calHot_S[calHotCount] = myStepper.currentPosition();
        calHotCount++;
        sortZone(calHot_T, calHot_S, calHotCount);
        bothPrint("HOT zone: ");
        bothPrint(t, 1);
        bothPrint("C at "); bothPrintln(String(myStepper.currentPosition()));
        saveCalibration();
      }
    }
    return;
  }

  // Clear specific zones or all (CAL mode only)
  if(s.startsWith("CLEAR")){
    if(!calibrationMode){
      bothPrintln("CLEAR only available in CAL mode. Use: MODE CAL");
      return;
    }
    String zone = s.substring(5);
    zone.trim();  // trim() modifies in place, doesn't return a value
    zone.toUpperCase();
    
    if(zone == "" || zone == "ALL"){
      calColdCount = 0;
      calGreenCount = 0;
      calHotCount = 0;
      saveCalibration();
      bothPrintln("All calibration cleared");
    }
    else if(zone == "COLD"){
      calColdCount = 0;
      saveCalibration();
      bothPrintln("Cold zone cleared");
    }
    else if(zone == "GREEN"){
      calGreenCount = 0;
      saveCalibration();
      bothPrintln("Green zone cleared");
    }
    else if(zone == "HOT"){
      calHotCount = 0;
      saveCalibration();
      bothPrintln("Hot zone cleared");
    }
    return;
  }

  // Calibration suggestions for Rocket gauge (CAL mode only)
  if(s.equalsIgnoreCase("SUGGEST")){
    if(!calibrationMode){
      bothPrintln("SUGGEST only available in CAL mode. Use: MODE CAL");
      return;
    }
    bothPrintln("=== ROCKET GAUGE CALIBRATION ===");
    
    if (calColdCount < 2) {
      bothPrintln("COLD ZONE (90-92C) needs:");
      bothPrintln("  - 90.5C and 91.5C");
    } else {
      bothPrintln("COLD ZONE: OK");
    }
    
    bothPrintln("GREEN ZONE (92-97C) needs:");
    if (calGreenCount < 6) {
      float suggested[] = {92, 93, 94, 95, 96, 97};
      for(int i = 0; i < 6; i++) {
        bool have = false;
        for(int j = 0; j < calGreenCount; j++) {
          if(abs(calGreen_T[j] - suggested[i]) < 0.5) {
            have = true;
            break;
          }
        }
        if(!have) {
          bothPrint("  - ");
          bothPrint(suggested[i], 0);
          bothPrintln("C");
        }
      }
    } else {
      bothPrintln("  Well calibrated!");
    }
    
    if (calHotCount < 2) {
      bothPrintln("HOT ZONE (97-101C) needs:");
      bothPrintln("  - 98C and 100C");
    } else {
      bothPrintln("HOT ZONE: OK");
    }
    return;
  }

  // Show calibration status (CAL mode only)
  if(s.equalsIgnoreCase("STATUS")){
    if(!calibrationMode){
      bothPrintln("STATUS only available in CAL mode. Use: MODE CAL");
      return;
    }
    bothPrintln("=== CALIBRATION STATUS ===");
    bothPrint("Cold (90-92C red): ");
    bothPrint(calColdCount);
    bothPrint("/");
    bothPrintln(String(MAX_CAL_COLD));
    
    bothPrint("Green (92-97C): ");
    bothPrint(calGreenCount);
    bothPrint("/");
    bothPrintln(String(MAX_CAL_GREEN));
    
    bothPrint("Hot (97-101C red): ");
    bothPrint(calHotCount);
    bothPrint("/");
    bothPrintln(String(MAX_CAL_HOT));
    
    // Show current temp and zone (dial range: 0-150°C)
    float t = readNTCTemp();
    bothPrint("Current: ");
    bothPrint(t, 1);
    if(t < 0) bothPrint("C - Below dial");
    else if(t < 90) bothPrint("C - Below critical range");
    else if(t < 92) bothPrint("C - COLD zone");
    else if(t <= 97) bothPrint("C - GREEN zone");
    else if(t <= 101) bothPrint("C - HOT zone");
    else if(t <= 150) bothPrint("C - Above critical range");
    else bothPrint("C - Above dial");
    bothPrintln("");
    
    return;
  }

  if(s.startsWith("GOTO ")){
    if(!calibrationMode){
      bothPrintln("GOTO only available in CAL mode. Use: MODE CAL");
      return;
    }
    float t = s.substring(5).toFloat();
    int target = stepsForTemp(t);
    bothPrint("GOTO "); bothPrint(t,1); bothPrint("C -> steps "); bothPrintln(String(target));
    moveToSteps(target);
    bothPrint("Now at "); bothPrintln(String(myStepper.currentPosition()));
    return;
  }

  // Move to absolute step position (CAL mode only)
  if(s.startsWith("STEP ")){
    if(!calibrationMode){
      bothPrintln("STEP only available in CAL mode. Use: MODE CAL");
      return;
    }
    int targetStep = s.substring(5).toInt();
    if(targetStep < 0) targetStep = 0;
    if(targetStep > maxSteps) targetStep = maxSteps;
    bothPrint("Moving to step: "); bothPrintln(String(targetStep));
    moveToSteps(targetStep);
    bothPrint("Now at step: "); bothPrintln(String(myStepper.currentPosition()));
    return;
  }

  // Motor test - sweep back and forth for voltage testing
  if(s.equalsIgnoreCase("SWEEP")){
    if(!calibrationMode){
      bothPrintln("SWEEP only available in CAL mode. Use: MODE CAL");
      return;
    }
    bothPrintln("Motor sweep test - 5 cycles. Press reset to stop.");
    for(int i = 0; i < 5; i++){
      bothPrint("Cycle "); bothPrintln(String(i+1));
      myStepper.runToNewPosition(1500);
      delay(200);
      myStepper.runToNewPosition(0);
      delay(200);
    }
    bothPrintln("Sweep done.");
    return;
  }

  if(s.equalsIgnoreCase("LIST")){
    if(!calibrationMode){
      bothPrintln("LIST only available in CAL mode. Use: MODE CAL");
      return;
    }
    listCal();
    return;
  }

  if(s.equalsIgnoreCase("SAVE")){
    if(!calibrationMode){
      bothPrintln("SAVE only available in CAL mode. Use: MODE CAL");
      return;
    }
    saveCalibration();
    return;
  }

  // Boost converter control (for testing)
  if(s.startsWith("BOOST")){
    String arg = s.substring(5);
    arg.trim();
    arg.toUpperCase();

    if(arg == "" || arg == "ON"){
      digitalWrite(boostPin, HIGH);
      bothPrintln("Boost ON");
    }
    else if(arg == "OFF"){
      digitalWrite(boostPin, LOW);
      bothPrintln("Boost OFF (no safe shutdown - use SHUTDOWN for safe power down)");
    }
    return;
  }

  // Deep sleep (System OFF)
  if(s.equalsIgnoreCase("SLEEP")){
    sleepNow(); // Does not return
    return;
  }

  // Safe shutdown sequence
  if(s.equalsIgnoreCase("SHUTDOWN")){
    bothPrintln("Shutting down safely...");

    // Turn off motor driver pins first
    digitalWrite(enabPin, LOW);
    digitalWrite(dirPin, LOW);
    digitalWrite(stepPin, LOW);
    bothPrintln("Motor driver pins LOW");

    delay(50);  // Brief delay to ensure motor driver is off

    // Turn off boost converter
    digitalWrite(boostPin, LOW);
    bothPrintln("Boost OFF - system shutdown complete");
    bothPrintln("Send BOOST ON to restart");

    return;
  }

  // Manual step movement (relative) - CAL mode only
  int steps = s.toInt();
  if(steps != 0){
    if(!calibrationMode){
      bothPrintln("+/-N steps only available in CAL mode. Use: MODE CAL");
      return;
    }
    int currentPos = myStepper.currentPosition();
    moveToSteps(currentPos + steps);
    bothPrint("Pos: ");
    bothPrintln(String(myStepper.currentPosition()));
  }
  else {
    // Show context-sensitive help
    bothPrintln("=== AVAILABLE COMMANDS ===");
    bothPrintln("Basic:");
    bothPrintln("  HOME | TEMP | TEMPTEST | BATTERY");
    bothPrintln("  BOOST [ON/OFF] | SHUTDOWN | SLEEP");
    bothPrintln("  MODE [CAL/WORK]");
    bothPrintln("");

    if(calibrationMode){
      bothPrintln("Calibration Mode:");
      bothPrintln("  OFFSET <n> | MAXSTEPS <n>");
      bothPrintln("  CAL <tempC> | GOTO <tempC> | STEP <n>");
      bothPrintln("  LIST | STATUS | SUGGEST");
      bothPrintln("  CLEAR [ALL/COLD/GREEN/HOT]");
      bothPrintln("  SAVE | +N/-N steps");
    } else {
      bothPrintln("Use 'MODE CAL' for calibration commands");
    }
  }
}

// Read a full line from USB Serial
void pumpUsbSerial(){
  while(Serial.available()){
    char c = (char)Serial.read();
    if(c=='\r') continue;
    if(c=='\n'){
      processCommand(usbLineBuf);
      usbLineBuf = "";
    }else{
      usbLineBuf += c;
    }
  }
}

// Read a full line from BLE UART
void pumpBleSerial(){
  while(bleuart.available()){
    char c = (char)bleuart.read();
    if(c=='\r') continue;
    if(c=='\n'){
      processCommand(bleLineBuf);
      bleLineBuf = "";
    }else{
      bleLineBuf += c;
    }
  }
}

