#include "GraphUtils.h"
#include "WeatherUtils.h"      // for getCachedForecastRaw()
#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

// Extern TFT object declared in main sketch
extern Adafruit_ST7789 tft;

// Exported arrays (defined here)
float graphTemp[GRAPH_HOURS];
float graphWind[GRAPH_HOURS];
float graphPop[GRAPH_HOURS];
bool  graphValid[GRAPH_HOURS];
int   graphHourLabels[GRAPH_HOURS];

// Graph area state
static int g_x = 0, g_y = 0, g_w = 0, g_h = 0;

// Colors (tweak as desired)
static const uint16_t COL_BG      = ST77XX_BLACK;
static const uint16_t COL_AXIS    = ST77XX_WHITE;
static const uint16_t COL_GRID    = 0x4208; // dim grey
static const uint16_t COL_TEMP    = ST77XX_RED;
static const uint16_t COL_WIND    = ST77XX_CYAN;
static const uint16_t COL_POP     = ST77XX_YELLOW;
static const uint16_t COL_MARKER  = ST77XX_MAGENTA;
static const uint16_t COL_TEXT    = ST77XX_WHITE;

// Forward declarations of locals used earlier
static float lerpFloat(float a, float b, double t);
static void smoothArray(float *arr, bool *valid, int n);

