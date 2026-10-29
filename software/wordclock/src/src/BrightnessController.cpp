#include "BrightnessController.h"

#define MIN_DIM 100
// Minimum change of the dim level, before gamma correction, to update the
// LEDs. After gamma, sensor noise is amplified at high brightness: it used to
// trigger an update on almost every loop, each one restarting the fade, and
// the display froze on the time it showed.
#define MIN_DIM_CHANGE 5
// Minimum interval between two sensor driven updates, in ms.
#define DIM_UPDATE_INTERVAL 1000

BrightnessController::BrightnessController() {}

void BrightnessController::setOriginalColor(RgbColor color)
{
  original_ = color;
  corrected_ = gammaAdjust(original_);
  changed_ = true;
  dim_ = -1; // Apply the sensor reading on the next loop.
}

void BrightnessController::setup()
{
  lightSensor_.setup();
  corrected_ = gammaAdjust(original_);
}

void BrightnessController::loop()
{
  lightSensor_.loop();

  if (lightSensor_.sensitivity == 0)
  {
    RgbColor gamma_corrected = gammaAdjust(original_);
    changed_ = corrected_ != gamma_corrected;
    corrected_ = gamma_corrected;
    return;
  }

  int dim = static_cast<int>((255 - MIN_DIM) * lightSensor_.reading() + MIN_DIM);
  if (dim_ >= 0 && (abs(dim - dim_) < MIN_DIM_CHANGE || millis() - dimUpdateMs_ < DIM_UPDATE_INTERVAL))
  {
    return; // don't adjust for small or too frequent changes.
  }

  dim_ = dim;
  dimUpdateMs_ = millis();
  corrected_ = gammaAdjust(original_.Dim(dim));
  changed_ = true;
}
