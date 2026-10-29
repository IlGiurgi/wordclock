#include "EventLog.h"

#include <Preferences.h>
#include <esp_timer.h>
#include <time.h>

#define EVENT_LOG_NAMESPACE "wordclock_log"
#define EVENT_LOG_KEY "events"
// Below this, the system clock was never set from NTP.
#define EVENT_LOG_MIN_EPOCH 1700000000
#define EVENT_LOG_REPEAT_SECONDS 600

namespace
{
struct Event
{
  uint32_t epoch;  // Unix time, 0 when unknown.
  uint32_t uptime; // Seconds since boot.
  char text[48];
};

struct Log
{
  uint8_t next; // Index of the next event to write.
  uint8_t count;
  Event events[EVENT_LOG_SIZE];
} eventLog_;

uint32_t uptimeSeconds()
{
  return esp_timer_get_time() / 1000000;
}
} // namespace

void eventLogBegin()
{
  Preferences prefs;
  prefs.begin(EVENT_LOG_NAMESPACE, true /* readOnly */);
  if (prefs.getBytes(EVENT_LOG_KEY, &eventLog_, sizeof(eventLog_)) != sizeof(eventLog_) ||
      eventLog_.next >= EVENT_LOG_SIZE || eventLog_.count > EVENT_LOG_SIZE)
  {
    memset(&eventLog_, 0, sizeof(eventLog_));
  }
  prefs.end();
}

void eventLog(const String &text)
{
  uint32_t uptime = uptimeSeconds();
  if (eventLog_.count > 0)
  {
    const Event &last = eventLog_.events[(eventLog_.next + EVENT_LOG_SIZE - 1) % EVENT_LOG_SIZE];
    if (strncmp(text.c_str(), last.text, sizeof(last.text) - 1) == 0 && last.uptime <= uptime && uptime - last.uptime < EVENT_LOG_REPEAT_SECONDS)
    {
      return;
    }
  }

  Event &event = eventLog_.events[eventLog_.next];
  time_t now = time(nullptr);
  event.epoch = now >= EVENT_LOG_MIN_EPOCH ? now : 0;
  event.uptime = uptime;
  strlcpy(event.text, text.c_str(), sizeof(event.text));
  eventLog_.next = (eventLog_.next + 1) % EVENT_LOG_SIZE;
  if (eventLog_.count < EVENT_LOG_SIZE)
  {
    eventLog_.count++;
  }

  Preferences prefs;
  prefs.begin(EVENT_LOG_NAMESPACE, false /* readOnly */);
  prefs.putBytes(EVENT_LOG_KEY, &eventLog_, sizeof(eventLog_));
  prefs.end();
}

String eventLogText()
{
  String result;
  for (int i = 0; i < eventLog_.count; i++)
  {
    const Event &event = eventLog_.events[(eventLog_.next + EVENT_LOG_SIZE - eventLog_.count + i) % EVENT_LOG_SIZE];
    char line[100];
    if (event.epoch > 0)
    {
      time_t epoch = event.epoch;
      struct tm timeinfo;
      localtime_r(&epoch, &timeinfo);
      strftime(line, sizeof(line), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    else
    {
      strlcpy(line, "time unknown", sizeof(line));
    }
    result += String(line) + " (uptime " + String(event.uptime) + " s) " + event.text + "\n";
  }
  return result;
}
