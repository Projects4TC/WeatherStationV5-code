//WeatherStationV5

/*
  WeatherStationV5.ino
  Main skeleton for v5:
  - Top 20%: small scrolling weather ticker (adjustable size)
  - Middle-left: small stat boxes (LeftBoxUtils)
  - Middle-right: rotating graphs (GraphUtils)
  - Bottom-left: clock (TimeUtils) — unchanged from v4
  - Uses WeatherUtils for fetching + caching forecast
  - Orchestration is done with millis() timers
*/

// ----- core libs -----
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "Freenove_WS2812_Lib_for_ESP32.h"

// ----- helper modules (to be implemented) -----
#include "TimeUtils.h"    // initTimeModule(), getTimeString(), getLocalHour()
#include "WeatherUtils.h" // initWeather(), getWeatherReport(), tryUpdateWeather(now)
#include "GraphUtils.h"   // calculateGraphDataFromForecast(...), drawGraph(graphIndex)
#include "LeftBoxUtils.h" // calculateLeftBoxData(...), drawLeftBoxes()
#include "UIUtils.h"      // drawBox(), drawLabel(), useful UI helpers

// ----- TFT pins and object (Waveshare ESP32S3 1.9") -----
#define TFT_CS    12
#define TFT_DC    11
#define TFT_RST    9
#define TFT_BL    14
#define TFT_MOSI  13
#define TFT_SCLK  10
#define TFT_MISO   2
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ----- LED strip (unchanged) -----
#define LEDS_COUNT  2
#define LEDS_PIN    15
#define CHANNEL     0
Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(LEDS_COUNT, LEDS_PIN, CHANNEL);

// ----- Credentials / timezone -----
// Replace with your own if needed (you already have these in v4)
const char* WIFI_SSID     = "You SSID here";
const char* WIFI_PASSWORD = "Wireless Network password here";
const char* OPENWEATHER_KEY = "your API key here"; // or put in WeatherUtils init

// Eastern US example: EDT/EST handling is done in TimeUtils (configTime or TZ string)
const long GMT_OFFSET = -5 * 3600; // change as appropriate or use TZ strings
const int  DST_OFFSET = 3600;

// ----- Display layout constants (computed in setup()) -----
int SCREEN_W = 320; // tft.width() after init
int SCREEN_H = 170; // tft.height() after init

// Top small ticker band = 20% of screen height (rounded)
int TOP_BAND_H;

// clock globals (non-const so other .cpp files can link to them)
uint8_t clockTextSize     = 3;    // size of the clock font
uint8_t clockTextPaddingY = 6;    // vertical padding inside that band
uint8_t clockBandHeight   = 20;   // total height of the clear-and-draw area
int     clockX            = 4;    // horizontal offset for the clock text
int     clockYOffset      = -2;   // optional Y nudge


// Graph / UI layout (computed in setup)
int graphX, graphY, graphW, graphH;     // middle-right graph area
int leftBoxX, leftBoxY, leftBoxW, leftBoxH; // middle-left stat area

// ----- Scrolling small ticker (top 20%) -----
const uint8_t scrollSmallTextSize = 3; // same as clock default; changeable
int scrollSmallY;                      // computed from TOP_BAND_H
int scrollSmallX;                      // current X position for small ticker
int scrollSmallSpeed = 3;              // pixels per tick (smaller = slower)
const unsigned long smallScrollInterval = 40; // ms between small-ticker frame updates
unsigned long lastSmallScrollMs = 0;

// ----- Main (large) scrolling marquee (optional) -----
// If you keep the big marquee from v4, keep these. Otherwise, leave empty.
const uint8_t textSizeScale = 6;
int16_t textBoxX, textBoxY;
uint16_t textW, textH;
int16_t scrollX_main, scrollY_main;
unsigned long lastMainScrollMs = 0;
const unsigned long mainScrollInterval = 40;

// ----- Scrolling messages (String so we can update msgs[1] with weather) -----
String msg0 = "Good things are coming";
String msg1 = "Loading weather...";
String msg2 = "Check USAJobs->EB->GovConnect->MyCAA too!";
String msgs[] = { msg0, msg1, msg2 };
const uint8_t MSG_COUNT = sizeof(msgs) / sizeof(msgs[0]);
uint8_t currentMsg = 0;
String curMsg = msgs[0];

// ----- Graph rotation & scheduling (millis-based) -----
unsigned long lastWeatherCheckMs = 0;
const unsigned long WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL; // 10 minutes
unsigned long lastGraphSwitchMs = 0;
const unsigned long GRAPH_SWITCH_MS = 2UL * 60UL * 1000UL;     // 2 minutes
int graphIndex = 0;
const int NUM_GRAPHS = 2; // 0=temp, 1=wind (extend later)

// ----- Clock update scheduling (we update chars efficiently) -----
unsigned long lastClockUpdateMs = 0;
unsigned long clockUpdateIntervalMs = 500; // check twice/sec (or 1000ms for once/sec)
String prevClockText = "";

