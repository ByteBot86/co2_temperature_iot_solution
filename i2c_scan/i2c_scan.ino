#include <Wire.h>

#define SDA_PIN 8
#define SCL_PIN 9

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== Skan I2C (SDA=GPIO8, SCL=GPIO9, 100 kHz) ===");

  Wire.begin(SDA_PIN, SCL_PIN, 100000);
}

void loop() {
  byte found = 0;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Znaleziono urzadzenie: 0x%02X", addr);
      if (addr == 0x62) Serial.print("  <-- SCD40/SCD41");
      Serial.println();
      found++;
    }
    delay(2);
  }

  if (found == 0) {
    Serial.println("Nic nie znaleziono. Sprawdz polaczenia i zasilanie.");
  } else {
    Serial.printf("Razem urzadzen: %d\n", found);
  }

  Serial.println("---");
  delay(3000);
}
