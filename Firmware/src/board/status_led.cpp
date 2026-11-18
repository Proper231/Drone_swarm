#include "board/status_led.h"

#include <Arduino.h>

#include "config.h"

void StatusLed::begin() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
}

void StatusLed::update() {
  const uint32_t now = millis();
  if (on_) {
    if (now - lastOnMs_ >= LED_FLASH_MS) {
      digitalWrite(PIN_LED, LOW);
      on_ = false;
    }
  } else if (now - lastOnMs_ >= LED_BLINK_MS) {
    digitalWrite(PIN_LED, HIGH);
    on_ = true;
    lastOnMs_ = now;
  }
}
