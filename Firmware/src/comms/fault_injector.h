// The emulated faults from the step 2.4 command table: KILL, JAM, LOSS, SPLIT.
//
// It sits at the radio, below the protocols, and that placement is the point:
// every protocol is tested against exactly the same fault behaviour, and none
// can accidentally see a message it was supposed to miss.
//
// The serial commands set the faults; the radio asks about them. Nothing is
// transmitted or jammed for real (Phase 0 safety rule).

#pragma once

#include <stdint.h>

#include "config.h"

class FaultInjector {
 public:
  // How many messages each fault has swallowed, for the COUNTERS log line.
  struct Drops {
    uint32_t kill;
    uint32_t jam;
    uint32_t split;
    uint32_t loss;
  };

  // ---- set by the serial commands ----

  void setKilled(bool on) { killed_ = on; }  // KILL / REVIVE: silent and deaf
  bool killed() const { return killed_; }

  void setJammed(bool on) { jammed_ = on; }  // JAM: emulated 2.4 GHz jamming
  bool jammed() const { return jammed_; }

  void setLossPct(uint8_t pct);  // LOSS: drop this % of received messages
  uint8_t lossPct() const { return lossPct_; }

  void splitTo(const uint8_t *ids, uint8_t count);  // SPLIT <id,id,...>
  void splitOff();                                  // SPLIT OFF
  bool splitOn() const { return splitOn_; }

  // Every fault off, as RESET asks. Leaves the drop counts alone.
  void clear();

  // ---- asked by the radio, in this order ----

  // True when the board may not transmit: it is killed or jammed.
  bool blocksSend() const { return killed_ || jammed_; }

  // True when an arriving frame must be discarded unread, because the radio
  // is "not working". Counts the reason.
  bool blocksReceive();

  // True when a well-formed message from `sender` must be dropped. Counts the
  // reason. Only ask once the message has passed msg::looksValid(), which is
  // what guarantees the sender is in range.
  bool dropsMessageFrom(uint8_t sender);

  const Drops &drops() const { return drops_; }
  void resetDrops();

 private:
  bool killed_ = false;
  bool jammed_ = false;
  uint8_t lossPct_ = 0;
  bool splitOn_ = false;
  bool splitAllow_[MAX_NODES] = {};
  Drops drops_ = {};
};
