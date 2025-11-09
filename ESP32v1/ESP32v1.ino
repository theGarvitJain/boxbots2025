#include <Wire.h>
#include <ESP8266WiFi.h>

const uint8_t MPU = 0x68;
const int SDA_PIN = D2, SCL_PIN = D1;

float lastMag = 0;
bool cycleActive = false;
uint32_t cycleStart = 0;

float readMag() {
  Wire.beginTransmission(MPU); Wire.write(0x3B); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU, (size_t)6, true);
  int16_t ax = (Wire.read()<<8)|Wire.read();
  int16_t ay = (Wire.read()<<8)|Wire.read();
  int16_t az = (Wire.read()<<8)|Wire.read();
  // magnitude in raw counts
  return sqrtf((float)ax*ax + (float)ay*ay + (float)az*az);
}

void setup() {
  Serial.begin(115200);         // debug
  Serial1.begin(9600);          // TX-only on D4 -> Arduino
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(MPU); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission(true);

  delay(200);
}

void loop() {
  uint32_t now = millis();

  if (!cycleActive) {
    // Start a cycle every 10 s
    static uint32_t nextStart = 0;
    if (now >= nextStart) {
      Serial1.println("START");      // tell Arduino to flash "SHAKE"
      Serial.println("START");
      cycleActive = true;
      cycleStart = now;
      // prime baseline
      lastMag = readMag();
      nextStart = now + 10000UL;     // schedule next cycle
    }
    delay(20);
    return;
  }

  // During active cycle: watch for shake
  float mag = readMag();
  float dmag = fabsf(mag - lastMag);
  lastMag = mag;

  // Threshold: ~5000 raw counts (~0.3 g change). Tweak if needed.
  if (dmag > 5000.0f) {
    delay(2000);
    Serial1.println("TRIGGER");      // tell Arduino to stop flashing
    Serial.println("TRIGGER");
    delay(2000);                      // wait 2 s
    Serial1.println("CLEAR");        // clear LCD
    Serial.println("CLEAR");
    cycleActive = false;             // end this cycle; next starts in ~10 s
  }

  delay(15); // ~60–70 Hz sampling
}
