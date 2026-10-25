#include <WiFi.h>
#include <time.h>
#include "secrets.h"

void connectWifi() {
  Serial.printf("Lacze z siecia: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("NIEUDANE. status=%d\n", WiFi.status());
    Serial.println("Sprawdz: nazwe sieci, haslo, czy siec jest 2,4 GHz (ESP32 nie widzi 5 GHz).");
    return;
  }

  Serial.println("Polaczono.");
  Serial.printf("  IP:   %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
}

void syncTime() {
  // Czas UTC z serwerow NTP. Strefe ustawiamy osobno przez TZ.
  configTime(0, 0, "pl.pool.ntp.org", "pool.ntp.org");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);  // Polska, z czasem letnim
  tzset();

  Serial.print("Pobieram czas");
  struct tm timeinfo;
  unsigned long start = millis();
  while (!getLocalTime(&timeinfo, 1000) && millis() - start < 15000) {
    Serial.print(".");
  }
  Serial.println();

  if (timeinfo.tm_year + 1900 < 2020) {
    Serial.println("NTP NIEUDANE - czas nieustawiony.");
    return;
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.printf("Czas lokalny: %s\n", buf);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== Test WiFi + NTP ===");

  connectWifi();

  if (WiFi.status() == WL_CONNECTED) {
    syncTime();
  }

  Serial.println("---");
}

void loop() {
  delay(10000);
  Serial.printf("WiFi: %s | RSSI: %d dBm\n",
                WiFi.status() == WL_CONNECTED ? "OK" : "ROZLACZONE",
                WiFi.RSSI());
}