// ----- Prototypes for helper functions (implemented in helper .cpp files) -----
// WeatherUtils.h should provide these:
//   void initWeather(const char* apiKey, const char* cityQuery, unsigned long cacheMillis);
//   String getWeatherReport();
//   bool tryUpdateWeather(unsigned long nowMillis); // returns true if a fetch occurred

// GraphUtils.h should provide:
//   void setGraphArea(int x,int y,int w,int h);
//   void calculateGraphDataFromForecastRaw(); // fill internal graph arrays (9..21)
//   void drawGraph(int graphType); // draws chosen graph into area

// LeftBoxUtils.h should provide:
//   void calculateLeftBoxDataFromForecastRaw();
//   void drawLeftBoxes(int x,int y,int w,int h);

// UIUtils.h should provide drawing helpers:
//   void drawHeaderBox(...), drawLabel(...), drawValue(...)

// TimeUtils.h provides:
//   void initTimeModule(const char* ssid, const char* pwd, long gmtOffset, int dstOffset);
//   String getTimeString();
//   bool getLocalTime(struct tm* out); // if needed by helpers

// ----- Setup: initialize peripherals, layout, modules -----
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("=== WeatherStation V5 BOOT ===");

  // init time (also connects Wi-Fi inside TimeUtils)
  Serial.println("Init time module (this connects Wi-Fi)...");
  initTimeModule(WIFI_SSID, WIFI_PASSWORD, GMT_OFFSET, DST_OFFSET);
  Serial.println("Time init done.");

  // initialize display
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
  tft.init(170, 320);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  SCREEN_W = tft.width();
  SCREEN_H = tft.height();
  Serial.printf("Screen W=%d H=%d\n", SCREEN_W, SCREEN_H);

  // compute layout
  TOP_BAND_H = (int)(SCREEN_H * 0.20); // top 20%
  //scrollSmallY = (TOP_BAND_H - (int)(8 * scrollSmallTextSize)) / 2; // Commented out for better scrollSmallY calculated below. That way text scroll doesnt scroll 3-4 lines, only 1 using text bounds ||  If you later change the size, this still works.

    // compute scrollSmallY more robustly using text bounds
  tft.setTextSize(scrollSmallTextSize);
  tft.setTextWrap(false); // ensure no wrapping when we measure
  // measure an approximate character height using "Mg" as sample
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  tft.getTextBounds("Mg", 0, 0, &tbx, &tby, &tbw, &tbh);
  // center vertically inside the top band
  scrollSmallY = max(0, (TOP_BAND_H - (int)tbh) / 2);
  scrollSmallX = SCREEN_W; // start from right edge


  // graph area (middle-right): leave left column for stat boxes
  leftBoxW = 80; // px for tiles (tweak as desired)
  leftBoxX = 4;
  leftBoxY = TOP_BAND_H + 4;
  leftBoxH = SCREEN_H - TOP_BAND_H - clockBandHeight - 8;

  graphX = leftBoxX + leftBoxW + 8;
  graphY = TOP_BAND_H + 4;
  graphW = SCREEN_W - graphX - 6;
  graphH = leftBoxH;

  // clock baseline stays at bottom-left (we keep user-friendly Y offset)
  // (clock drawing will compute cursor Y dynamically)

  // initialize RGB strip (unchanged)
  strip.begin();

  // initialize weather module (cache 10 minutes)
  initWeather(OPENWEATHER_KEY, "Groton,CT,US", WEATHER_REFRESH_MS);

  //Maybe add LAT+LON For better and more precise weather


  // Seed msgs[1] with current cached weather summary
  msgs[1] = getWeatherReport();
  curMsg = msgs[currentMsg];

  // Let graph module know where to draw
  setGraphArea(graphX, graphY, graphW, graphH);

  // Try an immediate forecast fetch (non-blocking via tryUpdateWeather or forced)
  if (tryUpdateWeather(millis())) {
    // If fetch occurred, update graph/boxes now from cached forecast
    calculateGraphDataFromForecastRaw(); // implemented in GraphUtils
    calculateLeftBoxDataFromForecastRaw(); // implemented in LeftBoxUtils
    msgs[1] = getWeatherReport(); // refresh ticker string
  } else {
    // If no fetch happened (cache valid), still attempt to populate graph data from previously cached forecast
    calculateGraphDataFromForecastRaw();
    calculateLeftBoxDataFromForecastRaw();
  }

  // initial render: clear UI areas and draw initial static elements
  tft.fillScreen(ST77XX_BLACK);
  // draw top small-band background
  tft.fillRect(0, 0, SCREEN_W, TOP_BAND_H, ST77XX_BLACK);
  // draw left boxes outline
  drawLeftBoxes(leftBoxX, leftBoxY, leftBoxW, leftBoxH);
  // draw initial graph
  drawGraph(graphIndex);
  // draw initial clock (will be updated in loop)
  // (we rely on the optimized clock drawing helper we used in v4)
  Serial.println("Setup complete. Entering loop.");
}

