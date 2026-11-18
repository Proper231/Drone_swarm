#include "board/display.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

#include "board/node_id.h"
#include "config.h"

namespace {

// SSD1306 on the Heltec V3.
constexpr uint8_t OLED_I2C_ADDR = 0x3C;

}  // namespace

Display::Display(const NodeId &nodeId)
    : nodeId_(nodeId),
      u8g2_(U8G2_R0, PIN_OLED_RST, PIN_OLED_SCL, PIN_OLED_SDA) {}

bool Display::begin() {
  status_ = Status();
  status_.radio = "ESPNOW";

  // Vext is active LOW. Without this the panel gets no power and stays blank
  // no matter what you send it.
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);
  delay(50);

  // Hardware reset the controller before talking to it.
  pinMode(PIN_OLED_RST, OUTPUT);
  digitalWrite(PIN_OLED_RST, LOW);
  delay(20);
  digitalWrite(PIN_OLED_RST, HIGH);
  delay(20);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  // u8g2.begin() does not probe the bus, it reports success even with no panel
  // attached. Ask the controller for an ACK first so that "oled=0" in the boot
  // log actually means something.
  Wire.beginTransmission(OLED_I2C_ADDR);
  const bool present = (Wire.endTransmission() == 0);

  ok_ = present && u8g2_.begin();
  if (ok_) {
    draw();
    lastDrawMs_ = millis();
  }
  return ok_;
}

void Display::show(const Status &status) {
  status_ = status;
  dirty_ = true;
}

void Display::update() {
  if (!ok_ || !dirty_) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastDrawMs_ < DISPLAY_MS) {
    return;
  }
  draw();
  lastDrawMs_ = now;
  dirty_ = false;
}

void Display::draw() {
  char buf[26];

  u8g2_.clearBuffer();

  // Node label, large, so you can read the swarm across a desk.
  u8g2_.setFont(u8g2_font_ncenB14_tr);
  u8g2_.drawStr(0, 15, nodeId_.label());

  u8g2_.setFont(u8g2_font_6x12_tr);

  // Active radio, top right.
  u8g2_.drawStr(78, 13, status_.radio != nullptr ? status_.radio : "--");

  // Leader and term.
  if (status_.leader == 0) {
    snprintf(buf, sizeof(buf), "L:--  T:%lu", (unsigned long)status_.term);
  } else {
    snprintf(buf, sizeof(buf), "L:%u   T:%lu", (unsigned)status_.leader,
             (unsigned long)status_.term);
  }
  u8g2_.drawStr(0, 30, buf);

  // Neighbours heard in the last second, then any fault flags that are on.
  snprintf(buf, sizeof(buf), "N:%u", (unsigned)status_.neighbors);
  u8g2_.drawStr(0, 42, buf);

  char flags[20] = "";
  if (status_.killed) strncat(flags, "KILL ", sizeof(flags) - strlen(flags) - 1);
  if (status_.jammed) strncat(flags, "JAM ", sizeof(flags) - strlen(flags) - 1);
  if (status_.split)  strncat(flags, "SPLIT ", sizeof(flags) - strlen(flags) - 1);
  if (status_.lossPct > 0) {
    char l[8];
    snprintf(l, sizeof(l), "L%u%% ", (unsigned)status_.lossPct);
    strncat(flags, l, sizeof(flags) - strlen(flags) - 1);
  }
  if (flags[0] == '\0' && !status_.loraOk) {
    snprintf(flags, sizeof(flags), "NOLORA");
  }
  u8g2_.drawStr(34, 42, flags);

  snprintf(buf, sizeof(buf), "TX%lu RX%lu", (unsigned long)status_.tx,
           (unsigned long)status_.rx);
  u8g2_.drawStr(0, 56, buf);

  u8g2_.sendBuffer();
}
