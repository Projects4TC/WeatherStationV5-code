#ifndef GRAPH_UTILS_H
#define GRAPH_UTILS_H

#include <Arduino.h>

const int GRAPH_HOURS = 13; // 9..21 inclusive

// Exported arrays (filled by calculateGraphDataFromForecastRaw)
extern float graphTemp[GRAPH_HOURS];   // °F
extern float graphWind[GRAPH_HOURS];   // mph
extern float graphPop[GRAPH_HOURS];    // precipitation probability (0..1)
extern bool  graphValid[GRAPH_HOURS];  // true if a value is present for that hour
extern int   graphHourLabels[GRAPH_HOURS]; // 9..21

// Fills arrays from WeatherUtils cached forecast JSON
bool calculateGraphDataFromForecastRaw(bool smooth = true);

// Graph rendering API
void setGraphArea(int x, int y, int w, int h);
void drawGraph(int graphType); // 0 = temp, 1 = wind, 2 = pop (optional)

#endif // GRAPH_UTILS_H
