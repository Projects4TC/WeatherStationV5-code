#ifndef WEATHERUTILS_H
#define WEATHERUTILS_H

#include <Arduino.h>

/*
  WeatherUtils - header for fetching & caching OpenWeather 3-hour forecast
  Exposes:
    - initWeather(apiKey, cityQuery, cacheMillis)
    - getWeatherReport() -> short one-line summary for ticker
    - tryUpdateWeather(nowMillis) -> returns true if a network fetch occurred
    - fetchForecastNow() -> forces a forecast fetch now (returns true on success)
    - getCachedForecastRaw() -> returns the raw JSON payload (empty if none)
*/

void initWeather(const char* apiKey, const char* cityQuery, unsigned long cacheMillis);
String getWeatherReport();
bool tryUpdateWeather(unsigned long nowMillis);
bool fetchForecastNow();                 // force fetch now (uses HTTP)
String getCachedForecastRaw();           // returns raw cached JSON payload (may be "")

#endif // WEATHERUTILS_H
