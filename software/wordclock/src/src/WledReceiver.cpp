#include "logging.h"
#include "WledReceiver.h"

#include "ClockFace.h"

// Letters, then the four corners.
static const size_t kFramePixels = NEOPIXEL_ROWS * NEOPIXEL_COLUMNS + 4;
// Packets handled per port and loop, so a flood cannot stall the clock.
static const int kMaxPacketsPerLoop = 8;

// DDP header (http://www.3waylabs.com/ddp/): flags, sequence, data type,
// destination id, 32-bit offset in bytes and 16-bit length, big endian.
// A timecode adds 4 bytes.
static const size_t kDdpHeaderSize = 10;
static const uint8_t kDdpVersionMask = 0xC0;
static const uint8_t kDdpVersion1 = 0x40;
static const uint8_t kDdpTimecodeFlag = 0x10;
static const uint8_t kDdpReplyFlag = 0x04;
static const uint8_t kDdpQueryFlag = 0x02;
static const uint8_t kDdpPushFlag = 0x01;
// Data type bits 3-5: 3 is RGBW, anything else is taken as RGB.
static const uint8_t kDdpTypeRgbw = 3;
// Destination ids from 246 up are configuration, status and DMX.
static const uint8_t kDdpFirstControlId = 246;

// First byte of a WLED UDP packet.
enum WledProtocol : uint8_t
{
  WLED_SYNC = 0,
  WLED_WARLS = 1,
  WLED_DRGB = 2,
  WLED_DRGBW = 3,
  WLED_DNRGB = 4,
};
// Realtime packets: second byte, seconds to keep the frame. 255 keeps it until
// the next stream or the display is switched off, 0 returns to the clock.
static const uint8_t kWledTimeoutForever = 255;
// Sync notification offsets.
static const size_t kSyncBrightness = 2;
static const size_t kSyncRed = 3;
static const size_t kSyncWhite = 10;
static const size_t kSyncVersion = 11;
static const size_t kSyncGroups = 36;
// Sync groups were added in packet version 9. The clock is in group 1.
static const uint8_t kSyncGroupsVersion = 9;
static const uint8_t kSyncGroup = 0x01;

void WledReceiver::begin()
{
  if (begun_)
  {
    return;
  }
  // The sockets stay bound across WiFi reconnections.
  begun_ = ddp_udp_.begin(DDP_PORT) && wled_udp_.begin(WLED_UDP_PORT);
  DLOGLN(begun_ ? "WLED receiver listening" : "WLED receiver failed to open its ports");
}

void WledReceiver::loop()
{
  if (!begun_)
  {
    return;
  }
  read_(ddp_udp_, true);
  read_(wled_udp_, false);
}

void WledReceiver::setRealtimeEnabled(bool enabled)
{
  realtime_enabled_ = enabled;
  if (!enabled)
  {
    display_->stopRealtime();
  }
}

void WledReceiver::read_(WiFiUDP &udp, bool ddp)
{
  for (int packet = 0; packet < kMaxPacketsPerLoop; packet++)
  {
    int size = udp.parsePacket();
    if (size <= 0)
    {
      return;
    }
    int length = udp.read(buffer_, sizeof(buffer_));
    udp.flush();
    if (length <= 0)
    {
      continue;
    }
    if (ddp)
    {
      handleDdp_(buffer_, length);
    }
    else
    {
      handleWled_(buffer_, length);
    }
  }
}

void WledReceiver::startFrame_()
{
  if (display_->getMode() != Display::REALTIME || frame_.size() != kFramePixels)
  {
    frame_.assign(kFramePixels, RgbColor(0));
  }
}

void WledReceiver::setPixel_(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
  if (index < frame_.size())
  {
    // The LEDs have no white channel: add it to the colors.
    frame_[index] = RgbColor(min(r + w, 255), min(g + w, 255), min(b + w, 255));
  }
}

void WledReceiver::handleDdp_(const uint8_t *data, size_t length)
{
  if (!realtime_enabled_ || length < kDdpHeaderSize)
  {
    return;
  }
  uint8_t flags = data[0];
  if ((flags & kDdpVersionMask) != kDdpVersion1 || (flags & (kDdpQueryFlag | kDdpReplyFlag)) ||
      data[3] >= kDdpFirstControlId)
  {
    return;
  }
  size_t header = kDdpHeaderSize + ((flags & kDdpTimecodeFlag) ? 4 : 0);
  uint32_t offset = (uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7];
  size_t dataLength = (size_t)data[8] << 8 | data[9];
  if (length < header || length - header < dataLength)
  {
    return;
  }
  size_t channels = ((data[2] >> 3) & 0x07) == kDdpTypeRgbw ? 4 : 3;

  startFrame_();
  const uint8_t *pixels = data + header;
  size_t index = offset / channels;
  for (size_t i = 0; i + channels <= dataLength; i += channels, index++)
  {
    setPixel_(index, pixels[i], pixels[i + 1], pixels[i + 2], channels == 4 ? pixels[i + 3] : 0);
  }
  // A frame split over several packets shows with the last one.
  if (flags & kDdpPushFlag)
  {
    display_->showRealtime(frame_, DDP_TIMEOUT);
  }
}

void WledReceiver::handleWled_(const uint8_t *data, size_t length)
{
  if (length < 2)
  {
    return;
  }
  uint8_t protocol = data[0];
  if (protocol == WLED_SYNC)
  {
    handleSync_(data, length);
    return;
  }
  if (!realtime_enabled_ || protocol > WLED_DNRGB)
  {
    return;
  }
  uint8_t timeout = data[1];
  if (timeout == 0)
  {
    display_->stopRealtime();
    return;
  }

  startFrame_();
  switch (protocol)
  {
  case WLED_WARLS:
    for (size_t i = 2; i + 3 < length; i += 4)
    {
      setPixel_(data[i], data[i + 1], data[i + 2], data[i + 3]);
    }
    break;
  case WLED_DRGB:
    for (size_t i = 2, index = 0; i + 2 < length; i += 3, index++)
    {
      setPixel_(index, data[i], data[i + 1], data[i + 2]);
    }
    break;
  case WLED_DRGBW:
    for (size_t i = 2, index = 0; i + 3 < length; i += 4, index++)
    {
      setPixel_(index, data[i], data[i + 1], data[i + 2], data[i + 3]);
    }
    break;
  case WLED_DNRGB:
    if (length < 4)
    {
      return;
    }
    for (size_t i = 4, index = (size_t)data[2] << 8 | data[3]; i + 2 < length; i += 3, index++)
    {
      setPixel_(index, data[i], data[i + 1], data[i + 2]);
    }
    break;
  }
  display_->showRealtime(frame_, timeout == kWledTimeoutForever ? 0 : timeout * 1000UL);
}

void WledReceiver::handleSync_(const uint8_t *data, size_t length)
{
  if (!sync_enabled_ || !sync_callback_ || length <= kSyncVersion)
  {
    return;
  }
  if (data[kSyncVersion] >= kSyncGroupsVersion && length > kSyncGroups &&
      !(data[kSyncGroups] & kSyncGroup))
  {
    return;
  }
  uint8_t brightness = data[kSyncBrightness];
  uint8_t r = data[kSyncRed], g = data[kSyncRed + 1], b = data[kSyncRed + 2];
  if (r == 0 && g == 0 && b == 0)
  {
    // White only, on RGBW strips.
    r = g = b = data[kSyncWhite];
  }
  RgbColor color(r * brightness / 255, g * brightness / 255, b * brightness / 255);
  sync_callback_(brightness > 0, color);
}