// -------------------------- calculateGraphDataFromForecastRaw --------------------------
bool calculateGraphDataFromForecastRaw(bool smooth) {
  // initialize outputs to invalid
  for (int i = 0; i < GRAPH_HOURS; ++i) {
    graphTemp[i] = NAN;
    graphWind[i] = NAN;
    graphPop[i] = NAN;
    graphValid[i] = false;
    graphHourLabels[i] = 9 + i;
  }

  String raw = getCachedForecastRaw();
  if (raw.length() < 10) {
    Serial.println("GraphUtils: No cached forecast JSON available.");
    return false;
  }

  // Parse JSON
  StaticJsonDocument<8192> doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    Serial.print("GraphUtils: JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }
    // Debug: which city / timezone did the API return?
  const char* cityName = doc["city"]["name"] | "n/a";
  const char* country = doc["city"]["country"] | "n/a";
  long api_tz = doc["city"]["timezone"] | 0L;
  double lat = doc["city"]["coord"]["lat"] | 0.0;
  double lon = doc["city"]["coord"]["lon"] | 0.0;
  Serial.printf("API city: %s, %s  timezone(sec)=%ld  coord=%.4f,%.4f\n",
                cityName, country, api_tz, lat, lon);


  // Forecast list
  JsonArray list = doc["list"].as<JsonArray>();
  if (!list) {
    Serial.println("GraphUtils: forecast JSON missing 'list' array.");
    return false;
  }

  // timezone offset (seconds) if provided by the API
  long tz_offset = doc["city"]["timezone"] | 0L;

  // Determine today's midnight in the *city's* local time (use tz_offset returned by API)
  time_t now_t = time(NULL);                // current epoch (system UTC-based)
  time_t city_now = now_t + tz_offset;      // epoch adjusted to city's local time
  struct tm tm_city;
  gmtime_r(&city_now, &tm_city);            // interpret city_now as UTC structure (gives city's wall-clock)
  // midnight in city-local epoch (seconds)
  time_t midnight_local = city_now - (tm_city.tm_hour * 3600 + tm_city.tm_min * 60 + tm_city.tm_sec);

  //Why: city_now is the current epoch shifted into the city's local timeline. gmtime_r(&city_now, &tm_city) gives the city's broken-down time (hour/min/sec). Subtracting the H/M/S yields the epoch for that city’s midnight. All target_ts = midnight_local + H*3600 are now correct for the city.


  // Build a small vector of forecast samples: each has dt (UTC), local_ts, temp, wind, pop
  struct Sample { long dt; long local_ts; float temp; float wind; float pop; };
  const int MAX_SAMPLES = 256;
  Sample samples[MAX_SAMPLES];
  int sampleCount = 0;

  for (JsonObject item : list) {
    if (sampleCount >= MAX_SAMPLES) break;
    long dt = item["dt"] | 0L; // UTC epoch seconds
    long local_ts = dt + tz_offset; // local epoch seconds

    // read fields
    float temp = NAN;
    float wind = NAN;
    float pop  = NAN;
    if (item.containsKey("main")) temp = item["main"]["temp"] | NAN;
    if (item.containsKey("wind")) temp = temp; // noop to avoid warnings
    wind = item["wind"]["speed"] | NAN;
    pop = item["pop"] | NAN;

    samples[sampleCount++] = { dt, local_ts, temp, wind, pop };
  }

  if (sampleCount == 0) {
    Serial.println("GraphUtils: No forecast samples found.");
    return false;
  }

  //Debugging extra area below for checking sample data
    // ----- DEBUG: print raw forecast samples and mapping -----
  Serial.println("GraphUtils: raw forecast samples (UTC -> local):");
  for (int s = 0; s < sampleCount; ++s) {
    time_t u = (time_t)samples[s].dt;
    time_t l = (time_t)samples[s].local_ts;
    struct tm tm_u, tm_l;
    gmtime_r(&u, &tm_u);      // UTC
    gmtime_r(&l, &tm_l);      // city-local (interpret l as epoch shifted by tz_offset)

    char bufU[32], bufL[32];
    strftime(bufU, sizeof(bufU), "%Y-%m-%d %H:%M", &tm_u);
    strftime(bufL, sizeof(bufL), "%Y-%m-%d %H:%M", &tm_l);

    char line[160];
    float t = samples[s].temp;
    float w = samples[s].wind;
    float p = samples[s].pop;
    // Use NAN text if needed
    snprintf(line, sizeof(line), " s=%02d UTC=%s local=%s  T=%s W=%s POP=%s",
             s,
             bufU,
             bufL,
             isnan(t) ? "N/A" : String(t,1).c_str(),
             isnan(w) ? "N/A" : String(w,1).c_str(),
             isnan(p) ? "N/A" : String(p,2).c_str());
    Serial.println(line);
  }

  Serial.println("GraphUtils: mapping target hours -> sample indices (and alpha):");
  for (int i = 0; i < GRAPH_HOURS; ++i) {
    int H = 9 + i;
    long target_ts = midnight_local + (long)H * 3600L;
    int idx0 = -1, idx1 = -1;
    for (int s = 0; s < sampleCount; ++s) {
      if (samples[s].local_ts <= target_ts) idx0 = s;
      if (samples[s].local_ts >= target_ts) { idx1 = s; break; }
    }
    if (idx0 == -1) idx0 = 0;
    if (idx1 == -1) idx1 = sampleCount - 1;
    long t0 = samples[idx0].local_ts;
    long t1 = samples[idx1].local_ts;
    double alpha = (t1 == t0) ? 0.0 : double(target_ts - t0) / double(t1 - t0);

    // friendly times for idx0/idx1
    char t0s[32] = "n/a", t1s[32] = "n/a";
    if (idx0 >= 0) { time_t tmp = (time_t)t0; struct tm tm0; gmtime_r(&tmp, &tm0); strftime(t0s, sizeof(t0s), "%H:%M", &tm0); }
    if (idx1 >= 0) { time_t tmp = (time_t)t1; struct tm tm1; gmtime_r(&tmp, &tm1); strftime(t1s, sizeof(t1s), "%H:%M", &tm1); }

    //Fixed the above city-local wallclock.
    //Why: local_ts = dt + tz_offset is already the epoch representing city-local wallclock expressed as a raw epoch. Using gmtime_r() on that epoch produces the city's wallclock fields; using localtime_r() applies the device/system timezone conversion and yields the wrong display.

    char mapLine[120];
    snprintf(mapLine, sizeof(mapLine),
             " H=%02d -> idx0=%d (%s)  idx1=%d (%s)  alpha=%.2f",
             H, idx0, t0s, idx1, t1s, alpha);
    Serial.println(mapLine);
  }
  Serial.println("----- end debug -----");


  // For each target hour H in 9..21, compute target_ts (local) for today's date
  bool anyValid = false;
  for (int i = 0; i < GRAPH_HOURS; ++i) {
    int H = 9 + i;
    long target_ts = midnight_local + (long)H * 3600L;

    // find two samples s0,s1 such that s0.local_ts <= target_ts <= s1.local_ts
    int idx0 = -1, idx1 = -1;
    for (int s = 0; s < sampleCount; ++s) {
      if (samples[s].local_ts <= target_ts) idx0 = s;
      if (samples[s].local_ts >= target_ts) { idx1 = s; break; }
    }

    // if idx0 == -1 use first sample as both bounds (extrapolate/backfill)
    if (idx0 == -1) idx0 = 0;
    // if idx1 == -1 use last sample
    if (idx1 == -1) idx1 = sampleCount - 1;

    // if both indices are valid, compute interpolation weight
    long t0 = samples[idx0].local_ts;
    long t1 = samples[idx1].local_ts;

    float tempVal = NAN;
    float windVal = NAN;
    float popVal  = NAN;

    if (idx0 == idx1 || t1 == t0) {
      // exact match or single sample
      tempVal = samples[idx0].temp;
      windVal = samples[idx0].wind;
      popVal  = samples[idx0].pop;
    } else {
      double alpha = double(target_ts - t0) / double(t1 - t0);
      // clamp alpha
      if (alpha < 0.0) alpha = 0.0;
      if (alpha > 1.0) alpha = 1.0;
      // interpolate each value if not NaN; if NaN on one side, pick the other
      float t0v = samples[idx0].temp;
      float t1v = samples[idx1].temp;
      if (!isnan(t0v) && !isnan(t1v)) tempVal = lerpFloat(t0v, t1v, alpha);
      else if (!isnan(t0v)) tempVal = t0v;
      else if (!isnan(t1v)) tempVal = t1v;

      float w0v = samples[idx0].wind;
      float w1v = samples[idx1].wind;
      if (!isnan(w0v) && !isnan(w1v)) windVal = lerpFloat(w0v, w1v, alpha);
      else if (!isnan(w0v)) windVal = w0v;
      else if (!isnan(w1v)) windVal = w1v;

      float p0v = samples[idx0].pop;
      float p1v = samples[idx1].pop;
      if (!isnan(p0v) && !isnan(p1v)) popVal = lerpFloat(p0v, p1v, alpha);
      else if (!isnan(p0v)) popVal = p0v;
      else if (!isnan(p1v)) popVal = p1v;
    }

    // mark valid if we have at least one non-NaN
    bool valid = !(isnan(tempVal) && isnan(windVal) && isnan(popVal));
    graphValid[i] = valid;
    if (valid) {
      graphTemp[i] = tempVal;
      graphWind[i] = windVal;
      graphPop[i]  = popVal;
      anyValid = true;
    } else {
      graphTemp[i] = NAN;
      graphWind[i] = NAN;
      graphPop[i]  = NAN;
    }
  }

  //adding extra debugging here to see if data is valid
  // --- DEBUG: print computed hourly arrays + stats ---
Serial.println("DBG: Graph hourly arrays (raw / after interp):");
float minT = 1e9, maxT = -1e9;
float minW = 1e9, maxW = -1e9;
int validCount = 0;
for (int i = 0; i < GRAPH_HOURS; ++i) {
  int H = graphHourLabels[i];
  bool v = graphValid[i];

  char tbuf[16], wbuf[16], pbuf[16];
  if (v && !isnan(graphTemp[i])) snprintf(tbuf, sizeof(tbuf), "%.1f", graphTemp[i]); else snprintf(tbuf, sizeof(tbuf), "NaN");
  if (v && !isnan(graphWind[i])) snprintf(wbuf, sizeof(wbuf), "%.1f", graphWind[i]); else snprintf(wbuf, sizeof(wbuf), "NaN");
  if (v && !isnan(graphPop[i]))  snprintf(pbuf, sizeof(pbuf), "%.2f", graphPop[i]);  else snprintf(pbuf, sizeof(pbuf), "NaN");

  Serial.printf(" H=%02d : T=%6s F  W=%6s mph  POP=%6s  valid=%d\n", H, tbuf, wbuf, pbuf, v ? 1 : 0);

  if (v && !isnan(graphTemp[i])) { if (graphTemp[i] < minT) minT = graphTemp[i]; if (graphTemp[i] > maxT) maxT = graphTemp[i]; }
  if (v && !isnan(graphWind[i])) { if (graphWind[i] < minW) minW = graphWind[i]; if (graphWind[i] > maxW) maxW = graphWind[i]; }
  if (v) validCount++;
}

if (validCount == 0) {
  Serial.println("DBG: No valid points!");
} else {
  Serial.printf("DBG: temp min=%.1f max=%.1f  wind min=%.1f max=%.1f  valid=%d\n",
                (minT==1e9 ? NAN : minT),
                (maxT==-1e9 ? NAN : maxT),
                (minW==1e9 ? NAN : minW),
                (maxW==-1e9 ? NAN : maxW),
                validCount);
}


  // Optional smoothing (3-point moving average)
  if (smooth && anyValid) {
    smoothArray(graphTemp, graphValid, GRAPH_HOURS);
    smoothArray(graphWind, graphValid, GRAPH_HOURS);
    smoothArray(graphPop, graphValid, GRAPH_HOURS);
  }

  // Debug print summary
  Serial.println("GraphUtils: calculateGraphDataFromForecastRaw() results:");
  for (int i = 0; i < GRAPH_HOURS; ++i) {
    int H = graphHourLabels[i];
    if (graphValid[i]) {
      char buf[120];
      int popPct = isnan(graphPop[i]) ? -1 : int(round(graphPop[i] * 100.0f));
      if (popPct >= 0)
        snprintf(buf, sizeof(buf), " H=%02d : T=%.1fF  W=%.1fmph  POP=%d%%", H,
                 isnan(graphTemp[i]) ? NAN : graphTemp[i],
                 isnan(graphWind[i]) ? NAN : graphWind[i],
                 popPct);
      else
        snprintf(buf, sizeof(buf), " H=%02d : T=%.1fF  W=%.1fmph  POP=N/A", H,
                 isnan(graphTemp[i]) ? NAN : graphTemp[i],
                 isnan(graphWind[i]) ? NAN : graphWind[i]);
      Serial.println(buf);
    } else {
      Serial.printf(" H=%02d : no data\n", H);
    }
  }

  return anyValid;
}

// -------------------------- setGraphArea --------------------------
void setGraphArea(int x, int y, int w, int h) {
  g_x = x; g_y = y; g_w = w; g_h = h;
}

// -------------------------- helper: safe min/max -------------------
static bool findMinMax(const float *arr, const bool *valid, int n, float &minV, float &maxV) {
  bool found = false;
  minV = 0; maxV = 0;
  for (int i = 0; i < n; ++i) {
    if (!valid[i]) continue;
    float v = arr[i];
    if (!found) { minV = maxV = v; found = true; }
    else {
      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
    }
  }
  return found;
}

// -------------------------- drawGraph ------------------------------
void drawGraph(int graphType) {
  // graphType: 0=temp, 1=wind, 2=pop
  if (g_w <= 8 || g_h <= 8) return; // area not set

  // Clear graph area
  tft.fillRect(g_x, g_y, g_w, g_h, COL_BG);

  // Draw border
  tft.drawRect(g_x, g_y, g_w, g_h, COL_AXIS);

  // Choose metric arrays
  float *arr = (graphType == 0) ? graphTemp : (graphType == 1) ? graphWind : graphPop;
  const uint16_t lineColor = (graphType == 0) ? COL_TEMP : (graphType == 1) ? COL_WIND : COL_POP;
  const char *title = (graphType == 0) ? "Temperature (F)" : (graphType == 1) ? "Wind (mph)" : "Precip %";

  // Determine min/max for Y
  float vmin, vmax;
  bool hasData = findMinMax(arr, graphValid, GRAPH_HOURS, vmin, vmax);
  if (!hasData) {
    // No data: render message
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(g_x + 6, g_y + g_h / 2 - 6);
    tft.print("No graph data");
    return;
  }

  // For POP (0..1) convert to percent for nicer scale
  bool showPercent = false;
  if (graphType == 2) {
    showPercent = true;
    // values are 0..1; convert to 0..100 for plotting scale
    for (int i = 0; i < GRAPH_HOURS; ++i) {
      if (graphValid[i] && !isnan(graphPop[i])) graphPop[i] = graphPop[i] * 100.0f;
    }
    // re-evaluate min/max
    findMinMax(arr, graphValid, GRAPH_HOURS, vmin, vmax);
  }

  // Expand min/max a little for visual margin
  float padding = (vmax - vmin) * 0.12f;
  if (padding <= 0.5f) padding = 0.5f;
  vmin -= padding;
  vmax += padding;
  if (vmin == vmax) { vmin -= 1.0f; vmax += 1.0f; }

  // Draw horizontal grid lines (4 lines)
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT);
  int gridLines = 4;
  for (int gi = 0; gi <= gridLines; ++gi) {
    int yy = g_y + (gi * (g_h - 1)) / gridLines;
    // faint grid
    tft.drawFastHLine(g_x + 1, yy, g_w - 2, COL_GRID);
    // label Y at left
    float vlabel = vmax - ( (float)gi * (vmax - vmin) / gridLines );
    char lbl[12];
    if (showPercent) snprintf(lbl, sizeof(lbl), "%d%%", (int)round(vlabel));
    else snprintf(lbl, sizeof(lbl), "%g", round(vlabel*10)/10.0); // 1 decimal
    tft.setCursor(g_x + 4, yy - 6);
    tft.print(lbl);
  }

    // Draw X ticks & hour labels (9,12,3,6,9 in 12-hour format)
  tft.setTextSize(1);
  const int majorTicks24[] = {9,12,15,18,21};
  int numMajor = sizeof(majorTicks24)/sizeof(majorTicks24[0]);
  for (int ti = 0; ti < numMajor; ++ti) {
    int hour24 = majorTicks24[ti];
    // map hour index to x position
    float frac = float(hour24 - 9) / float(GRAPH_HOURS - 1);
    int xx = g_x + 1 + (int)round(frac * (g_w - 3));
    tft.drawFastVLine(xx, g_y + g_h - 12, 8, COL_AXIS);

    // convert to 12-hour display and print without leading zero
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    char buf[6];
    snprintf(buf, sizeof(buf), "%d", hour12);
    tft.setCursor(xx - 6, g_y + g_h - 10);
    tft.print(buf);
  }

/*
  // Draw X ticks & hour labels (9,12,15,18,21)  //changed this out for the better code above using 12hr time frame. saving for delete later, or optional military time button later
  tft.setTextSize(1);
  const int majorTicks[] = {9,12,15,18,21};
  int numMajor = sizeof(majorTicks)/sizeof(majorTicks[0]);
  for (int ti = 0; ti < numMajor; ++ti) {
    int hour = majorTicks[ti];
    // map hour index to x: index = hour - 9
    float frac = float(hour - 9) / float(GRAPH_HOURS - 1);
    int xx = g_x + 1 + (int)round(frac * (g_w - 3));
    tft.drawFastVLine(xx, g_y + g_h - 12, 8, COL_AXIS);
    // small label under axis
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d", hour);
    tft.setCursor(xx - 6, g_y + g_h - 10);
    tft.print(buf);
  }
*/



  // Compute pixel positions for each graph point
  int px[GRAPH_HOURS], py[GRAPH_HOURS];
  for (int i = 0; i < GRAPH_HOURS; ++i) {
    float fracX = float(i) / float(GRAPH_HOURS - 1);
    px[i] = g_x + 1 + (int)round(fracX * (g_w - 3)); // inside border
    if (graphValid[i] && !isnan(arr[i])) {
      float v = arr[i];
      float fracY = (v - vmin) / (vmax - vmin);
      if (fracY < 0) fracY = 0;
      if (fracY > 1) fracY = 1;
      py[i] = g_y + (g_h - 1) - (int)round(fracY * (g_h - 1));
    } else {
      py[i] = g_y + g_h - 1; // bottom as placeholder
    }
  }

  // Draw polyline connecting consecutive valid points
  for (int i = 0; i < GRAPH_HOURS - 1; ++i) {
    if (graphValid[i] && graphValid[i+1]) {
      tft.drawLine(px[i], py[i], px[i+1], py[i+1], lineColor);
      // small cap
      tft.fillCircle(px[i], py[i], 2, lineColor);
    } else if (graphValid[i]) {
      tft.fillCircle(px[i], py[i], 2, lineColor);
    }
  }
  // last point dot
  if (graphValid[GRAPH_HOURS-1]) tft.fillCircle(px[GRAPH_HOURS-1], py[GRAPH_HOURS-1], 2, lineColor);

  // Draw title in top-left of graph area
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(g_x + 6, g_y + 4);
  tft.print(title);

  // Draw min/max labels top-right & bottom-right
  char topLbl[16], botLbl[16];
  if (showPercent) {
    snprintf(topLbl, sizeof(topLbl), "Max %d%%", (int)round(vmax));
    snprintf(botLbl, sizeof(botLbl), "Min %d%%", (int)round(vmin));
  } else {
    snprintf(topLbl, sizeof(topLbl), "Max %.0f", round(vmax));
    snprintf(botLbl, sizeof(botLbl), "Min %.0f", round(vmin));
  }
  tft.setCursor(g_x + g_w - 60, g_y + 4);
  tft.print(topLbl);
  tft.setCursor(g_x + g_w - 60, g_y + g_h - 12);
  tft.print(botLbl);

  // Draw current time marker: compute local fractional position
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int curH = timeinfo.tm_hour;
    int curM = timeinfo.tm_min;
    float curPos = (float(curH) + float(curM)/60.0f) - 9.0f; // 9..21 => 0..12
    float fracPos = curPos / float(GRAPH_HOURS - 1);
    if (fracPos >= 0.0f && fracPos <= 1.0f) {
      int markerX = g_x + 1 + (int)round(fracPos * (g_w - 3));
      // draw vertical guide
      tft.drawFastVLine(markerX, g_y+2, g_h-4, COL_MARKER);
      // compute Y by interpolating between nearest graph points for selected metric
      // find surrounding indices
      float exactIdxF = curPos; // 0..12
      int idxL = (int)floor(exactIdxF);
      int idxR = (int)ceil(exactIdxF);
      if (idxL < 0) idxL = 0;
      if (idxR >= GRAPH_HOURS) idxR = GRAPH_HOURS - 1;
      int markerY = g_y + g_h - 4;
      if (graphValid[idxL] && graphValid[idxR] && !isnan(arr[idxL]) && !isnan(arr[idxR])) {
        float t = (exactIdxF - idxL);
        float valL = arr[idxL];
        float valR = arr[idxR];
        float vmarker = lerpFloat(valL, valR, t);
        // map to pixel y
        float fracY = (vmarker - vmin) / (vmax - vmin);
        if (fracY < 0) fracY = 0;
        if (fracY > 1) fracY = 1;
        markerY = g_y + (g_h - 1) - (int)round(fracY * (g_h - 1));
      }
      // marker circle
      tft.fillCircle(markerX, markerY, 4, COL_MARKER);

      
            // label near marker -> show simple 12-hour "Hpm"/"Ham" (e.g. "3pm")
      int hour24_marker = timeinfo.tm_hour;
      int hour12_marker = hour24_marker % 12;
      if (hour12_marker == 0) hour12_marker = 12;
      const char *ampm = (hour24_marker >= 12) ? "pm" : "am";
      char markerLabel[8];
      snprintf(markerLabel, sizeof(markerLabel), "%d%s", hour12_marker, ampm);

      /*
      // label near marker
      char markerLabel[32];
      if (showPercent) snprintf(markerLabel, sizeof(markerLabel), "%02d:%02d %d%%", timeinfo.tm_hour, timeinfo.tm_min, (int)round(arr[(int)round(exactIdxF)]));
      else snprintf(markerLabel, sizeof(markerLabel), "%02d:%02d %.0f", timeinfo.tm_hour, timeinfo.tm_min, round(arr[(int)round(exactIdxF)]));
      */

      // draw label with background for legibility
      int lblX = markerX + 6;
      int lblY = max(g_y + 6, markerY - 10);
      tft.fillRect(lblX - 2, lblY - 2, 60, 12, COL_BG);
      tft.setCursor(lblX, lblY);
      tft.setTextSize(1);
      tft.setTextColor(COL_MARKER);
      tft.print(markerLabel);
    }
  }

  // done
}

// ------------- helpers -------------
static float lerpFloat(float a, float b, double t) {
  return a + (b - a) * (float)t;
}

// Weighted 3-point smoothing (center has higher weight => less aggressive smoothing)
static void smoothArray(float *arr, bool *valid, int n) {
  float tmp[GRAPH_HOURS];
  for (int i = 0; i < n; ++i) tmp[i] = arr[i];

  for (int i = 0; i < n; ++i) {
    if (!valid[i] || isnan(tmp[i])) continue;

    float center = tmp[i];
    float left = NAN, right = NAN;
    bool hasLeft = false, hasRight = false;

    if (i > 0 && valid[i-1] && !isnan(tmp[i-1])) { left = tmp[i-1]; hasLeft = true; }
    if (i < n-1 && valid[i+1] && !isnan(tmp[i+1])) { right = tmp[i+1]; hasRight = true; }

    // Weighted average: center weight = 2, neighbors weight = 1 each (if present)
    float sum = center * 2.0f;
    int cnt = 2;
    if (hasLeft)  { sum += left;  cnt++; }
    if (hasRight) { sum += right; cnt++; }

    arr[i] = sum / float(cnt);
  }
}




