// Wezel czujnika CO2.
//   SCD40 --I2C--> ESP32-S3 --HTTPS--> InfluxDB Cloud (zapis pomiarow)
//                            --HTTPS--> ntfy.sh        (alert przy wysokim CO2)

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <time.h>
#include <SensirionI2cScd4x.h>
#include "secrets.h"
#include "certs.h"

#define SDA_PIN 8
#define SCL_PIN 9

#define MEASURE_INTERVAL_MS  5000UL   // SCD40 wystawia nowy pomiar co 5 s
#define SEND_INTERVAL_MS    60000UL   // do chmury raz na minute
#define WIFI_RETRY_MS       10000UL

// Progi alertu. Histereza: alarm wlacza sie przy HIGH, gasnie dopiero przy CLEAR.
// Bez tego odstepu powiadomienia sypalyby sie przy kazdym drgnieciu wokol progu.
#define CO2_ALERT_HIGH   1200
#define CO2_ALERT_CLEAR  1000

// Jak czesto przypominac, gdy stan alarmowy trwa.
#define ALERT_REPEAT_MS (30UL * 60UL * 1000UL)

SensirionI2cScd4x scd4x;

// Agregacja: usredniamy wszystkie wazne odczyty z okna wysylki.
static uint32_t sumCo2      = 0;
static float    sumTemp     = 0.0f;
static float    sumHum      = 0.0f;
static uint16_t sampleCount = 0;

static unsigned long lastMeasure = 0;
static unsigned long lastSend    = 0;
static unsigned long lastWifiTry = 0;

// Stan alertu
static bool          alertActive   = false;
static unsigned long lastAlertSent = 0;

static char deviceId[24];

// --- WiFi ---------------------------------------------------------------

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWifiTry < WIFI_RETRY_MS) return;
  lastWifiTry = millis();

  Serial.println("[wifi] rozlaczone, ponawiam...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// --- Czujnik ------------------------------------------------------------

void takeMeasurement() {
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;

  int16_t err = scd4x.readMeasurement(co2, temperature, humidity);
  if (err != 0) {
    Serial.printf("[scd40] readMeasurement blad %d\n", err);
    return;
  }

  if (co2 == 0) return;  // odczyt nieprawidlowy - pomijamy

  sumCo2  += co2;
  sumTemp += temperature;
  sumHum  += humidity;
  sampleCount++;
}

// --- InfluxDB -----------------------------------------------------------

// Zwraca true, gdy Influx potwierdzil zapis (HTTP 204).
bool writeToInflux(const char* lineProtocol) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[influx] brak WiFi, pomijam");
    return false;
  }

  WiFiClientSecure client;
  client.setCACert(LETSENCRYPT_ROOT_CA);
  client.setTimeout(10000);

  String url = String(INFLUX_URL) + "/api/v2/write"
               + "?org="    + INFLUX_ORG
               + "&bucket=" + INFLUX_BUCKET
               + "&precision=s";

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("[influx] http.begin nieudane");
    return false;
  }

  http.addHeader("Authorization", String("Token ") + INFLUX_TOKEN);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");

  int status = http.POST((uint8_t*)lineProtocol, strlen(lineProtocol));

  bool ok = (status == 204);
  if (ok) {
    Serial.println("[influx] zapisano (204)");
  } else if (status > 0) {
    Serial.printf("[influx] HTTP %d: %s\n", status, http.getString().c_str());
  } else {
    Serial.printf("[influx] blad transportu: %s\n",
                  http.errorToString(status).c_str());
  }

  http.end();
  return ok;
}

// --- Powiadomienia ------------------------------------------------------

// Naglowki HTTP musza byc ASCII, dlatego tytuly bez polskich znakow.
// Tresc idzie w ciele zadania - tam UTF-8 jest w porzadku.
bool sendNtfy(const char* title, const char* body,
              const char* priority, const char* tags) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ntfy] brak WiFi, pomijam");
    return false;
  }

  WiFiClientSecure client;
  client.setCACert(LETSENCRYPT_ROOT_CA);
  client.setTimeout(10000);

  HTTPClient http;
  String url = String("https://ntfy.sh/") + NTFY_TOPIC;
  if (!http.begin(client, url)) {
    Serial.println("[ntfy] http.begin nieudane");
    return false;
  }

  http.addHeader("Title", title);
  http.addHeader("Priority", priority);
  http.addHeader("Tags", tags);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");

  int status = http.POST((uint8_t*)body, strlen(body));

  bool ok = (status == 200);
  if (ok) {
    Serial.printf("[ntfy] wyslano: %s\n", title);
  } else if (status > 0) {
    Serial.printf("[ntfy] HTTP %d\n", status);
  } else {
    Serial.printf("[ntfy] blad transportu: %s\n",
                  http.errorToString(status).c_str());
  }

  http.end();
  return ok;
}

