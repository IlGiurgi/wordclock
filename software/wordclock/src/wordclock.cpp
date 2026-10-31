//  Copyright 2023 Tom Granger

//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at

//      http://www.apache.org/licenses/LICENSE-2.0

//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
   
#include <Arduino.h>
#include <RTClib.h>
#include <esp_sntp.h>
#include <time.h>
#include "src/logging.h"
#include "src/Display.h"
#include "src/ClockFace.h"
#include "src/Iot.h"
#include "src/EventLog.h"
#include "src/nodo.h" // Nodo stuff
RTC_DS3231 rtc;

// TODO: refactor so we don't need to initialize another clockface here.
#ifdef NODO
auto pos = ClockFace::LightSensorPosition::Top;
#else
auto pos = ClockFace::LightSensorPosition::Bottom;
#endif

EnglishClockFace clockFace(pos);
Display display(&clockFace);

Iot iot(&display, &rtc);

// The time shown is kept by the ESP32 and only re-synced from the RTC every
// RTC_SYNC_INTERVAL ms. Polling the RTC over I2C ten times a second froze the
// displayed time whenever LED noise wedged the bus until a power cycle.
#define RTC_SYNC_INTERVAL 10000
// The DS3231 measures its temperature every 64 s.
#define RTC_TEMPERATURE_INTERVAL 60000
#define RTC_TEMPERATURE_REGISTER 0x11
#define DISPLAY_UPDATE_INTERVAL 100
#define RTC_I2C_ADDRESS 0x68
// Below this, the system clock was never set from NTP.
#define MIN_VALID_EPOCH 1700000000
// On October 31st the Halloween show plays at every full hour in this range.
#define HALLOWEEN_FIRST_HOUR 18
#define HALLOWEEN_LAST_HOUR 23

#ifdef SDA_PIN
#define RTC_SDA SDA_PIN
#define RTC_SCL SCL_PIN
#else
#define RTC_SDA SDA
#define RTC_SCL SCL
#endif

// Last time of day read from the RTC or the system clock, in seconds, and
// the millis() it was read at. Dates are ignored: only the time of day is
// shown.
uint32_t rtc_seconds = 0;
unsigned long rtc_time_ms = 0;
bool rtc_time_valid = false;
// Day of the week from the RTC, 0 = Sunday.
uint8_t rtc_weekday = 0;
// Date of the last reading, month 1-12 and day 1-31.
uint8_t rtc_month = 0;
uint8_t rtc_day = 0;
// Hour the Halloween show last started at, -1 for none.
int halloween_hour = -1;
unsigned long last_rtc_sync = 0;
unsigned long last_display_update = 0;
unsigned long last_temperature_read = 0;

