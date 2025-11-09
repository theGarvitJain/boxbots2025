#include <LiquidCrystal.h>
#include <SoftwareSerial.h>

// Adjust LCD pins to your wiring:
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);
// ESP TX -> Arduino D8 (RX)
SoftwareSerial esp(8, 9); // RX,TX (TX unused)

bool flashing = false;
uint32_t lastToggle = 0;
bool shown = false;

void setup() {
  Serial.begin(115200);
  esp.begin(9600);
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Ready");
}

void handleMsg(const String& m) {
  if (m == "START") {
    flashing = true; shown = false; lastToggle = 0;
    lcd.clear();
  } else if (m == "TRIGGER") {
    flashing = false;
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Shake detected");
  } else if (m == "CLEAR") {
    flashing = false;
    lcd.clear();
  }
}

void loop() {
  // non-blocking read
  if (esp.available()) {
    String s = esp.readStringUntil('\n'); s.trim();
    if (s.length()) { Serial.println("MSG: " + s); handleMsg(s); }
  }

  // flashing behavior
  if (flashing) {
    uint32_t now = millis();
    if (now - lastToggle >= 300) {
      lastToggle = now;
      lcd.clear();
      if (!shown) { lcd.setCursor(0,0); lcd.print("SHAKE"); }
      shown = !shown;
    }
  }
}
