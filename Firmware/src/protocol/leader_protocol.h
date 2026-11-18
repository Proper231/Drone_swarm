// What every leader-election protocol looks like to the rest of the firmware.
//
// Protocol A (Raft), Protocol B (linear) and Protocol C (hierarchical) all
// implement this, and the node drives whichever is selected through it without
// knowing which one that is. That is deliberate: the project compares the
// protocols, so everything around them (heartbeats, faults, logging, timing of
// the main loop) has to be identical, and the only way to be sure of that is
// for none of it to contain an "if Raft" anywhere.
//
// Both leader() and term() ride in every heartbeat this node sends, in the
// same two Msg fields for every protocol, with the same "higher term wins"
// meaning. That is what lets the analysis count a leader change the same way
// for A and B.

#pragma once

#include <stdint.h>

#include "comms/msg.h"

class LeaderProtocol {
 public:
  virtual ~LeaderProtocol() = default;

  // 'A' Raft, 'B' linear, 'C' hierarchical, '-' none. What the PROTO command
  // takes and STATUS reports.
  virtual char letter() const = 0;

  // Forget everything and start over, as a freshly booted node would.
  virtual void reset() = 0;

  // A message from another node that survived the fault filters.
  virtual void onMessage(const msg::Msg &m) = 0;

  // Advance the protocol's timers. Called every loop, except while the board
  // is killed: a killed board is isolated, so its protocol stops with it.
  virtual void tick() = 0;

  // Who this node currently believes leads, 0 for nobody.
  virtual uint8_t leader() const = 0;

  // Raft term, or leader epoch for the linear protocol.
  virtual uint32_t term() const = 0;
};