// ----- loop: orchestrate tasks via millis() ----- 
void loop() {
  unsigned long now = millis();

  // 1) Weather refresh check (guarded inside tryUpdateWeather)
  if (now - lastWeatherCheckMs >= WEATHER_REFRESH_MS) {
    lastWeatherCheckMs = now;
    bool fetched = tryUpdateWeather(now); // returns true if a network fetch actually performed
    if (fetched) {
      // update graph and leftboxes from new forecast cache
      calculateGraphDataFromForecastRaw();
      calculateLeftBoxDataFromForecastRaw();
      // update ticker textual message
      msgs[1] = getWeatherReport();
      // optionally force the ticker to restart to show new text immediately:
      if (currentMsg == 1) scrollSmallX = SCREEN_W;
    }
  }

  // 2) Graph rotation (every 2 minutes)
  if (now - lastGraphSwitchMs >= GRAPH_SWITCH_MS) {
    lastGraphSwitchMs = now;
    graphIndex = (graphIndex + 1) % NUM_GRAPHS;
    // redraw graph & left boxes when graph switches
    drawGraph(graphIndex);
    drawLeftBoxes(leftBoxX, leftBoxY, leftBoxW, leftBoxH);
  }

    // 3) Small top ticker update (runs frequently; use smallScrollInterval)
  if (now - lastSmallScrollMs >= smallScrollInterval) {
    lastSmallScrollMs = now;

    // Only clear the top band area (reduce flicker)
    tft.fillRect(0, 0, SCREEN_W, TOP_BAND_H, ST77XX_BLACK);

    // Draw small ticker text at configured small size
    tft.setTextSize(scrollSmallTextSize);
    tft.setTextWrap(false);                 // IMPORTANT: disable wrapping so text stays on a single line
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK); // foreground, background (helps remove artifacts)

    // Use current msgs[1] (weather) as the ticker
    tft.setCursor(scrollSmallX, scrollSmallY);
    tft.print(msgs[1]);

    // Advance X and wrap when off-screen: measure pixel width of current message
    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds(msgs[1].c_str(), 0, 0, &bx, &by, &bw, &bh);
    scrollSmallX -= scrollSmallSpeed;
    if (scrollSmallX < - (int)bw) {
      scrollSmallX = SCREEN_W;
    }

    // Restore main text size (if other code expects it)
    tft.setTextSize(textSizeScale);
  }


  // 4) Optionally: main marquee & LED crossfade handling (unchanged from v4)
  //  - reuse your existing RGB crossfade code here
  //  - reuse your main marquee code if you still want a large scrolling message

  // 5) Clock: update efficiently (only when time string changes or at interval)
  if (now - lastClockUpdateMs >= clockUpdateIntervalMs) {
    lastClockUpdateMs = now;
    String nowTime = getTimeString(); // from TimeUtils (12hr + AM/PM as you added)
    if (nowTime != prevClockText) {
      // draw clock using the robust clearing method (centered in band)
      // Implement a helper function in UIUtils or in main (you already have this in v4)
      drawClockBottom(nowTime); // expected helper - see comment below
      prevClockText = nowTime;
    }
  }

  // 6) Yield / short delay if desired (avoid busy looping)
  delay(1);
}

/* -------------------------------------------------------------------------
   Helper notes (where to implement functions in modules)
   -------------------------------------------------------------------------
   - WeatherUtils
       void initWeather(const char* apiKey, const char* cityQuery, unsigned long cacheMillis);
       String getWeatherReport();       // single-line summary for small ticker
       bool tryUpdateWeather(unsigned long nowMillis); // performs fetch if cache expired

   - GraphUtils
       void setGraphArea(int x,int y,int w,int h);
       void calculateGraphDataFromForecastRaw(); // reads cached forecast in WeatherUtils (or WeatherUtils returns JSON)
       void drawGraph(int graphType);            // draws graph into area set by setGraphArea()

   - LeftBoxUtils
       void calculateLeftBoxDataFromForecastRaw(); // pull current values (temp, wind, humidity) and format
       void drawLeftBoxes(int x,int y,int w,int h);

   - UIUtils
       void drawClockBottom(const String &timeStr); // draw clock at bottom using robust clearing
       void drawBox(...) / drawLabel(...)

   Implementation tip:
   - WeatherUtils can store the raw JSON forecast in an internal StaticJsonDocument when it fetches,
     and GraphUtils / LeftBoxUtils can call WeatherUtils to get that JSON or WeatherUtils can export
     a small API like `getForecastPoint(i)` or `fillForecastBuffer(...)`.
   - Keep the module boundaries minimal: Weather = fetching + caching, Graph = math + rendering,
     LeftBox = format + rendering, UI = minor drawing helpers.
------------------------------------------------------------------------- */
