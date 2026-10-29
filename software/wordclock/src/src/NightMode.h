#pragma once

#include <Arduino.h>

// Dims or switches off the display during a time slot, chosen per day of the
// week. Runs on the clock itself, without Home Assistant.
struct NightMode
{
  bool enabled = false;
  // Minutes since midnight. The slot spans midnight when start > end.
  uint16_t start = 23 * 60;
  uint16_t end = 7 * 60;
  // Days on which the slot starts, bit 0 = Monday ... bit 6 = Sunday.
  uint8_t days = 0x7F;
  // Display brightness during the slot, in percent (0 = off).
  uint8_t brightness = 0;

  // Whether the slot is active. weekday: 0 = Sunday ... 6 = Saturday.
  bool isActive(uint32_t secondsOfDay, uint8_t weekday) const
  {
    if (!enabled)
    {
      return false;
    }
    uint16_t minute = secondsOfDay / 60;
    // weekday 0 (Sunday) is bit 6.
    uint8_t today = (weekday + 6) % 7;
    uint8_t yesterday = (today + 6) % 7;
    if (start <= end)
    {
      return (days & (1 << today)) && minute >= start && minute < end;
    }
    return ((days & (1 << today)) && minute >= start) || ((days & (1 << yesterday)) && minute < end);
  }
};
