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
  _update();
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