/*
  N-Vector OTA client for ESP32 DevKit V1 + 0.96" SSD1306 SPI OLED
  Firmware version: 1.0.1

  Requirements:
    - Espressif ESP32 Arduino core >= 3.3.12 (built-in trusted CA bundle)
    - ArduinoJson library (v6.21+ or v7)
    - U8g2 library

  Board:
    ESP32 Dev Module
  Partition:
    Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)

  OLED wiring:
    GND -> GND
    VCC -> 3V3 (use 5V only if the exact OLED module explicitly supports it)
    D0  -> GPIO18 (SCK)
    D1  -> GPIO23 (MOSI)
    RES -> GPIO16
    DC  -> GPIO17
    CS  -> GPIO5

  First installation of this OTA client must be done over USB.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <time.h>
#include <esp_arduino_version.h>

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 3, 12)
#error "N-Vector OTA client requires Espressif ESP32 Arduino core 3.3.12 or newer for built-in CA bundle support."
#endif

// ---------- User configuration ----------
static const char* WIFI_SSID     = "Loading...";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

static const char* DEVICE_NAME = "ESP32-OLED";
static const char* FW_VERSION  = "1.0.3";

static const char* MANIFEST_URL =
  "https://raw.githubusercontent.com/nabiKZ/nvector-ota/main/firmware/esp32-oled/version.json";

// ---------- OLED pins ----------
#define OLED_CS   5
#define OLED_DC   17
#define OLED_RST  16

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI oled(
  U8G2_R0,
  OLED_CS,
  OLED_DC,
  OLED_RST
);

static void showLines(const char* line1, const char* line2, const char* line3 = "") {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(0, 12, "N-VECTOR OTA");
  oled.drawHLine(0, 16, 128);
  oled.drawStr(0, 31, line1);
  oled.drawStr(0, 45, line2);
  oled.drawStr(0, 59, line3);
  oled.sendBuffer();
}


static void showVersionStatus(const char* status, const char* details = "") {
  char versionLine[24];
  snprintf(versionLine, sizeof(versionLine), "FW: %s", FW_VERSION);
  showLines(versionLine, status, details);
}

static void showProgress(size_t done, size_t total) {
  unsigned int percent = total ? (unsigned int)((done * 100ULL) / total) : 0;
  if (percent > 100) percent = 100;

  char percentText[20];
  snprintf(percentText, sizeof(percentText), "Downloading %u%%", percent);

  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(0, 12, "N-VECTOR OTA");
  oled.drawHLine(0, 16, 128);
  oled.drawStr(0, 31, "Installing update");
  oled.drawFrame(0, 38, 128, 12);
  unsigned int barWidth = (unsigned int)((124ULL * done) / total);
  if (barWidth > 124) barWidth = 124;
  if (barWidth > 0) oled.drawBox(2, 40, barWidth, 8);
  oled.drawStr(0, 62, percentText);
  oled.sendBuffer();
}

static bool waitForWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(DEVICE_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  showLines("Connecting WiFi", "Please wait...");
  Serial.print("Connecting to Wi-Fi");

  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 30000UL) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    showLines("WiFi connection", "FAILED");
    Serial.println("Wi-Fi connection failed.");
    return false;
  }

  Serial.print("Wi-Fi connected. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static bool syncClock() {
  // TLS certificate validation requires a valid system clock.
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  showLines("WiFi connected", "Syncing time...");
  const unsigned long started = millis();

  while (time(nullptr) < 1700000000 && millis() - started < 20000UL) {
    delay(250);
  }

  if (time(nullptr) < 1700000000) {
    showLines("Clock sync", "FAILED");
    Serial.println("NTP time sync failed; HTTPS update check skipped.");
    return false;
  }

  Serial.println("System clock synchronized.");
  return true;
}

static bool parseVersion(const String& value,
                         unsigned long& major,
                         unsigned long& minor,
                         unsigned long& patch) {
  char tail = '\0';
  int parsed = sscanf(value.c_str(), "%lu.%lu.%lu%c", &major, &minor, &patch, &tail);
  return parsed == 3;
}

static bool isNewerVersion(const String& candidate, const String& current) {
  unsigned long cMaj, cMin, cPat;
  unsigned long lMaj, lMin, lPat;

  if (!parseVersion(candidate, cMaj, cMin, cPat) ||
      !parseVersion(current, lMaj, lMin, lPat)) {
    return false;
  }

  if (cMaj != lMaj) return cMaj > lMaj;
  if (cMin != lMin) return cMin > lMin;
  return cPat > lPat;
}


class OTAUpdateStream : public Stream {
public:
  explicit OTAUpdateStream(size_t totalBytes) : total_(totalBytes), received_(0) {}

  size_t write(uint8_t value) override {
    return write(&value, 1);
  }

  size_t write(const uint8_t* data, size_t length) override {
    if (data == nullptr || length == 0) return 0;

    // Update.write() in ESP32 Arduino Core accepts a mutable pointer.
    size_t written = Update.write(const_cast<uint8_t*>(data), length);
    received_ += written;

    if (total_ > 0 && written > 0) {
      showProgress(received_, total_);
    }
    return written;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  size_t received() const { return received_; }

private:
  size_t total_;
  size_t received_;
};

static bool downloadAndVerifyFirmware(const String& firmwareUrl,
                                      const String& expectedSha256) {
  WiFiClientSecure tls;
  tls.useBuiltinCACertBundle();

  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(5);
  http.setReuse(false);

  showLines("Connecting GitHub", "Downloading BIN...");
  if (!http.begin(tls, firmwareUrl)) {
    Serial.println("Could not initialize firmware HTTP request.");
    showLines("Firmware request", "FAILED");
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Firmware HTTP error: %d (%s)\n",
                  httpCode, HTTPClient::errorToString(httpCode).c_str());
    http.end();
    showLines("Firmware download", "HTTP ERROR");
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("Firmware Content-Length is missing or invalid.");
    http.end();
    showLines("Firmware size", "INVALID");
    return false;
  }

  Serial.printf("Firmware size: %d bytes\n", contentLength);

  if (!Update.begin((size_t)contentLength, U_FLASH)) {
    Serial.print("Update.begin failed: ");
    Update.printError(Serial);
    http.end();
    showLines("OTA partition", "NOT AVAILABLE");
    return false;
  }

  // Update library calculates SHA-256 while bytes are written and validates
  // the complete image in Update.end(). Set expected hash before first write.
  if (!Update.setSHA256(expectedSha256.c_str())) {
    Serial.println("Update.setSHA256 rejected the manifest digest.");
    Update.abort();
    http.end();
    showLines("SHA256 setup", "FAILED");
    return false;
  }

  OTAUpdateStream updateStream((size_t)contentLength);

  Serial.println("Streaming HTTPS response directly to OTA partition...");
  // HTTPClient::writeToStream() handles transient TLS read() == 0 results,
  // waiting for more data until its TCP timeout instead of treating a
  // temporary lack of TLS data as an immediate end-of-file.
  const int streamedBytes = http.writeToStream(&updateStream);
  const String streamError = HTTPClient::errorToString(streamedBytes);
  http.end();

  Serial.printf("HTTPClient stream result: %d (%s)\n",
                streamedBytes, streamError.c_str());
  Serial.printf("OTA bytes written: %u / %u\n",
                (unsigned int)updateStream.received(),
                (unsigned int)contentLength);

  if (streamedBytes < 0 ||
      updateStream.received() != (size_t)contentLength ||
      streamedBytes != contentLength) {
    Serial.println("HTTPS stream incomplete; aborting inactive OTA slot.");
    Update.abort();
    showLines("OTA transfer", "INCOMPLETE");
    return false;
  }

  if (!Update.end()) {
    Serial.print("Update.end failed (image/SHA256 validation): ");
    Update.printError(Serial);
    Update.abort();
    showLines("Flash finalize", "FAILED");
    return false;
  }

  Serial.print("Verified firmware SHA256: ");
  Serial.println(Update.sha256String());

  if (!Update.isFinished()) {
    Serial.println("Update not finished.");
    showLines("Firmware update", "INCOMPLETE");
    return false;
  }

  Serial.println("Firmware verified and written to inactive OTA slot.");
  showLines("Update verified", "Rebooting...");
  delay(1500);
  ESP.restart();
  return true;
}

static void checkGitHubForUpdate() {
  WiFiClientSecure tls;
  tls.useBuiltinCACertBundle();

  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(5);
  http.setReuse(false);

  showLines("Checking GitHub", "version.json...");
  if (!http.begin(tls, MANIFEST_URL)) {
    Serial.println("Could not initialize manifest request.");
    showLines("Manifest request", "FAILED");
    return;
  }

  http.addHeader("Cache-Control", "no-cache");
  const int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Manifest HTTP error: %d\n", httpCode);
    http.end();
    showLines("Manifest check", "HTTP ERROR");
    return;
  }

  const String payload = http.getString();
  http.end();

  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, payload);
  if (jsonError) {
    Serial.print("Manifest JSON error: ");
    Serial.println(jsonError.c_str());
    showLines("Manifest JSON", "INVALID");
    return;
  }

  const String device = doc["device"] | "";
  const String status = doc["status"] | "";
  const String remoteVersion = doc["version"] | "";
  const String firmwareUrl = doc["firmware"] | "";
  const String expectedSha256 = doc["sha256"] | "";

  Serial.printf("Current firmware: %s\n", FW_VERSION);
  Serial.printf("Remote firmware:  %s\n", remoteVersion.c_str());

  if (device != DEVICE_NAME || status != "published" ||
      firmwareUrl.length() == 0 || expectedSha256.length() != 64) {
    Serial.println("Manifest fields are invalid or device does not match.");
    showLines("Manifest/device", "MISMATCH");
    return;
  }

  if (!isNewerVersion(remoteVersion, FW_VERSION)) {
    Serial.println("No newer firmware available.");
    showVersionStatus("Already up to date");
    delay(1200);
    return;
  }

  char line2[24];
  snprintf(line2, sizeof(line2), "New version %s", remoteVersion.c_str());
  showLines("Update available", line2, "Starting OTA...");
  delay(1000);

  if (downloadAndVerifyFirmware(firmwareUrl, expectedSha256)) {
    // The ESP32 restarts after a successful update.
    return;
  }

  Serial.println("OTA update failed; current firmware remains active.");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  oled.begin();
  showVersionStatus("Starting...");

  if (!waitForWiFi()) {
    delay(1500);
    showVersionStatus("Offline mode");
    return;
  }

  if (!syncClock()) {
    delay(1500);
    showVersionStatus("Time unavailable");
    return;
  }

  checkGitHubForUpdate();

  delay(1500);
  char versionLine[24];
  snprintf(versionLine, sizeof(versionLine), "FW: %s", FW_VERSION);

  char ipLine[24];
  snprintf(ipLine, sizeof(ipLine), "IP: %s", WiFi.localIP().toString().c_str());

  showLines(versionLine, ipLine, "OTA check complete");
}

void loop() {
  delay(1000);
}
