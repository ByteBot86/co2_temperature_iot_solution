# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Cel projektu

Projekt IoT: czujnik CO2 oparty na ESP32-S3 + SCD40. Docelowy przepływ danych:

```
SCD40 --I2C--> ESP32-S3 --HTTPS--> InfluxDB Cloud --> dashboard
                        --HTTPS--> ntfy.sh (alert push przy wysokim CO2)
```

Cała ścieżka działa od końca do końca. Produkcyjny szkic to **`co2_node/`** — pozostałe foldery to narzędzia diagnostyczne.

## Sprzęt — fakty krytyczne

| Element | Wartość |
|---|---|
| Płytka | Waveshare ESP32-S3-DEV-KIT-N8R8 (ESP32-S3-WROOM-1-N8R8) |
| Flash / PSRAM | 8 MB flash, 8 MB PSRAM **octal (OPI)** |
| Czujnik | SCD40, I2C, adres **0x62** |
| SDA | GPIO 8 |
| SCL | GPIO 9 |
| Zasilanie czujnika | 3V3 |

Porty COM na maszynie dewelopera:

- **COM4** — `USB-Enhanced-SERIAL CH343` (VID 1A86). Tym się flashuje i czyta `Serial`.
- **COM3** — natywny USB ESP32-S3 (VID 303A/PID 1001), JTAG/CDC. Nieużywany.

Jedno gniazdo USB-C obsługuje oba interfejsy przez hub CH334.

## Pułapki, które już kosztowały czas

- **I2C maks. 100 kHz.** SCD40 nie toleruje 400 kHz. Wszystkie szkice wołają `Wire.begin(8, 9, 100000)` — nie podnosić.
- **PSRAM musi być ustawione na OPI PSRAM** w Tools. Wersja N8R8 ma octal PSRAM; złe ustawienie daje bootloop.
- **`USB CDC On Boot: Disabled`** — przy tym ustawieniu `Serial` idzie przez UART0 do CH343, czyli na COM4. Zmiana tego ustawienia przeniesie wyjście na COM3 i Serial Monitor na COM4 zamilknie.
- **Czujnik jest wsadzony "na sucho"** (goldpiny bez lutowania, dla zachowania prawa zwrotu). Styk jest niepewny — przy objawach typu "brak urządzenia na I2C" najpierw sprawdzić styk, nie kod. Służy do tego `i2c_diag`.
- **Zmiany połączeń tylko przy odłączonym USB.** Hot-plug sygnałów przed GND zasila SCD40 przez diody ochronne i może go uszkodzić.
- **Nagłówki HTTP muszą być ASCII.** Tytuły powiadomień ntfy bez polskich znaków — diakrytyki w nagłówku psują żądanie. Treść w ciele żądania może być UTF-8.
- **`WiFi.macAddress()` na tej płytce zwraca same zera** — także po `WiFi.mode(WIFI_STA)` i po połączeniu z siecią. Do identyfikatora urządzenia używać **`ESP.getEfuseMac()`**: czyta fabryczny MAC z eFuse i nie wymaga WiFi. Objaw błędnego podejścia: `device_id` równe `esp32-000000`.
- **GPIO 33–37 są zajęte przez octal PSRAM** — nie używać.
- **Polskie znaki w ścieżce projektu ("węgla") nie przeszkadzają** — toolchain ESP32 kompiluje poprawnie. Sprawdzone.
- **WiFi tylko 2,4 GHz.** RSSI w miejscu testów waha się od -67 do -85 dBm, czyli słabo. Przy pracy 24/7 potrzebny reconnect i bufor pomiarów.

## Struktura — folder to projekt

Arduino IDE traktuje **folder jako projekt (sketch)**. Nazwa folderu musi być identyczna z nazwą pliku `.ino`. Wszystkie pozostałe pliki w folderze (`.h`, `.cpp`) IDE wciąga automatycznie jako zakładki.

