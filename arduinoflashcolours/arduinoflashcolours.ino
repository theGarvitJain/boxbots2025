#include <LiquidCrystal.h>

const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

void setup() {
  lcd.begin(16, 2);      // 16 columns, 2 rows
}

void loop() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Red");
  delay(1000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Yellow");
  delay(1000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Blue");
  delay(1000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Green");
  delay(1000);
}
