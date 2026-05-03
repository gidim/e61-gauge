#include <Adafruit_TinyUSB.h>   // keeps USB serial alive on nRF52840
#include <math.h>

// NTC pin
const int NTC_PIN = A0;

// NTC parameters (from gauge.ino)
const float SERIES_R      = 100000.0f;     // 100 kΩ to GND
const float NTC_R0        = 100000.0f;     // 100 kΩ at 25 °C
const float NTC_T0_C      = 25.0f;         // nominal temp in °C
const float NTC_BETA      = 3950.0f;       // from datasheet
const float ADC_VREF      = 3.3f;          // ADC reference voltage
const int   ADC_MAX       = 4095;          // 12-bit ADC

// Read NTC temperature
// Voltage divider: NTC to 3V3, series resistor to GND
float readNTCTemp(){
  int adc = analogRead(NTC_PIN);
  float v = adc * ADC_VREF / ADC_MAX;
  float r = SERIES_R * (ADC_VREF / v - 1.0f);  // NTC to 3V3, fixed to GND
  float tK = 1.0f / (logf(r / NTC_R0) / NTC_BETA + 1.0f / (NTC_T0_C + 273.15f));
  float tC = tK - 273.15f;
  return tC;
}

void setup(){
  pinMode(NTC_PIN, INPUT);
  analogReadResolution(12);  // 12-bit ADC for nRF52840
  
  Serial.begin(115200);
  // Wait for serial connection (optional, remove timeout if not needed)
  unsigned long startTime = millis();
  while(!Serial && millis() - startTime < 2000) {
    delay(10);
  }
  
  Serial.println("=== NTC Temperature Test ===");
  Serial.println("Reading NTC on pin A0");
  Serial.println("Parameters:");
  Serial.print("  Series R: "); Serial.print(SERIES_R/1000); Serial.println(" kΩ");
  Serial.print("  NTC R0: "); Serial.print(NTC_R0/1000); Serial.println(" kΩ @ 25°C");
  Serial.print("  Beta: "); Serial.println(NTC_BETA);
  Serial.print("  ADC Vref: "); Serial.print(ADC_VREF); Serial.println("V");
  Serial.println("\nTemperature readings:");
  Serial.println("Time(ms)\tADC\tVoltage(V)\tResistance(Ω)\tTemp(°C)");
}

void loop(){
  int adc = analogRead(NTC_PIN);
  float v = adc * ADC_VREF / ADC_MAX;
  
  // Calculate resistance using same formula as readNTCTemp()
  float r = SERIES_R * (ADC_VREF / v - 1.0f);  // NTC to 3V3, fixed to GND
  
  float temp = readNTCTemp();
  
  Serial.print(millis());
  Serial.print("\t");
  Serial.print(adc);
  Serial.print("\t");
  Serial.print(v, 3);
  Serial.print("\t");
  Serial.print(r, 1);
  Serial.print("\t");
  Serial.print(temp, 2);
  Serial.println("°C");
  
  delay(500);  // Update every 500ms
}