// Frees a slave holding SDA low by clocking SCL, then restarts the bus.
void recoverI2C()
{
  DLOGLN("RTC read failed, recovering I2C bus");
  eventLog("RTC read failed, I2C bus reset");
  Wire.end();
  pinMode(RTC_SDA, INPUT_PULLUP);
  pinMode(RTC_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9; i++)
  {
    digitalWrite(RTC_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(RTC_SCL, HIGH);
    delayMicroseconds(5);
  }
  Wire.begin(RTC_SDA, RTC_SCL);
}

// Current time of day in seconds: the last RTC reading plus the time elapsed since.
uint32_t currentSeconds()
{
  return (rtc_seconds + (millis() - rtc_time_ms) / 1000) % 86400;
}

// Re-reads the RTC, keeping the running time when the read looks wrong.
void syncFromRTC()
{
  Wire.beginTransmission(RTC_I2C_ADDRESS);
  bool busOk = Wire.endTransmission() == 0;
  DateTime now = busOk ? rtc.now() : DateTime((uint32_t)0);
  uint32_t seconds = now.hour() * 3600UL + now.minute() * 60UL + now.second();
  // A frozen RTC returns the same time on every read.
  bool advancing = !rtc_time_valid || seconds != rtc_seconds;
  bool sane = now.hour() < 24 && now.minute() < 60 && now.second() < 60;
  if (busOk && sane && advancing)
  {
    rtc_seconds = seconds;
    rtc_time_ms = millis();
    rtc_time_valid = true;
    rtc_weekday = now.dayOfTheWeek();
    rtc_month = now.month();
    rtc_day = now.day();
    return;
  }
  recoverI2C();
}

// Takes the time from the system clock once NTP has set it. It applies the
// timezone rules on every read, so daylight saving time changes show at once:
// the RTC holds local time and would only follow at the next NTP sync, up to
// an hour later. Returns false when NTP is off or has not synced yet.
bool syncFromSystemClock()
{
  time_t now = time(nullptr);
  if (!sntp_enabled() || now < MIN_VALID_EPOCH)
  {
    return false;
  }
  struct tm local;
  localtime_r(&now, &local);
  rtc_seconds = local.tm_hour * 3600UL + local.tm_min * 60UL + local.tm_sec;
  rtc_time_ms = millis();
  rtc_time_valid = true;
  rtc_weekday = local.tm_wday;
  rtc_month = local.tm_mon + 1;
  rtc_day = local.tm_mday;

  static int last_isdst = -1;
  if (last_isdst >= 0 && local.tm_isdst != last_isdst)
  {
    eventLog(local.tm_isdst > 0 ? "Daylight saving time started" : "Daylight saving time ended");
  }
  last_isdst = local.tm_isdst;
  return true;
}

// Re-reads the time, from the system clock when NTP set it, else the RTC.
void syncTime()
{
  if (!syncFromSystemClock())
  {
    syncFromRTC();
  }
}

// Reads the temperature sensor of the DS3231, NAN when the read fails.
float readRTCTemperature()
{
  Wire.beginTransmission(RTC_I2C_ADDRESS);
  Wire.write(RTC_TEMPERATURE_REGISTER);
  if (Wire.endTransmission() != 0 || Wire.requestFrom((uint8_t)RTC_I2C_ADDRESS, (uint8_t)2) != 2)
  {
    return NAN;
  }
  int8_t degrees = Wire.read();
  uint8_t quarters = Wire.read() >> 6;
  return degrees + quarters * 0.25f;
}

// Starts the Halloween show at the first minute of each evening hour of
// October 31st. playAnimation() skips it when the display is off or busy.
void playHalloween(uint32_t seconds)
{
  int hour = seconds / 3600;
  bool due = rtc_month == 10 && rtc_day == 31 && hour >= HALLOWEEN_FIRST_HOUR &&
             hour <= HALLOWEEN_LAST_HOUR && seconds / 60 % 60 == 0;
  if (due && hour != halloween_hour)
  {
    halloween_hour = hour;
    display.playAnimation(Display::ANIMATION_HALLOWEEN);
    eventLog("Halloween show");
  }
}

void setup()
{
  setupLogging();
  eventLogBegin();
#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);
#endif
#ifdef SDA_PIN
  Wire.begin(SDA_PIN, SCL_PIN);
#endif
  bool result = rtc.begin(); // pins for RTC swapped over on V1.0
  DCHECK(result, "RTC didn't start");
  if (rtc.lostPower())
  {
    DCHECK("RTC lost power. Battery was removed ?");
  }
  syncFromRTC();
  last_rtc_sync = millis();
  iot.setTemperature(readRTCTemperature());
  last_temperature_read = millis();

  display.setup();
  iot.setup();
}

void loop()
{
  unsigned long ts_now = millis();
  if (iot.takeRtcChanged())
  {
    // The new time may equal the last one read: not a frozen RTC.
    bool was_valid = rtc_time_valid;
    rtc_time_valid = false;
    syncTime();
    rtc_time_valid |= was_valid;
    last_rtc_sync = ts_now;
  }
  if (ts_now - last_rtc_sync >= RTC_SYNC_INTERVAL)
  {
    syncTime();
    last_rtc_sync = ts_now;
  }
  if (ts_now - last_temperature_read >= RTC_TEMPERATURE_INTERVAL)
  {
    iot.setTemperature(readRTCTemperature());
    last_temperature_read = ts_now;
  }
  if (rtc_time_valid && ts_now - last_display_update >= DISPLAY_UPDATE_INTERVAL)
  {
    uint32_t seconds = currentSeconds();
    display.updateForTime(seconds / 3600, seconds / 60 % 60, seconds % 60);
    iot.updateNight(seconds, rtc_weekday);
    playHalloween(seconds);
    last_display_update = ts_now;
  }

  display.loop();
  iot.loop();
}
