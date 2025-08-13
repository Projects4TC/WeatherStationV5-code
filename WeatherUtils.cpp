#include "WeatherUtils.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h> // for getLocalTime()

// Internal cached state
static String s_apiKey = "";
static String s_city = "";
static unsigned long s_cacheMs = 600000; // default 10 minutes
static unsigned long s_lastFetch = 0;
static String s_cachedReport = "Weather: unknown";    // short single-line summary for ticker
static String s_cachedForecastJson = "";              // raw forecast JSON payload (for GraphUtils)

// Small helper to trim and limit length
static String shorten(const String &src, size_t maxLen = 120) {
  if (src.length() <= maxLen) return src;
  return src.substring(0, maxLen - 3) + "...";
}

// Initialize weather subsystem
void initWeather(const char* apiKey, const char* cityQuery, unsigned long cacheMillis) {
  s_apiKey = String(apiKey);
  s_city = String(cityQuery);
  s_cacheMs = cacheMillis;
  s_lastFetch = 0; // force fetch on first tryUpdateWeather
  s_cachedReport = "Weather: loading...";
  s_cachedForecastJson = "";
}

// Build a short summary from forecast JSON (use first forecast entry as "now-ish")
static String buildReportFromForecastJson(JsonDocument &doc) {
  // doc structure: { "city": { "name": "...", ...}, "list": [ { "main": {"temp":...}, "wind":{"speed":...}, "weather":[{"description": "..."}], ... }, ... ] }
  const char* cityName = doc["city"]["name"] | "";
  JsonVariant first = doc["list"][0];
  float temp = first["main"]["temp"] | NAN;
  int humidity = first["main"]["humidity"] | -1;
  float wind = first["wind"]["speed"] | NAN;
  const char* desc = first["weather"][0]["description"] | "";

  char buf[160];
  if (isnan(temp)) {
    // fallback to simple text
    snprintf(buf, sizeof(buf), "%s %s", cityName, desc);
  } else {
    int t = (int)round(temp);
    int w = (int)round(wind);
    if (humidity >= 0) {
      snprintf(buf, sizeof(buf), "%s %d°F %s Hum %d%% Wind %dmph", cityName, t, desc, humidity, w);
    } else {
      snprintf(buf, sizeof(buf), "%s %d°F %s Wind %dmph", cityName, t, desc, w);
    }
  }
  return String(buf);
}

/*
  fetchForecastNow()
  - Performs HTTP GET to OpenWeather /data/2.5/forecast (3-hour)
  - On success stores:
      s_cachedForecastJson = raw payload
      s_cachedReport = short summary (first list[] item)
      s_lastFetch = millis()
    and prints a human-readable "Weather API called at: HH:MM:SS AM/PM" to Serial.
  - Returns true on successful fetch+parse+cache, false on error.
*/
bool fetchForecastNow() {
  // Only attempt if WiFi connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("fetchForecastNow(): WiFi not connected - skipping fetch");
    return false;
  }

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/forecast?q=" + s_city +
               "&appid=" + s_apiKey + "&units=imperial";
  Serial.print("fetchForecastNow(): requesting ");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();
  Serial.print("fetchForecastNow(): HTTP code ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.println("fetchForecastNow(): non-OK HTTP response");
    return false;
  }

  String payload = http.getString();
  http.end();

  // Attempt to parse the payload to ensure it's valid JSON
  // Forecast JSON can be fairly large: use a reasonably sized buffer
  StaticJsonDocument<8192> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("fetchForecastNow(): JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  // Build short one-line summary from parsed JSON (first item)
  String report = buildReportFromForecastJson(doc);
  report = shorten(report, 120);

  // Cache raw payload and the summary
  s_cachedForecastJson = payload;
  s_cachedReport = report;
  s_lastFetch = millis();

  // Print human-readable timestamp for the successful API call
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timestr[32];
    strftime(timestr, sizeof(timestr), "%I:%M:%S %p", &timeinfo); // e.g. "09:25:00 AM"
    Serial.print("Weather API called at: ");
    Serial.println(timestr);
  } else {
    Serial.print("Weather API called (millis): ");
    Serial.println(s_lastFetch);
  }

  return true;
}

// Return the short one-line weather summary for the ticker (cached)
String getWeatherReport() {
  return s_cachedReport;
}

// Return raw cached forecast JSON string (caller can deserialize it into a JsonDocument)
String getCachedForecastRaw() {
  return s_cachedForecastJson;
}

// Try to update weather if cache expired. Returns true if a real network fetch was performed.
bool tryUpdateWeather(unsigned long nowMillis) {
  if (s_lastFetch == 0 || (nowMillis - s_lastFetch) > s_cacheMs) {
    // fetch and update cache
    bool ok = fetchForecastNow();
    if (!ok) {
      Serial.println("tryUpdateWeather(): fetch failed - keeping previous cache");
    }
    return ok;
  }
  // Not due yet
  return false;
}
