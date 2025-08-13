#include "UIUtils.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>

// Externs (these are defined in your main sketch)
extern Adafruit_ST7789 tft;
extern int SCREEN_W;
extern int SCREEN_H;
extern uint8_t clockTextSize;
extern uint8_t clockTextPaddingY;
extern uint8_t clockBandHeight;

extern int clockX;
extern int clockYOffset;

// Draw the clock neatly in the bottom-left.
// Clears a band area using clockBandHeight and clockTextPaddingY and then prints the provided string.
void drawClockBottom(const String &timeStr) {
  // Compute Y baseline for text
  int bandTop = SCREEN_H - clockBandHeight;
  // Clear the whole band
  tft.fillRect(0, bandTop, SCREEN_W, clockBandHeight, ST77XX_BLACK);

  // Draw a faint divider line
  tft.drawFastHLine(0, bandTop, SCREEN_W, ST77XX_WHITE);

  // Set text properties and draw
  tft.setTextSize(clockTextSize);
  tft.setTextColor(ST77XX_CYAN);
  // Compute Y cursor: bandTop + padding + optional offset
  int cursorY = bandTop + clockTextPaddingY + clockYOffset;
  if (cursorY < 0) cursorY = 0;
  if (cursorY > SCREEN_H - 8) cursorY = SCREEN_H - 8;

  tft.setCursor(clockX, cursorY);
  tft.print(timeStr);

  // If you want to show a small timezone or AM/PM indicator elsewhere, add here.
}
