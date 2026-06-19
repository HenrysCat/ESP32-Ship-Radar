#include "hardware/display.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  pinMode(config::kDisplayPinBacklight, OUTPUT);
  digitalWrite(config::kDisplayPinBacklight, HIGH);
  delay(20);

  const bool ok = tft.init();
  Serial.printf("display: init %s\n", ok ? "ok" : "failed");
  digitalWrite(config::kDisplayPinBacklight, HIGH);
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
