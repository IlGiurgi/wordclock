#include "logging.h"

#include "Display.h"
#include "ClockFace.h"

static const RgbColor black = RgbColor(0x00, 0x00, 0x00);

Display::Display(ClockFace* clockFace, uint8_t pin)
    : _clockFace(clockFace),
      _pixels(ClockFace::pixelCount(), pin),
      _animations(ClockFace::pixelCount(), NEO_CENTISECONDS),
      _bootAnimations(2) {}

void Display::setup()
{
  _mode = CLOCK;
  _pixels.Begin();
  _brightnessController.setup();
  _mapColumns();
}

void Display::loop()
{
  if (_mode == TICKER) {
    return;
  }
  if (_bootAnimations.IsAnimating()) {
    _bootAnimations.UpdateAnimations();
  } else if (_mode == ANIMATION) {
    unsigned long elapsed = millis() - _animationStartMs;
    if (elapsed >= _animationDurationMs) {
      stopAnimation();
    } else if (millis() - _animationFrameMs >= EFFECT_FRAME_INTERVAL) {
      _animationFrameMs = millis();
      _renderAnimation(elapsed);
    }
  } else if (_mode == MATRIX) {
      if (_matrix_buf.size() >= NEOPIXEL_COLUMNS * NEOPIXEL_ROWS) {
        _pixels.SetPixelColor(_clockFace->mapMinute(ClockFace::TopLeft), black);
        _pixels.SetPixelColor(_clockFace->mapMinute(ClockFace::TopRight), black); 
        _pixels.SetPixelColor(_clockFace->mapMinute(ClockFace::BottomLeft), black); 
        _pixels.SetPixelColor(_clockFace->mapMinute(ClockFace::BottomRight), black);  
        DLOGLN("Updating matrix from arbitray color vector");
        uint16_t indexPixel = 0;
        for (int j = 0; j < NEOPIXEL_ROWS; j++) {
          {
          for (int i = 0; i < NEOPIXEL_COLUMNS; i++)
            {
              if (_matrix_buf.size() >= indexPixel) {
                _pixels.SetPixelColor(_clockFace->map(i, j), _matrix_buf[indexPixel]);
              }
              indexPixel++;
            }
          }
        }
        _matrix_buf.clear();
      }
  } else {
    _animations.UpdateAnimations();
    if (_effect != EFFECT_NONE && !_off && !_animations.IsAnimating() &&
        millis() - _effectFrameMs >= EFFECT_FRAME_INTERVAL)
    {
      _effectFrameMs = millis();
      _renderEffect();
    }
    if (!_off && _brightnessController.hasChanged())
    {
      DLOGLN("Brightness has changed, updating");
      _update(30); // Update in 300 ms
    }
    _brightnessController.loop();
  }
  _show();
}

void Display::_show()
{
  if (!_pixels.IsDirty())
  {
    return;
  }

  // Sum of all channel values, where 255 on one channel draws LED_CHANNEL_MILLIAMPS.
  const uint16_t count = _pixels.PixelCount();
  uint32_t channelSum = 0;
  for (uint16_t index = 0; index < count; index++)
  {
    RgbColor color = _pixels.GetPixelColor(index);
    channelSum += color.R + color.G + color.B;
  }

  const uint32_t channelBudget = (uint32_t)_maxMilliamps * 255 / LED_CHANNEL_MILLIAMPS;
  if (channelSum <= channelBudget)
  {
    _pixels.Show();
    return;
  }

  // Send a scaled copy, then restore the unscaled colors: animations and fades
  // read the buffer back and must not compound the scaling.
  std::vector<RgbColor> unscaled(count);
  for (uint16_t index = 0; index < count; index++)
  {
    RgbColor color = _pixels.GetPixelColor(index);
    unscaled[index] = color;
    _pixels.SetPixelColor(index, RgbColor(color.R * channelBudget / channelSum,
                                          color.G * channelBudget / channelSum,
                                          color.B * channelBudget / channelSum));
  }
  _pixels.Show();
  for (uint16_t index = 0; index < count; index++)
  {
    _pixels.SetPixelColor(index, unscaled[index]);
  }
  // The LEDs already show this frame, scaled.
  _pixels.ResetDirty();
}

