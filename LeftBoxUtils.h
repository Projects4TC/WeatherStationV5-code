#ifndef LEFTBOXUTILS_H
#define LEFTBOXUTILS_H

#include <Arduino.h>

// Calculate/refresh left-box data from the cached forecast JSON
// (reads WeatherUtils::getCachedForecastRaw())
void calculateLeftBoxDataFromForecastRaw();

// Draw the left boxes into the provided rectangle (x,y,w,h).
// The function will divide the area into three stacked boxes and render
// the pre-calculated values. If data is missing, it will render 'N/A'.
void drawLeftBoxes(int x, int y, int w, int h);

#endif // LEFTBOXUTILS_H
