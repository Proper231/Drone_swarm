#include "swarm/presence_monitor.h"

#include <Arduino.h>
#include <string.h>

#include "swarm/neighbor_table.h"
#include "util/log.h"

PresenceMonitor::PresenceMonitor(const NeighborTable &neighbors)
    : neighbors_(neighbors) {}

void PresenceMonitor::reset() { memset(present_, 0, sizeof(present_)); }

void PresenceMonitor::update() {
  if (scanDue_.due()) {
    scan();
  }
}

void PresenceMonitor::scan() {
  const uint32_t now = millis();
  const uint8_t highest = neighbors_.highestSeen();
  for (uint8_t id = 1; id <= highest; ++id) {
    const NeighborTable::Entry &e = neighbors_.get(id);
    if (!e.seen) {
      continue;
    }
    const bool present = (now - e.lastHeardMs) <= NEIGHBOR_WINDOW_MS;
    if (present == present_[id]) {
      continue;
    }
    present_[id] = present;
    if (present) {
      Log::event("NEIGHBOR_UP,node=%u,rssi=%d", (unsigned)id, (int)e.lastRssi);
    } else {
      Log::event("NEIGHBOR_LOST,node=%u,silent_ms=%lu", (unsigned)id,
                 (unsigned long)(now - e.lastHeardMs));
    }
  }
}
