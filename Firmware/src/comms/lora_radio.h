// LoRa (SX1262) link. Today it carries only the two-board text ping of
// step 1.6, which is why it does not implement Radio yet.
//
// Phase 5 grows it into the real backup radio: the packed Msg struct instead
// of text, random backoff plus channel activity detection before each send,
// heartbeats every 2 s. At that point it implements Radio and the link manager
// chooses between it and ESP-NOW.

#pragma once

#include <RadioLib.h>
#include <stdint.h>

class LoRaRadio {
 public:
  LoRaRadio();

  // Brings up SPI and the SX1262 in receive mode. Transmitting stays disabled
  // until setTxEnabled(true), so a board with no antenna attached cannot damage
  // its radio just by booting.
  // Returns the RadioLib status code: 0 is success.
  int16_t begin();

  // Puts the radio in standby and marks it not ready, so nothing is sent or
  // received until begin() runs again. Used by "LORA OFF".
  void stop();

  bool ready() const { return ready_; }
  int16_t lastError() const { return lastError_; }

  void setTxEnabled(bool enabled) { txEnabled_ = enabled; }
  bool txEnabled() const { return txEnabled_; }

  // Sends "PING <node> <seq>" and logs it. Returns false, sending nothing,
  // when TX is disabled or the radio failed to start.
  bool sendPing(uint8_t nodeId, uint32_t seq);

  // Call every loop. Reads any packet the DIO1 interrupt flagged and logs it.
  // Returns true if a good packet was handled this call.
  bool poll();

  uint32_t txCount() const { return tx_; }
  uint32_t rxCount() const { return rx_; }
  int lastRssi() const { return rssi_; }
  float lastSnr() const { return snr_; }

 private:
  SX1262 radio_;
  bool ready_ = false;
  bool txEnabled_ = false;
  int16_t lastError_ = 0;
  uint32_t tx_ = 0;
  uint32_t rx_ = 0;
  int rssi_ = 0;
  float snr_ = 0.0f;
};
