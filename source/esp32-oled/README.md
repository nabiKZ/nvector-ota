# ESP32 OLED GitHub OTA client (v1.0.1)

## Hardware
- ESP32 Dev Module / ESP32-WROOM-32
- 0.96-inch 128x64 OLED, SSD1306, 4-wire SPI
- OLED GND -> GND
- OLED VCC -> 3V3 (use 5V only if the exact module is rated for it)
- D0/SCK -> GPIO18
- D1/MOSI -> GPIO23
- RES -> GPIO16
- DC -> GPIO17
- CS -> GPIO5

## Arduino IDE setup
- Board: ESP32 Dev Module
- Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)
- Install libraries: U8g2 by olikraus, ArduinoJson (6.21+ or 7)
- ESP32 Arduino core: 3.3.12 or newer (the sketch uses the built-in CA bundle and the Update library SHA-256 verification API).

## Configure before compiling
In `esp32-oled-ota-1.0.1.ino`, set:
- `WIFI_SSID`
- `WIFI_PASSWORD`

Do not commit real Wi-Fi credentials to this public repository.

## Initial installation
1. Open the sketch in Arduino IDE.
2. Compile and upload over USB.
3. Open Serial Monitor at 115200 baud.
4. The board connects to Wi-Fi, syncs time using NTP, then fetches:
   `https://raw.githubusercontent.com/nabiKZ/nvector-ota/main/firmware/esp32-oled/version.json`
5. It only installs a strictly newer version after validating the firmware SHA-256.

## Release process
1. Increment `FW_VERSION` in the sketch.
2. Compile and upload over USB for the first OTA-client installation, then export the compiled binary.
3. Compute SHA-256 with PowerShell:
   `Get-FileHash .\\firmware.bin -Algorithm SHA256`
4. Upload the binary as `firmware.bin` to a GitHub Release.
5. Update `firmware/esp32-oled/version.json` with the new version, release URL, SHA-256 and `"status": "published"`.

## Security note
HTTPS validates the GitHub TLS certificate and SHA-256 detects an altered or corrupted download when compared with the manifest. Since both the public manifest and binary are hosted in the same public repository, SHA-256 alone does not authenticate the publisher. Add a signed manifest (for example Ed25519) before deploying devices outside a controlled test environment.