Nie otwierać plików `.h` przez File → Open — otwierać tylko `.ino`, reszta pojawi się sama.

Szkice powstały jako kolejne etapy diagnostyki i warto je zachować do debugowania regresji:

| Folder | Rola |
|---|---|
| `i2c_scan/` | Skanuje adresy I2C. Pierwszy test po każdej zmianie okablowania. Szuka 0x62. |
| `i2c_diag/` | Sprawdza poziomy spoczynkowe SDA/SCL przez `INPUT_PULLDOWN`. Rozróżnia brak styku (LOW) od sprawnej magistrali (HIGH). **Ograniczenie: moduł bez pull-upów też da LOW.** |
| `scd40_read/` | Odczyt CO2/temp/RH co 5 s z progami jakości powietrza. Referencyjny sposób użycia biblioteki. |
| `co2_node/` | **Szkic produkcyjny.** Odczyt + agregacja + zapis do Influxa + alert ntfy. Tu się pracuje. |
| `wifi_test/` | WiFi + NTP (strefa `CET-1CEST,M3.5.0,M10.5.0/3`). Hasło w `secrets.h`. |

`secrets.h` jest w `.gitignore`. Każdy nowy szkic z poświadczeniami powinien trzymać je w `secrets.h` w swoim folderze, nie w `.ino`.

## Build i upload

Nie ma builda z CLI. Wszystko przez **Arduino IDE 2.x** (GUI):

1. File → Open → `<folder>/<folder>.ino`
2. Board: **ESP32S3 Dev Module**, Port: **COM4**
3. Tools: **PSRAM = OPI PSRAM**, **Flash Size = 8MB (64Mb)**, **USB CDC On Boot = Disabled**
4. Upload
5. Serial Monitor (`Ctrl+Shift+M`), **115200**

Jeśli Serial Monitor milczy po przełożeniu kabla: port się nie zmienia (nadal COM4), wystarczy nacisnąć **RESET** na płytce przy otwartym monitorze.

`arduino-cli` i PlatformIO **nie są zainstalowane**. Gdyby dodawać `arduino-cli`, FQBN to `esp32:esp32:esp32s3`; nazwy opcji (PSRAM, flash) sprawdzić przez `arduino-cli board details --fqbn esp32:esp32:esp32s3`.

## Biblioteki

Zainstalowane w `~/Documents/Arduino/libraries/`:

- `Sensirion_I2C_SCD4x` — oficjalna biblioteka producenta czujnika
- `Sensirion_Core` — zależność powyższej

**API wersji 1.x różni się od 0.x.** Większość tutoriali w sieci opisuje stare 0.x. Obowiązuje:

```cpp
#include <SensirionI2cScd4x.h>
SensirionI2cScd4x scd4x;
scd4x.begin(Wire, SCD41_I2C_ADDR_62);   // stala dziala tez dla SCD40
scd4x.readMeasurement(co2 /*uint16_t&*/, temp /*float&*/, rh /*float&*/);
```

Metody zwracają `int16_t` — 0 oznacza sukces. `stopPeriodicMeasurement()` na starcie zwraca błąd po zimnym starcie i to jest normalne.

Przy zmianach API sprawdzać przykłady dołączone do zainstalowanej wersji (File → Examples), nie blogi.

## Zachowanie czujnika

- Nowy pomiar co **5 s**. Odpytywanie częściej nie ma sensu.
- Pierwszy wynik ~5 s po `startPeriodicMeasurement()`.
- Wiarygodne wartości po kilku minutach rozgrzewki. `co2 == 0` oznacza odczyt nieważny — pomijać, nie logować jako pomiar.
- Pobór prądu impulsowo do ~205 mA. Przewody krótkie, bez rozgałęzień.
- Progi używane w `scd40_read`: <800 dobrze, <1200 podwyższone, <2000 duszno, powyżej źle. Typowo w pomieszczeniu 400–1000 ppm.
- Alert produkcyjny planowany ~1200 ppm **z histerezą** — bez niej powiadomienia będą się powtarzać wokół progu.
- Do chmury wysyłać co 30–60 s, nie co 5 s.

