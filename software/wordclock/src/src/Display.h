#pragma once

#include <IotWebConf.h>
#include <NeoPixelAnimator.h>
#include <NeoPixelBrightnessBus.h>

#include "BrightnessController.h"
#include "ClockFace.h"
#include "nodo.h"
#include "Font.h"

// The pin to control the matrix
#ifndef NEOPIXEL_PIN
#define NEOPIXEL_PIN 32
#endif
//
#define TIME_CHANGE_ANIMATION_SPEED 400

// Default current budget for the LEDs, in mA. Frames estimated above it are
// dimmed proportionally so an undersized power supply does not brown out.
#ifndef LED_MAX_MILLIAMPS
#define LED_MAX_MILLIAMPS 1500
#endif

// Frame interval of the lighting effects, in ms.
#define EFFECT_FRAME_INTERVAL 40

// Typical WS2812 draw for one color channel at full duty, in mA.
#ifndef LED_CHANNEL_MILLIAMPS
#define LED_CHANNEL_MILLIAMPS 20
#endif

class Display
{
public:
  Display(ClockFace* clockFace, uint8_t pin = NEOPIXEL_PIN);

  void setup();
  void loop();
  void setColor(const RgbColor &color);
  RgbColor getColor() { return _brightnessController.getOriginalColor(); };
  void setClockFace(ClockFace* clockface);
  void runBootAnimation();

  // Sets the sensor sentivity of the brightness controller.
  void setSensorSentivity(int value) { _brightnessController.setSensorSensitivity(value); }

  // Sets the sensor sentivity of the brightness controller.
  float getRawSensorValue() { return _brightnessController.readSensor(); }

  // Sets whether to show AM/PM information on the display.
  void setShowAmPm(bool show_ampm) { _show_ampm = show_ampm; }

  // Starts an animation to update the clock to a new time if necessary.
  void updateForTime(int hour, int minute, int second, int animationSpeed = TIME_CHANGE_ANIMATION_SPEED);

  // Used by MQTT Client to switch off and back on without overriding the color.
  void setOff();
  void setOn();
  bool isOn() { return !_off; }
  
  void setMatrix(std::vector<RgbColor> colorValues);
  void clearMatrix();
  void scrollText(IotWebConf &iwc, String text, RgbColor textColor, int speed = 200, bool rightToLeft = false);
  enum Mode {
    BOOT,
    CLOCK,
    MATRIX,
    TICKER
  };
  Mode getMode() { return _mode; }

  // Lighting effects applied to the lit letters. The time stays readable.
  enum Effect {
    EFFECT_NONE,
    EFFECT_RAINBOW,   // Hue wave moving across the columns.
    EFFECT_COLORLOOP, // All letters slowly cycle through the hues.
    EFFECT_BREATHE,   // The color slowly pulses between 25% and 100%.
    EFFECT_COUNT
  };
  void setEffect(Effect effect);
  Effect getEffect() { return _effect; }
  static const char *effectName(Effect effect);
  // Returns EFFECT_COUNT for an unknown name.
  static Effect effectFromName(const String &name);

  // Current budget for the LEDs, in mA.
  void setMaxMilliamps(uint16_t milliamps);
  uint16_t getMaxMilliamps() { return _maxMilliamps; }

private:
  // Sends the pixels to the LEDs, dimmed to stay within _maxMilliamps.
  void _show();

  // Fills _pixelColumn for the current clock face.
  void _mapColumns();

  // Color of a lit pixel, with the current effect applied.
  RgbColor _litColor(uint16_t index);

  // Recolors the lit pixels for the current effect frame.
  void _renderEffect();

  // Updates pixel color on the display.
  void _update(int animationSpeed = TIME_CHANGE_ANIMATION_SPEED);

  // To know which pixels to turn on and off, one needs to know which letter
  // matches which LED, and the orientation of the display. This is the job
  // of the clockFace.
  ClockFace* _clockFace;

  // Overrides color to #000000 when true.
  bool _off;

  Mode _mode;
  std::vector<RgbColor> _matrix_buf;
  
  // Current budget for the LEDs, in mA.
  uint16_t _maxMilliamps = LED_MAX_MILLIAMPS;

  Effect _effect = EFFECT_NONE;
  unsigned long _effectFrameMs = 0;
  // Horizontal position of each pixel, from 0 (left) to 1 (right).
  std::vector<float> _pixelColumn;

  // Whether the display should show AM/PM information.
  bool _show_ampm = 0;

  // Addressable bus to control the LEDs.
  NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> _pixels;

  // Reacts to change in ambient light to adapt the power of the LEDs
  BrightnessController _brightnessController;

  //
  // Animation time management object.
  // Uses centiseconds as precision, so an animation can range from 1/100 of a
  // second to a little bit more than 10 minutes.
  //
  NeoPixelAnimator _animations;

  // Reserved for boot animation.
  NeoPixelAnimator _bootAnimations;

  static double _distance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    return sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
  }

  uint16_t _circleCount = 0;
  RgbColor _circleColor;
  uint16_t _circleCenterX = 5;
  uint16_t _circleCenterY = 11;
  void _circle(uint16_t x, uint16_t y, int radius, RgbColor color);
  void _colorCornerPixels(RgbColor color);
  void _circleAnimUpdate(const AnimationParam &param);
  void _fadeAll(uint8_t darkenBy);
  void _fadeAnimUpdate(const AnimationParam &param);
  void _displayCharacter(FontTable fontTable, char character, int scrollPosition, RgbColor color);
};
