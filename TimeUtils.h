#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <Arduino.h>

/**
 * Initialize Wi-Fi and NTP time sync.
 * - ssid / password: WiFi credentials
 * - gmtOffset: seconds offset from UTC (e.g. -4*3600 for EDT)
 * - dstOffset: daylight seconds (usually 0 or 3600)
 *
 * This function will attempt to connect to Wi-Fi for a limited time and
 * will attempt to sync NTP time for a limited time. It will not block forever.
 */
void initTimeModule(const char* ssid, const char* password, long gmtOffset, int dstOffset);

/**
 * Return the current local time as a formatted string: "HH:MM:SS AM" or "--:--:--" if not available.
 * Uses 12-hour clock with AM/PM.
 */
String getTimeString();

/**
 * Return true if local time (NTP) is available on the device right now.
 */
bool localTimeAvailable();

#endif // TIME_UTILS_H