void Display::setMaxMilliamps(uint16_t milliamps)
{
  _maxMilliamps = milliamps;
  _pixels.Dirty(); // Resend the current frame with the new budget.
}

void Display::setClockFace(ClockFace* clockface)
{
  DLOGLN("Updating clockface");
  _clockFace = clockface;
  _mapColumns();
  _update();
}

void Display::setColor(const RgbColor &color)
{
  DLOGLN("Updating color");
  _brightnessController.setOriginalColor(color);
}

void Display::setOff()
{
  _off = true;
  if (_mode == ANIMATION)
  {
    _particles.clear();
    _mode = CLOCK;
  }
  _update();
}

void Display::setOn()
{
  _off = false;
  _update();
}

void Display::_update(int animationSpeed)
{
  DLOGLN("Updating display");

  _animations.StopAll();

  // For all the LED animate a change from the current visible state to the new
  // one.
  const std::vector<bool> &state = _clockFace->getState();

  for (int index = 0; index < state.size(); index++)
  {
    RgbColor originalColor = _pixels.GetPixelColor(index);
    RgbColor targetColor = _off ? black : (state[index] ? _litColor(index) : black);

    AnimUpdateCallback animUpdate = [=](const AnimationParam &param) {
      float progress = NeoEase::QuadraticIn(param.progress);
      RgbColor updatedColor = RgbColor::LinearBlend(
          originalColor, targetColor, progress);
      _pixels.SetPixelColor(index, updatedColor);
    };
    _animations.StartAnimation(index, animationSpeed, animUpdate);
  }
}

const char *Display::effectName(Effect effect)
{
  switch (effect)
  {
  case EFFECT_RAINBOW:
    return "rainbow";
  case EFFECT_COLORLOOP:
    return "colorloop";
  case EFFECT_BREATHE:
    return "breathe";
  default:
    return "none";
  }
}

Display::Effect Display::effectFromName(const String &name)
{
  for (int effect = EFFECT_NONE; effect < EFFECT_COUNT; effect++)
  {
    if (name.equalsIgnoreCase(effectName(static_cast<Effect>(effect))))
    {
      return static_cast<Effect>(effect);
    }
  }
  return EFFECT_COUNT;
}

void Display::setEffect(Effect effect)
{
  if (effect >= EFFECT_COUNT || effect == _effect)
  {
    return;
  }
  DLOG("Effect: ");
  DLOGLN(effectName(effect));
  _effect = effect;
  // A playing animation returns to the clock with the new effect.
  if (_mode == CLOCK)
  {
    _update();
  }
}

void Display::_mapColumns()
{
  _pixelColumn.assign(ClockFace::pixelCount(), 0);
  for (int x = 0; x < NEOPIXEL_COLUMNS; x++)
  {
    for (int y = 0; y < NEOPIXEL_ROWS; y++)
    {
      _pixelColumn[_clockFace->map(x, y)] = static_cast<float>(x) / (NEOPIXEL_COLUMNS - 1);
    }
  }
  _pixelColumn[_clockFace->mapMinute(ClockFace::TopRight)] = 1;
  _pixelColumn[_clockFace->mapMinute(ClockFace::BottomRight)] = 1;
}

RgbColor Display::_litColor(uint16_t index)
{
  RgbColor color = _brightnessController.getCorrectedColor();
  unsigned long now = millis();
  switch (_effect)
  {
  case EFFECT_RAINBOW:
  case EFFECT_COLORLOOP:
  {
    // Keep the brightness of the chosen color, only the hue moves.
    float hue = (now % 60000) / 60000.0f;
    if (_effect == EFFECT_RAINBOW)
    {
      hue = (now % 6000) / 6000.0f + _pixelColumn[index];
    }
    return HsbColor(hue - floorf(hue), 1.0f, HsbColor(color).B);
  }
  case EFFECT_BREATHE:
  {
    float wave = 0.5f - 0.5f * cosf(2 * PI * (now % 4000) / 4000.0f);
    return color.Dim(64 + 191 * wave);
  }
  default:
    return color;
  }
}

void Display::_renderEffect()
{
  const std::vector<bool> &state = _clockFace->getState();
  for (uint16_t index = 0; index < state.size(); index++)
  {
    if (state[index])
    {
      _pixels.SetPixelColor(index, _litColor(index));
    }
  }
}

