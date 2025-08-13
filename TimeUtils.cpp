#include "TimeUtils.h"
#include <WiFi.h>
#include "time.h"

void initTimeModule(const char* ssid, const char* password, long gmtOffset, int dstOffset) {
  // NOTE: caller should have Serial.begin() already to see prints.
  Serial.println("[TimeUtils] initTimeModule() starting...");

  // 1) Connect to Wi-Fi with a timeout (non-infinite)
  Serial.print("[TimeUtils] WiFi.begin()");
  WiFi.begin(ssid, password);

  const unsigned long wifiTimeoutMs = 20UL * 1000UL; // 20 seconds
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < wifiTimeoutMs) {
    delay(250);
    Serial.print('.'); // progress
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[TimeUtils] WiFi connected, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[TimeUtils] WARNING: WiFi not connected after timeout. Continuing (some features may not work).");
  }

  // 2) Configure NTP (configTime takes offsets in seconds)
  // This works even if WiFi isn't connected; getLocalTime will simply fail until NTP works.
  configTime(gmtOffset, dstOffset, "pool.ntp.org", "time.nist.gov");
  Serial.println("[TimeUtils] configTime() called, waiting for NTP...");

  // 3) Wait briefly for NTP sync (but do not block forever)
  const unsigned long ntpTimeoutMs = 8UL * 1000UL; // 8 seconds
  unsigned long ntpStart = millis();
  struct tm timeinfo;
  bool synced = false;
  while ((millis() - ntpStart) < ntpTimeoutMs) {
    if (getLocalTime(&timeinfo)) {
      synced = true;
      break;
    }
    delay(250);
  }

  if (synced) {
    char buf[64];
    strftime(buf, sizeof(buf), "%c", &timeinfo);
    Serial.print("[TimeUtils] NTP time set: ");
    Serial.println(buf);
  } else {
    Serial.println("[TimeUtils] WARNING: NTP time not acquired within timeout. Time functions will return unavailable until NTP syncs.");
  }

  Serial.println("[TimeUtils] initTimeModule() finished.");
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String("--:--:--");
  }
  // Convert to 12-hour clock with AM/PM
  int hour24 = timeinfo.tm_hour;
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  const char* ampm = (hour24 >= 12) ? "PM" : "AM";

  char buf[16];
  // Format: HH:MM:SS AM
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d %s", hour12, timeinfo.tm_min, timeinfo.tm_sec, ampm);
  return String(buf);
}

bool localTimeAvailable() {
  struct tm timeinfo;
  return getLocalTime(&timeinfo);
}
