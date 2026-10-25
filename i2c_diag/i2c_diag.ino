// Test obecnosci rezystorow podciagajacych na module SCD40.
// Wlaczamy slabe podciagniecie DO MASY w ESP32 (~45 kOhm).
// Jesli modul jest podlaczony i zasilony, jego rezystory (zwykle 10 kOhm
// do 3V3) przewaza i linia odczyta sie jako HIGH.
// Jesli nic nie jest podlaczone -> LOW.

#define SDA_PIN 8
#define SCL_PIN 9

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== Test linii I2C ===");

  pinMode(SDA_PIN, INPUT_PULLDOWN);
  pinMode(SCL_PIN, INPUT_PULLDOWN);
  delay(100);
}

void loop() {
  int sda = digitalRead(SDA_PIN);
  int scl = digitalRead(SCL_PIN);

  Serial.printf("SDA (GPIO%d) = %s\n", SDA_PIN, sda ? "HIGH" : "LOW");
  Serial.printf("SCL (GPIO%d) = %s\n", SCL_PIN, scl ? "HIGH" : "LOW");

  if (sda && scl) {
    Serial.println("WYNIK: oba HIGH -> modul podlaczony i zasilony. Problem gdzie indziej.");
  } else if (!sda && !scl) {
    Serial.println("WYNIK: oba LOW -> brak polaczenia LUB brak zasilania modulu.");
  } else {
    Serial.println("WYNIK: jedna linia martwa -> luzny przewod albo zly pin.");
  }

  Serial.println("---");
  delay(3000);
}
