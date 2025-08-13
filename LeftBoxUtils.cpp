#include "LeftBoxUtils.h"
#include "WeatherUtils.h"
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>

// extern TFT declared in main sketch
extern Adafruit_ST7789 tft;

// Local cached strings (simple module-level state)
static String lb_title[3] = { "Now Temp", "Wind", "Humidity" };
static String lb_value[3] = { "N/A", "N/A", "N/A" };

// Helper: round float to int string or N/A
static String fmtFloatVal(float v, const char* suffix = "") {
  if (isnan(v)) return String("N/A");
  int vi = (int)round(v);
  if (strlen(suffix)) return String(vi) + String(suffix);
  return String(vi);
}

void calculateLeftBoxDataFromForecastRaw() {
  // Reset defaults
  for (int i = 0; i < 3; ++i) lb_value[i] = "N/A";

  String raw = getCachedForecastRaw();
  if (raw.length() < 10) {
    // no cached forecast
    Serial.println("LeftBoxUtils: no cached forecast available");
    return;
  }

  // parse small JSON
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    Serial.print("LeftBoxUtils: JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  // Use first list[] item as 'now-ish' (3-hour window) — fallback safe reads
  JsonObject first = doc["list"][0].as<JsonObject>();
  if (!first) {
    Serial.println("LeftBoxUtils: no list[0] in forecast");
    return;
  }

  float temp = first["main"]["temp"] | NAN;
  float wind = first["wind"]["speed"] | NAN;
  int humidity = first["main"]["humidity"] | -1;

  // fill cached strings
  if (!isnan(temp)) lb_value[0] = String((int)round(temp)) + "F";
  else lb_value[0] = "N/A";

  if (!isnan(wind)) lb_value[1] = String((int)round(wind)) + " mph";
  else lb_value[1] = "N/A";

  if (humidity >= 0) lb_value[2] = String(humidity) + "%";
  else lb_value[2] = "N/A";

  Serial.println("LeftBoxUtils: values updated:");
  Serial.print("  Temp: "); Serial.println(lb_value[0]);
  Serial.print("  Wind: "); Serial.println(lb_value[1]);
  Serial.print("  Hum:  "); Serial.println(lb_value[2]);
}

void drawLeftBoxes(int x, int y, int w, int h) {
  // Split height into 3 equal boxes with small gaps
  int gap = 4;
  int boxH = (h - gap*2) / 3;

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  for (int i = 0; i < 3; ++i) {
    int bx = x;
    int by = y + i * (boxH + gap);
    // background
    tft.fillRect(bx, by, w, boxH, ST77XX_BLACK);
    // border
    tft.drawRect(bx, by, w, boxH, ST77XX_WHITE);

    // Title (small)
    tft.setCursor(bx + 6, by + 4);
    tft.print(lb_title[i]);

    // Value (larger)
    tft.setTextSize(2);
    // vertical center the value
    // nudge value down slightly to avoid collision with small title text above
    int vtextY = by + (boxH/2) - 2;
    tft.setCursor(bx + 6, vtextY);
    tft.print(lb_value[i]);

    // restore small text size
    tft.setTextSize(1);
  }
}
