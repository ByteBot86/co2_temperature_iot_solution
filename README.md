# Czujnik CO2 — ESP32-S3 + SCD40

Węzeł IoT mierzący CO2, temperaturę i wilgotność. Wysyła średnie z minuty do
InfluxDB Cloud, a przy wysokim CO2 wysyła alert push przez ntfy.sh na urzadzenie mobilne.

```
SCD40 --I2C--> ESP32-S3 --HTTPS--> InfluxDB Cloud --> dashboard
                        --HTTPS--> ntfy.sh (alert przy wysokim CO2)
```

## Sprzęt

- Waveshare ESP32-S3-DEV-KIT-N8R8 (8 MB flash, 8 MB PSRAM OPI)
- Sensirion SCD40, I2C `0x62`, SDA = GPIO 8, SCL = GPIO 9, zasilanie 3V3
- I2C maks. 100 kHz

## Szkice

| Folder | Rola |
|---|---|
| `co2_node/` | Szkic produkcyjny: pomiar, agregacja, InfluxDB, alert ntfy |
| `scd40_read/` | Odczyt CO2/temp/RH na Serial co 5 s |
| `wifi_test/` | Test WiFi + NTP |
| `i2c_scan/` | Skaner adresów I2C |
| `i2c_diag/` | Test poziomów spoczynkowych SDA/SCL |

## Uruchomienie

1. Zainstaluj w Arduino IDE 2.x wsparcie ESP32 oraz bibliotekę
   `Sensirion I2C SCD4x` (wersja 1.x).
2. W folderze szkicu skopiuj `secrets.h.example` jako `secrets.h` i uzupełnij.
3. Board: **ESP32S3 Dev Module**, PSRAM: **OPI PSRAM**, Flash Size: **8MB**,
   USB CDC On Boot: **Disabled**.
4. Upload, Serial Monitor 115200.

## Dane

Zapis w line protocol do bucketu `co2`:

```
air,device_id=esp32-xxxxxxxxxxxx co2_ppm=657i,temperature_c=19.2,humidity_pct=48.1,samples=12i,rssi_dbm=-72i 1792337053
```

Alert włącza się przy 1200 ppm, gaśnie przy 1000 ppm, przypomnienie co 30 min.
TLS jest weryfikowany względem rootów Let's Encrypt (`co2_node/certs.h`).
