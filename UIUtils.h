#ifndef UIUTILS_H
#define UIUTILS_H

#include <Arduino.h>

// Draw the clock at the bottom band.
// Accepts a time string (e.g. "09:25:00 AM") and renders it using the
// global clock settings defined in the main sketch (clockTextSize, clockBandHeight, etc.)
void drawClockBottom(const String &timeStr);

#endif // UIUTILS_H
