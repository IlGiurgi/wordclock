#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <functional>
#include <vector>

#include "Display.h"

// UDP ports of DDP and of WLED (sync notifications and realtime protocols).
#define DDP_PORT 4048
#define WLED_UDP_PORT 21324
// A DDP stream returns to the clock after this long without a frame, in ms,
// like WLED's default realtime timeout.
#define DDP_TIMEOUT 2500
// Largest UDP payload read, the rest of a bigger packet is dropped.
#define WLED_RECEIVER_BUFFER_SIZE 1472

// Lets WLED, LedFx, xLights or Hyperion drive the letters, and the clock
// follow the on/off state and color of other WLED devices:
// - DDP on port 4048 (WLED "virtual DDP" output, LedFx, xLights).
// - WLED realtime UDP on port 21324: WARLS, DRGB, DRGBW and DNRGB.
// - WLED sync notifications on port 21324, sync group 1.
// Streamed pixels are the 11x10 letters row by row from the top left (110),
// then the four corners clockwise from the top left (114 in all).
// Call loop() from the main loop: everything runs there.
class WledReceiver
{
public:
  // Called for a sync notification: whether the sender is on, and its
  // color scaled to its brightness.
  typedef std::function<void(bool on, RgbColor color)> SyncCallback;

  explicit WledReceiver(Display *display) : display_(display) {}

  // Opens the UDP ports. Call once WiFi is connected, later calls do nothing.
  void begin();
  void loop();

  void setRealtimeEnabled(bool enabled);
  bool realtimeEnabled() { return realtime_enabled_; }
  void setSyncEnabled(bool enabled) { sync_enabled_ = enabled; }
  bool syncEnabled() { return sync_enabled_; }
  void onSync(SyncCallback callback) { sync_callback_ = callback; }

private:
  // Reads the pending packets of a port, up to a few per loop.
  void read_(WiFiUDP &udp, bool ddp);
  void handleDdp_(const uint8_t *data, size_t length);
  void handleWled_(const uint8_t *data, size_t length);
  void handleSync_(const uint8_t *data, size_t length);
  // Starts a new frame from black unless a stream is already showing.
  void startFrame_();
  void setPixel_(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);

  Display *display_;
  WiFiUDP ddp_udp_;
  WiFiUDP wled_udp_;
  bool begun_ = false;
  bool realtime_enabled_ = true;
  bool sync_enabled_ = false;
  SyncCallback sync_callback_;
  std::vector<RgbColor> frame_;
  uint8_t buffer_[WLED_RECEIVER_BUFFER_SIZE];
};