## Chmura

**Baza: InfluxDB Cloud**, darmowy plan. Organizacja `co2`, bucket `co2`, region
`eu-central-1-1.aws.cloud2.influxdata.com`. Dashboard `CO2`.

Zapis przez **HTTPS POST** na `/api/v2/write?org=...&bucket=...&precision=s`,
naglowek `Authorization: Token <token>`. Cialo to **InfluxDB line protocol**, nie JSON:

```
air,device_id=esp32-xxxxxxxxxx co2_ppm=657i,temperature_c=19.2,... 1792337053
```

Sukces to **HTTP 204**, nie 200. Sufiks `i` przy liczbach calkowitych jest konieczny —
bez niego Influx zapisze pole jako float i zmiana typu w locie wywoli blad zapisu.

**Alerty: ntfy.sh**, realizowane **w firmware**, nie w Influxie. Powod: darmowy plan
InfluxDB Cloud obsluguje jako kanal powiadomien **wylacznie Slacka** — PagerDuty i webhooki
HTTP sa platne. Alert w firmware jest niezalezny od planu i daje kontrole nad histereza.

Progi w `co2_node.ino`: wlacza sie przy `CO2_ALERT_HIGH` (1200 ppm), gasnie dopiero przy
`CO2_ALERT_CLEAR` (1000 ppm), przypomnienie co `ALERT_REPEAT_MS` (30 min).

**TLS jest weryfikowany, nie wylaczony.** `certs.h` zawiera roota Let's Encrypt
(ISRG Root X1 + Root YR). Oba serwisy — InfluxDB Cloud i ntfy.sh — maja certyfikaty od
Let's Encrypt, wiec wystarcza te same roota. Nie uzywac `setInsecure()`: token Influxa
lecialby wtedy po niewalidowanym TLS. Gdy TLS zacznie sie wywalac, odswiezyc certyfikaty
z https://letsencrypt.org/certificates/

## Stan projektu

Zrobione:

1. Toolchain, sterowniki, wsparcie ESP32 w Arduino IDE
2. Potwierdzony sprzęt — czujnik odpowiada na 0x62, poprawne odczyty CO2/temp/RH
3. WiFi + NTP
4. Zapis do InfluxDB Cloud przez HTTPS (potwierdzone HTTP 204), dashboard `CO2`
5. Alert push przez ntfy.sh z histerezą

Następne:

6. **Buforowanie pomiarów przy braku sieci** — dziś przy nieudanym zapisie rekord ginie. Konieczne przy RSSI -85 dBm. Reconnect WiFi już jest.
7. Sprawdzanie `dataReady` przed `readMeasurement()` — teraz pierwszy odczyt po starcie zwraca błąd 527 (nieszkodliwy, ale zaśmieca log)
8. `setTemperatureOffset()` i ewentualnie FRC — **dopiero po montażu w docelowej obudowie**. ASC (automatyczna kalibracja) jest włączone domyślnie i wystarcza, o ile urządzenie pracuje ciągle i widzi świeże powietrze min. 1 h/tydzień.
9. OTA, ewentualnie deep sleep i tryb `startLowPowerPeriodicMeasurement()` — tylko gdyby projekt przeszedł na baterię

Opcja na później, nie porzucona: własny backend + baza + frontend zamiast Influxa. Payload urządzenia by się nie zmienił — inny URL i nagłówki.

## Komunikacja

Użytkownik jest programistą z podstawami elektroniki. Pisze po polsku i oczekuje odpowiedzi po polsku.

Poprosił wyraźnie o **krótkie zdania i pojedyncze kroki** — jeden krok na wiadomość, potem czekanie na wynik, zanim pójdzie się dalej. Nie wysypywać całej procedury naraz.
