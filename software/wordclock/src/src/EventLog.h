#pragma once

#include <Arduino.h>

// Small log of notable events (resets, NTP, RTC, MQTT...) kept in NVS so it
// survives resets and power losses. Only call it from the main loop.

// Number of events kept, the oldest are dropped first.
#define EVENT_LOG_SIZE 20

// Loads the saved events. Call once at boot before logging.
void eventLogBegin();

// Adds an event, truncated to 47 characters. The same text as the latest
// event is skipped for 10 minutes, so a repeating error does not wear the
// flash or push the other events out.
void eventLog(const String &text);

// All events, oldest first, one per line.
String eventLogText();
