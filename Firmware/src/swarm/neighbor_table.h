// Who have we heard from, how recently, and how strongly (step 2.3).
//
// Indexed directly by node ID. Phase 5's link manager turns the same data into
// a loss percentage per radio and drives the LoRa switch from it.

#pragma once

#include <stdint.h>

#include "config.h"

class NeighborTable {
 public:
  struct Entry {
    uint32_t lastHeardMs = 0;  // millis() of the last accepted message
    uint32_t lastSeq = 0;      // last sequence number seen from this node
    uint32_t bootId = 0;       // sender's boot id, to spot a restart
    uint32_t rxCount = 0;      // accepted messages since the last reset
    int16_t  lastRssi = 0;
    bool     seen = false;
  };

  void reset();

  // Records an accepted message. Returns true if this looks like a fresh boot
  // of that node (new bootId), which Phase 6 uses to reset its replay window.
  bool record(uint8_t id, int16_t rssi, uint32_t seq, uint32_t bootId);

  const Entry &get(uint8_t id) const;

  // How many distinct nodes we have heard from inside the window.
  uint8_t heardWithin(uint32_t windowMs) const;

  // Highest node ID ever seen, so callers can iterate without scanning all 100.
  uint8_t highestSeen() const { return highest_; }

 private:
  Entry table_[MAX_NODES];
  uint8_t highest_ = 0;
};