namespace
{
const char *const kHeart[NEOPIXEL_ROWS] = {
    "...........",
    "..XX...XX..",
    ".XXXX.XXXX.",
    ".XXXXXXXXX.",
    ".XXXXXXXXX.",
    "..XXXXXXX..",
    "...XXXXX...",
    "....XXX....",
    ".....X.....",
    "..........."};

const char *const kSmile[NEOPIXEL_ROWS] = {
    "...XXXXX...",
    "..X.....X..",
    ".X.......X.",
    "X..X...X..X",
    "X..X...X..X",
    "X.........X",
    "X.X.....X.X",
    ".X.XXXXX.X.",
    "..X.....X..",
    "...XXXXX..."};

// Same smiley with the right eye closed.
const char *const kWink[NEOPIXEL_ROWS] = {
    "...XXXXX...",
    "..X.....X..",
    ".X.......X.",
    "X..X......X",
    "X..X..XXX.X",
    "X.........X",
    "X.X.....X.X",
    ".X.XXXXX.X.",
    "..X.....X..",
    "...XXXXX..."};

const char *const kStar[NEOPIXEL_ROWS] = {
    ".....X.....",
    ".....X.....",
    "....XXX....",
    "XXXXXXXXXXX",
    ".XXXXXXXXX.",
    "..XXXXXXX..",
    "...XXXXX...",
    "..XXX.XXX..",
    ".XX.....XX.",
    "XX.......XX"};

// 7x8 ghost, the last row alternates to make it wiggle. 'W' is the white of
// the eyes, 'B' the pupils.
const char *const kGhost[] = {
    "..XXX..",
    ".XXXXX.",
    "XWWXWWX",
    "XWBXWBX",
    "XXXXXXX",
    "XXXXXXX",
    "XXXXXXX",
    "X.X.X.X",
    ".X.X.X."};
} // namespace

const char *Display::animationName(Animation animation)
{
  switch (animation)
  {
  case ANIMATION_HEART:
    return "heart";
  case ANIMATION_SMILE:
    return "smile";
  case ANIMATION_STAR:
    return "star";
  case ANIMATION_MATRIX:
    return "matrix";
  case ANIMATION_FIREWORKS:
    return "fireworks";
  case ANIMATION_SNOW:
    return "snow";
  case ANIMATION_BOOT:
    return "boot";
  case ANIMATION_RAIN:
    return "rain";
  case ANIMATION_PACMAN:
    return "pacman";
  case ANIMATION_GHOST:
    return "ghost";
  case ANIMATION_SUNRISE:
    return "sunrise";
  default:
    return "";
  }
}

Display::Animation Display::animationFromName(const String &name)
{
  for (int animation = 0; animation < ANIMATION_COUNT; animation++)
  {
    if (name.equalsIgnoreCase(animationName(static_cast<Animation>(animation))))
    {
      return static_cast<Animation>(animation);
    }
  }
  return ANIMATION_COUNT;
}

void Display::playAnimation(Animation animation, unsigned long durationMs)
{
  if (animation >= ANIMATION_COUNT || _off || (_mode != CLOCK && _mode != ANIMATION))
  {
    return;
  }
  DLOG("Animation: ");
  DLOGLN(animationName(animation));
  _animations.StopAll();
  _mode = ANIMATION;
  _animation = animation;
  _animationStartMs = millis();
  _animationDurationMs = durationMs > 0 ? durationMs
                         : animation == ANIMATION_SUNRISE ? SUNRISE_DURATION
                                                          : ANIMATION_DURATION;
  _animationFrameMs = 0;
  _particles.clear();
  _burstRadius = -1;
  _pixels.ClearTo(black);
  if (animation == ANIMATION_BOOT)
  {
    // The real thing, which returns to the clock when done.
    runBootAnimation();
  }
}

void Display::stopAnimation()
{
  if (_mode != ANIMATION)
  {
    return;
  }
  _particles.clear();
  _mode = CLOCK;
  _update(50);
}

void Display::_setGridPixel(int x, int y, RgbColor color)
{
  if (x >= 0 && x < NEOPIXEL_COLUMNS && y >= 0 && y < NEOPIXEL_ROWS)
  {
    _pixels.SetPixelColor(_clockFace->map(x, y), color);
  }
}

