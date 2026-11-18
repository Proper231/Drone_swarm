// What a leader protocol needs from a radio, and nothing more.
//
// The protocols are written against this interface rather than against
// ESP-NOW, so they cannot tell which link is carrying their messages. That is
// what lets Phase 5 put a link manager in the same seat and switch between
// ESP-NOW and LoRa without touching Protocol A or B.

#pragma once

#include <stdint.h>

#include "comms/msg.h"

// One message off the air, with what the radio knew about it.
struct Received {
  msg::Msg m;
  int16_t  rssi;
  uint32_t rxMillis;  // millis() when it arrived, not when it was polled
};

class Radio {
 public:
  virtual ~Radio() = default;

  // "ESPNOW" or "LORA", as shown on the screen and written in the logs.
  virtual const char *name() const = 0;

  virtual bool ready() const = 0;

  // Broadcasts a message. Returns false if it did not go out.
  virtual bool send(const msg::Msg &m) = 0;

  // Fills `out` with the next message that survived the fault filters and
  // returns true, or returns false when there is nothing left. Call it in a
  // while loop.
  virtual bool poll(Received &out) = 0;
};
