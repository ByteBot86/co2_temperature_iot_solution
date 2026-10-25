#include <Wire.h>
#include <SensirionI2cScd4x.h>

#define SDA_PIN 8
#define SCL_PIN 9

SensirionI2cScd4x scd4x;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== SCD40: odczyt CO2 / temperatura / wilgotnosc ===");

  // SCD40 wymaga I2C maks. 100 kHz - nie podnosic!
  Wire.begin(SDA_PIN, SCL_PIN, 100000);

  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  // Czujnik moze byc w trybie pomiaru po poprzednim resecie.
  // Zatrzymujemy, zeby komendy konfiguracyjne przeszly.
  int16_t err = scd4x.stopPeriodicMeasurement();
  if (err != 0) {
    Serial.printf("stopPeriodicMeasurement -> blad %d (to normalne po zimnym starcie)\n", err);
  }
  delay(500);

  err = scd4x.startPeriodicMeasurement();
  if (err != 0) {
    Serial.printf("startPeriodicMeasurement -> blad %d. Zatrzymuje.\n", err);
    return;
  }

  Serial.println("Pomiar uruchomiony. Pierwszy wynik po ~5 s.");
  Serial.println("Uwaga: wiarygodne wartosci dopiero po kilku minutach rozgrzewki.");
  Serial.println("---");
}

void loop() {
  delay(5000);  // SCD40 wystawia nowy pomiar co 5 s

  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;

  int16_t err = scd4x.readMeasurement(co2, temperature, humidity);

  if (err != 0) {
    Serial.printf("readMeasurement -> blad %d\n", err);
    return;
  }

  if (co2 == 0) {
    Serial.println("Odczyt nieprawidlowy (0 ppm) - czekam na kolejny.");
    return;
  }

  Serial.printf("CO2: %u ppm | T: %.1f C | RH: %.1f %%", co2, temperature, humidity);

  if (co2 < 800)       Serial.println("  [dobrze]");
  else if (co2 < 1200) Serial.println("  [podwyzszone]");
  else if (co2 < 2000) Serial.println("  [duszno - przewietrz]");
  else                 Serial.println("  [zle]");
}