void Display::_drawBitmap(const char *const *rows, RgbColor color)
{
  for (int y = 0; y < NEOPIXEL_ROWS; y++)
  {
    for (int x = 0; x < NEOPIXEL_COLUMNS; x++)
    {
      _setGridPixel(x, y, rows[y][x] == 'X' ? color : black);
    }
  }
}

void Display::_renderAnimation(unsigned long elapsedMs)
{
  // Follow the chosen brightness, but stay visible when the clock is very dim.
  const float brightness = max(HsbColor(_brightnessController.getCorrectedColor()).B, 0.12f);
  auto hsb = [brightness](float hue, float saturation, float value) {
    return RgbColor(HsbColor(hue - floorf(hue), saturation, value * brightness));
  };

  switch (_animation)
  {
  case ANIMATION_HEART:
  {
    // Two quick beats per second, like a heartbeat.
    float t = (elapsedMs % 1000) / 1000.0f;
    float beat = max(expf(-powf((t - 0.1f) * 12, 2)), 0.7f * expf(-powf((t - 0.35f) * 12, 2)));
    _drawBitmap(kHeart, hsb(0, 1, 0.45f + 0.55f * beat));
    break;
  }
  case ANIMATION_SMILE:
  {
    // Wink for 300 ms every 2.5 seconds.
    bool wink = elapsedMs % 2500 > 2200;
    _drawBitmap(wink ? kWink : kSmile, hsb(0.15f, 1, 1));
    break;
  }
  case ANIMATION_STAR:
  {
    _drawBitmap(kStar, black);
    for (int y = 0; y < NEOPIXEL_ROWS; y++)
    {
      for (int x = 0; x < NEOPIXEL_COLUMNS; x++)
      {
        if (kStar[y][x] == 'X')
        {
          // Each pixel twinkles with its own phase.
          float phase = (elapsedMs / 700.0f) + (x * 7 + y * 13) * 0.37f;
          float twinkle = 0.55f + 0.45f * sinf(phase * 2 * PI);
          _setGridPixel(x, y, hsb(0.12f, 0.85f, twinkle));
        }
      }
    }
    break;
  }
  case ANIMATION_MATRIX:
  {
    _fadeAll(40);
    if (_particles.size() < 7 && random(4) == 0)
    {
      _particles.push_back({static_cast<float>(random(NEOPIXEL_COLUMNS)), 0, random(15, 40) / 100.0f, 0});
    }
    for (auto it = _particles.begin(); it != _particles.end();)
    {
      _setGridPixel(it->x, it->y, hsb(0.33f, 0.4f, 1));
      it->y += it->speed;
      it = it->y >= NEOPIXEL_ROWS + 1 ? _particles.erase(it) : it + 1;
    }
    break;
  }
  case ANIMATION_FIREWORKS:
  {
    _fadeAll(25);
    if (_particles.empty())
    {
      // New rocket: x, current height, burst height, hue.
      _particles.push_back({static_cast<float>(random(2, NEOPIXEL_COLUMNS - 2)), NEOPIXEL_ROWS - 1,
                            static_cast<float>(random(2, 5)), random(100) / 100.0f});
      _burstRadius = -1;
    }
    Particle &rocket = _particles.front();
    if (_burstRadius < 0)
    {
      _setGridPixel(rocket.x, rocket.y, hsb(0.1f, 0.3f, 1));
      rocket.y -= 0.5f;
      if (rocket.y <= rocket.speed)
      {
        _burstRadius = 0;
      }
    }
    else
    {
      for (int y = 0; y < NEOPIXEL_ROWS; y++)
      {
        for (int x = 0; x < NEOPIXEL_COLUMNS; x++)
        {
          if (fabs(_distance(x, y, rocket.x, rocket.y) - _burstRadius) < 0.6)
          {
            _setGridPixel(x, y, hsb(rocket.hue + random(10) / 100.0f, 1, 1 - _burstRadius / 6));
          }
        }
      }
      _burstRadius += 0.35f;
      if (_burstRadius > 5)
      {
        _particles.clear();
      }
    }
    break;
  }
  case ANIMATION_SNOW:
  {
    _fadeAll(60);
    if (_particles.size() < 12 && random(3) == 0)
    {
      _particles.push_back({static_cast<float>(random(NEOPIXEL_COLUMNS)), 0, random(5, 15) / 100.0f, 0});
    }
    for (auto it = _particles.begin(); it != _particles.end();)
    {
      _setGridPixel(roundf(it->x), it->y, hsb(0.6f, 0.15f, 1));
      it->y += it->speed;
      // Drift slowly sideways.
      it->x += (random(3) - 1) * 0.1f;
      it = it->y >= NEOPIXEL_ROWS ? _particles.erase(it) : it + 1;
    }
    break;
  }
  case ANIMATION_RAIN:
  {
    _fadeAll(90);
    if (_particles.size() < 10 && random(2) == 0)
    {
      _particles.push_back({static_cast<float>(random(NEOPIXEL_COLUMNS)), 0, random(40, 70) / 100.0f, 0});
    }
    for (auto it = _particles.begin(); it != _particles.end();)
    {
      _setGridPixel(it->x, it->y, hsb(0.6f, 0.8f, 1));
      it->y += it->speed;
      it = it->y >= NEOPIXEL_ROWS ? _particles.erase(it) : it + 1;
    }
    break;
  }
  case ANIMATION_PACMAN:
  {
    // Crosses the screen every 4 s, chomping every 200 ms.
    float centerX = -4 + (elapsedMs % 4000) / 4000.0f * (NEOPIXEL_COLUMNS + 8);
    const float centerY = 4.5f;
    bool open = (elapsedMs / 200) % 2 == 0;
    _pixels.ClearTo(black);
    for (int x = 0; x < NEOPIXEL_COLUMNS; x += 2)
    {
      if (x > centerX + 2)
      {
        _setGridPixel(x, 4, hsb(0.08f, 0.3f, 0.6f));
        _setGridPixel(x, 5, hsb(0.08f, 0.3f, 0.6f));
      }
    }
    for (int y = 0; y < NEOPIXEL_ROWS; y++)
    {
      for (int x = 0; x < NEOPIXEL_COLUMNS; x++)
      {
        float dx = x - centerX, dy = y - centerY;
        bool inMouth = open && dx > 0 && fabsf(dy) < dx * 0.9f;
        if (dx * dx + dy * dy <= 12.5f && !inMouth)
        {
          _setGridPixel(x, y, hsb(0.15f, 1, 1));
        }
      }
    }
    break;
  }
  case ANIMATION_GHOST:
  {
    // Up to two ghosts in the classic colors, each with its own direction,
    // speed and height, floating up and down.
    static const float kHues[] = {0, 0.9f, 0.5f, 0.08f};
    if (_particles.size() < 2 && random(30) == 0)
    {
      bool toRight = random(2) == 0;
      float speed = random(8, 25) / 100.0f;
      _particles.push_back({toRight ? -7.0f : static_cast<float>(NEOPIXEL_COLUMNS), static_cast<float>(random(0, 3)),
                            toRight ? speed : -speed, kHues[random(4)]});
    }
    _pixels.ClearTo(black);
    bool wiggle = (elapsedMs / 250) % 2 == 0;
    for (auto it = _particles.begin(); it != _particles.end();)
    {
      int left = roundf(it->x);
      int top = roundf(it->y + 0.6f * sinf(elapsedMs / 400.0f + it->hue * 10));
      for (int row = 0; row < 8; row++)
      {
        const char *line = kGhost[row == 7 && wiggle ? 8 : row];
        for (int col = 0; col < 7; col++)
        {
          char c = line[col];
          // The pupils look where the ghost is going.
          if (it->speed < 0 && row == 3 && (col == 1 || col == 4))
            c = 'B';
          else if (it->speed < 0 && row == 3 && (col == 2 || col == 5))
            c = 'W';
          if (c == '.')
            continue;
          _setGridPixel(left + col, top + row, c == 'X' ? hsb(it->hue, 1, 1) : c == 'W' ? hsb(0, 0, 1) : hsb(0.66f, 1, 1));
        }
      }
      it->x += it->speed;
      // Sometimes turn around.
      if (random(150) == 0)
        it->speed = -it->speed;
      bool gone = it->x < -8 || it->x > NEOPIXEL_COLUMNS + 1;
      it = gone ? _particles.erase(it) : it + 1;
    }
    break;
  }
  case ANIMATION_SUNRISE:
  {
    // 20 s from night to day, then daylight for 10 s before starting again.
    float progress = min((elapsedMs % 30000) / 20000.0f, 1.0f);
    // Sky colors at the top and at the horizon, from night to day.
    RgbColor top = RgbColor::LinearBlend(RgbColor(0, 0, 40), RgbColor(40, 110, 255), progress);
    RgbColor horizon = progress < 0.5f
                           ? RgbColor::LinearBlend(RgbColor(40, 0, 10), RgbColor(255, 70, 0), progress * 2)
                           : RgbColor::LinearBlend(RgbColor(255, 70, 0), RgbColor(255, 190, 110), progress * 2 - 1);
    RgbColor sun = RgbColor::LinearBlend(RgbColor(255, 40, 0), RgbColor(255, 230, 150), progress);
    float sunY = NEOPIXEL_ROWS + 2 - progress * 8.5f;
    const uint8_t dim = brightness * 255;
    for (int y = 0; y < NEOPIXEL_ROWS; y++)
    {
      float height = static_cast<float>(NEOPIXEL_ROWS - 1 - y) / (NEOPIXEL_ROWS - 1);
      RgbColor sky = RgbColor::LinearBlend(horizon, top, powf(height, 0.6f));
      for (int x = 0; x < NEOPIXEL_COLUMNS; x++)
      {
        RgbColor color = sky;
        // A few stars fading away at the top.
        if (y < 4 && (x * 7 + y * 3) % 11 == 0 && progress < 0.4f)
          color = RgbColor::LinearBlend(RgbColor(180, 180, 255), sky, progress / 0.4f);
        float sunDistance = sqrtf((x - 5) * (x - 5) + (y - sunY) * (y - sunY));
        if (sunDistance <= 2.3f)
          color = sun;
        else if (sunDistance <= 3.5f)
          color = RgbColor::LinearBlend(color, sun, 0.5f * (3.5f - sunDistance) / 1.2f);
        _setGridPixel(x, y, color.Dim(dim));
      }
    }
    break;
  }
  default:
    break;
  }
}

