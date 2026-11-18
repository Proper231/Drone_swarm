#include "swarm/neighbor_table.h"

#include <Arduino.h>

namespace {

// What get() hands back for an ID that is out of range.
const NeighborTable::Entry kEmpty;

}  // namespace

void NeighborTable::reset() {
  for (Entry &e : table_) {
    e = Entry();
  }
  highest_ = 0;
}

bool NeighborTable::record(uint8_t id, int16_t rssi, uint32_t seq,
                           uint32_t bootId) {
  if (id == 0 || id >= MAX_NODES) {
    return false;
  }
  Entry &e = table_[id];

  const bool rebooted = e.seen && e.bootId != bootId;

  e.lastHeardMs = millis();
  e.lastSeq = seq;
  e.bootId = bootId;
  e.lastRssi = rssi;
  e.rxCount++;
  e.seen = true;

  if (id > highest_) {
    highest_ = id;
  }
  return rebooted;
}

const NeighborTable::Entry &NeighborTable::get(uint8_t id) const {
  if (id == 0 || id >= MAX_NODES) {
    return kEmpty;
  }
  return table_[id];
}

uint8_t NeighborTable::heardWithin(uint32_t windowMs) const {
  const uint32_t now = millis();
  uint8_t n = 0;
  for (uint8_t i = 1; i <= highest_; ++i) {
    if (table_[i].seen && (now - table_[i].lastHeardMs) <= windowMs) {
      n++;
    }
  }
  return n;
}
