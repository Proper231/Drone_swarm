// Turns the neighbour table into two log events: NEIGHBOR_UP when a node is
// first heard, NEIGHBOR_LOST when it has been silent for a whole window.
//
// This is what makes a KILL show up in the capture as an event with a
// timestamp, rather than only as a gap in the data, and it is the event the
// Phase 2 "Done when" check is measured from. It also stands in for a log line
// per heartbeat, which would flood the serial link (step 2.5).

#pragma once

#include <stdint.h>

#include "config.h"
#include "util/interval.h"

class NeighborTable;

class PresenceMonitor {
 public:
  explicit PresenceMonitor(const NeighborTable &neighbors);

  // Forgets who was present. Call whenever the neighbour table is reset.
  void reset();

  // Call every loop. Rescans every PRESENCE_SCAN_MS.
  void update();

 private:
  void scan();

  const NeighborTable &neighbors_;
  Interval scanDue_{PRESENCE_SCAN_MS};
  bool present_[MAX_NODES] = {};
};