void Display::updateForTime(int hour, int minute, int second, int animationSpeed)
{
  if (_mode != CLOCK || !_clockFace->stateForTime(hour, minute, second, _show_ampm))
  {
    return; // Nothing to update.
  }

  DLOG("Time: ");
  DLOG(hour);
  DLOG(":");
  DLOGLN(minute);

  _update(animationSpeed);
}

void Display::_circle(uint16_t x, uint16_t y, int radius, RgbColor color)
{
  for (int i = 0; i < 11; i++)
  {
    for (int j = 0; j < 10; j++) {
      double distance = _distance(i, j, x, y);
      if (distance < radius && distance > radius - 2)
      {
        _pixels.SetPixelColor(_clockFace->map(i, j), color);
      }
    }
  }
}

void Display::_colorCornerPixels(RgbColor color)
{
  for (int corner = _clockFace->TopLeft; corner <= _clockFace->TopRight; corner++)
  {
    _pixels.SetPixelColor(_clockFace->mapMinute(static_cast<ClockFace::Corners>(corner)), color);
  }
}

void Display::_circleAnimUpdate(const AnimationParam& param)
{
  if (param.state == AnimationState_Completed)
    {      
      switch(_circleCount) {
        case 0:
          DLOGLN("Testing green");
          _circleCenterX = 0;
          _circleCenterY = 5;
          _circleColor = HtmlColor(0x007f00);
          _bootAnimations.RestartAnimation(param.index);
          break;
        case 1:
          DLOGLN("Testing blue");
          _circleCenterX = 10;
          _circleCenterY = 5;
          _circleColor = HtmlColor(0x00007f);
          _bootAnimations.RestartAnimation(param.index);
          break;
        default:
          return;
      }
      _circleCount++;
  } else {
    float progress = NeoEase::QuarticOut(param.progress);
    int radius = progress * 15;
    _circle(_circleCenterX, _circleCenterY, radius, _circleColor);
    if (progress >= .95f && progress <= .98f) {
      _colorCornerPixels(_circleColor);
    }
  }
}