void checkAlert(uint16_t co2) {
  char body[160];

  if (co2 >= CO2_ALERT_HIGH) {
    bool firstTime    = !alertActive;
    bool timeToRepeat = (millis() - lastAlertSent >= ALERT_REPEAT_MS);

    if (firstTime || timeToRepeat) {
      snprintf(body, sizeof(body),
               "CO2: %u ppm (prog %u). Przewietrz pomieszczenie.",
               co2, (unsigned)CO2_ALERT_HIGH);
      if (sendNtfy("Wysokie CO2", body, "high", "warning")) {
        lastAlertSent = millis();
      }
    }
    alertActive = true;

  } else if (alertActive && co2 <= CO2_ALERT_CLEAR) {
    alertActive = false;
    snprintf(body, sizeof(body), "CO2: %u ppm. Wrocilo do normy.", co2);
    sendNtfy("CO2 w normie", body, "default", "white_check_mark");
  }
}

// --- Wysylka ------------------------------------------------------------

void buildAndSend() {
  if (sampleCount == 0) {
    Serial.println("[send] brak waznych odczytow w tym okienku, pomijam");
    return;
  }

  time_t now = time(nullptr);
  if (now < 1700000000) {
    Serial.println("[send] brak czasu z NTP, pomijam");
    return;
  }

  uint16_t co2         = sumCo2 / sampleCount;
  float    temperature = sumTemp / sampleCount;
  float    humidity    = sumHum / sampleCount;
  uint16_t n           = sampleCount;

  sumCo2 = 0; sumTemp = 0.0f; sumHum = 0.0f; sampleCount = 0;

  // InfluxDB line protocol:
  //   <measurement>,<tagi> <pola> <timestamp>
  // Sufiks "i" oznacza integer; bez niego Influx zapisze liczbe jako float.
  char lineProtocol[256];
  snprintf(lineProtocol, sizeof(lineProtocol),
           "air,device_id=%s "
           "co2_ppm=%ui,temperature_c=%.1f,humidity_pct=%.1f,samples=%ui,rssi_dbm=%di "
           "%lu",
           deviceId, co2, temperature, humidity, n, WiFi.RSSI(),
           (unsigned long)now);

  Serial.println(lineProtocol);
  writeToInflux(lineProtocol);
  checkAlert(co2);
}

// --- Setup / loop -------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== Wezel CO2 ===");

  // SCD40: I2C maksymalnie 100 kHz.
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  scd4x.stopPeriodicMeasurement();  // blad po zimnym starcie jest normalny
  delay(500);

  int16_t err = scd4x.startPeriodicMeasurement();
  if (err != 0) {
    Serial.printf("[scd40] startPeriodicMeasurement blad %d - zatrzymuje\n", err);
    return;
  }

  WiFi.mode(WIFI_STA);

  // Identyfikator z MAC zapisanego w eFuse. WiFi.macAddress() na tej plytce
  // zwracalo same zera, niezaleznie od kolejnosci wywolan.
  uint64_t efuseMac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "esp32-%04x%08x",
           (unsigned)((efuseMac >> 32) & 0xFFFF),
           (unsigned)(efuseMac & 0xFFFFFFFF));
  Serial.printf("device_id: %s\n", deviceId);

  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  configTime(0, 0, "pl.pool.ntp.org", "pool.ntp.org");

  Serial.printf("Pomiar co %lu s, wysylka co %lu s.\n",
                MEASURE_INTERVAL_MS / 1000, SEND_INTERVAL_MS / 1000);
  Serial.printf("Alert: wlacza sie >= %u ppm, gasnie <= %u ppm.\n",
                (unsigned)CO2_ALERT_HIGH, (unsigned)CO2_ALERT_CLEAR);
  Serial.println("---");
}

void loop() {
  ensureWifi();

  if (millis() - lastMeasure >= MEASURE_INTERVAL_MS) {
    lastMeasure = millis();
    takeMeasurement();
  }

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    buildAndSend();
  }

  delay(50);
}
