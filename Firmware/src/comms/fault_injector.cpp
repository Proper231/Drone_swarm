#include "comms/fault_injector.h"

#include <Arduino.h>
#include <string.h>

void FaultInjector::setLossPct(uint8_t pct) { lossPct_ = pct > 100 ? 100 : pct; }

void FaultInjector::splitTo(const uint8_t *ids, uint8_t count) {
  memset(splitAllow_, 0, sizeof(splitAllow_));
  for (uint8_t i = 0; i < count; ++i) {
    if (ids[i] > 0 && ids[i] < MAX_NODES) {
      splitAllow_[ids[i]] = true;
    }
  }
  splitOn_ = true;
}

void FaultInjector::splitOff() {
  splitOn_ = false;
  memset(splitAllow_, 0, sizeof(splitAllow_));
}

void FaultInjector::clear() {
  killed_ = false;
  jammed_ = false;
  lossPct_ = 0;
  splitOff();
}

bool FaultInjector::blocksReceive() {
  if (killed_) {
    drops_.kill++;
    return true;
  }
  if (jammed_) {
    drops_.jam++;
    return true;
  }
  return false;
}

bool FaultInjector::dropsMessageFrom(uint8_t sender) {
  // The range test is belt and braces: the caller has already validated the
  // sender, but an out-of-range index here would read past the table.
  if (splitOn_ && (sender >= MAX_NODES || !splitAllow_[sender])) {
    drops_.split++;
    return true;
  }
  if (lossPct_ > 0 && (esp_random() % 100u) < lossPct_) {
    drops_.loss++;
    return true;
  }
  return false;
}

void FaultInjector::resetDrops() { memset(&drops_, 0, sizeof(drops_)); }