void Display::_fadeAll(uint8_t darkenBy)
{
    RgbColor color;
    for (uint16_t indexPixel = 0; indexPixel < _pixels.PixelCount(); indexPixel++)
    {
        color = _pixels.GetPixelColor(indexPixel);
        color.Darken(darkenBy);
        _pixels.SetPixelColor(indexPixel, color);
    }
}

void Display::_fadeAnimUpdate(const AnimationParam& param)
{
    if (param.state == AnimationState_Completed)
    {
      if (_bootAnimations.IsAnimating()) 
      {
        // Keep fading as long as the other animation is running.
        _fadeAll(5);
        _bootAnimations.RestartAnimation(param.index);
      }
      else 
      {
        DLOGLN("Boot animation complete");
        _mode = CLOCK;
        _update(100);
      }
    }
}

void Display::runBootAnimation()
{
    DLOGLN("Starting boot animation");
    _mode = BOOT;
    _circleCount = 0;
    DLOGLN("Testing red");
    _circleCenterX = 5;
    _circleCenterY = 10;
    _circleColor = HtmlColor(0x7f0000);
    _bootAnimations.StartAnimation(0, 20, [this](const AnimationParam& param) { _fadeAnimUpdate(param);});
    _bootAnimations.StartAnimation(1, 3000,[this](const AnimationParam& param) { _circleAnimUpdate(param);});
}

void Display::setMatrix(std::vector<RgbColor> colorValues)
{
  _animations.StopAll();
  _mode = MATRIX;
  _clockFace->clearDisplay();
  _matrix_buf = colorValues;
}

void Display::clearMatrix()
{
  _mode = CLOCK;
  _update(30);
}

void Display::_displayCharacter(FontTable fontTable, char character, int scrollPosition, RgbColor color) {
  // Get bit array for this character
  std::vector<bool> charData = FontTable::getCharData(fontTable, character);
  // Iterate through each pixel of the character
  if (charData.size() != fontTable.characterHeight * fontTable.characterWidth) {
    return;
  }
  // Center vertically
  static int offsetY = (NEOPIXEL_ROWS - fontTable.characterHeight) / 2;
  for (int i = 0; i < fontTable.characterHeight; i++) {
    for (int j = 0; j < fontTable.characterWidth; j++) {
      int offsetX = scrollPosition + j;
      // Only account for on-screen pixels
      if (offsetX < NEOPIXEL_COLUMNS && offsetX >= 0) {
        _pixels.SetPixelColor(_clockFace->map(offsetX, offsetY + i), charData[i * fontTable.characterWidth + j] ? color : black);
      } 
    }
  }
}

void Display::scrollText(IotWebConf &iwc, String text, RgbColor textColor, int speed, bool rightToLeft)
{
  DLOGLN("Ticker activated");
  DLOGLN(text);

  const FontTable fontTable = font5x7;  
  const int letterSpacing = 1;
  int textLength = text.length();
  int scrollSpeed = std::min(10000, std::max(10, speed));
  int scrollDirection = rightToLeft ? 1 : -1;

  if (fontTable.characterHeight > NEOPIXEL_ROWS) {
    DLOGLN("Font is too tall to fit the display");
    return;
  }

  _animations.StopAll();
  _mode = TICKER;

  // Calculate total scrolling distance
  const int letterWidth = fontTable.characterWidth + letterSpacing;
  const int totalScrollDistance = letterWidth * textLength + NEOPIXEL_COLUMNS;

  // Starting position
  int scrollPosition = scrollDirection == 1 ? NEOPIXEL_COLUMNS - totalScrollDistance : NEOPIXEL_COLUMNS;

  // Iterate through each pixel of the scrolling text
  for (int i = 0; i <= totalScrollDistance; i++) {
    _pixels.ClearTo(black);

    for (int j = 0; j < textLength; j++) {
      int charPos = scrollPosition + j * letterWidth;
      if (charPos >= -fontTable.characterWidth && charPos < NEOPIXEL_COLUMNS) {
      _displayCharacter(fontTable, text.charAt(j), charPos, textColor);
      }
    }

    // Update the matrix
    _show();

    // Update scroll position
    scrollPosition += scrollDirection;

    // Delay between frames
    iwc.delay(scrollSpeed);
  }

  _mode = CLOCK;
  DLOGLN("Ticker exited");
  _update();
}