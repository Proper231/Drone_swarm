// One board in the swarm: the object that owns every other part and runs the
// main loop.
//
// Reading order for someone new to the firmware: update() in the .cpp is the
// whole life of the board, one line per job. Each job belongs to one of the
// members declared at the bottom of this class, and each of those is a small
// class with a single responsibility:
//
//   board/     NodeId, Display, StatusLed        this board's own hardware
//   comms/     msg::Builder, EspNowRadio,        bytes on and off the air,
//              LoRaRadio, FaultInjector          and the emulated faults
//   swarm/     NeighborTable, PresenceMonitor    what we believe about others
//   protocol/  LeaderProtocol and its            how a leader is chosen
//              implementations
//
// The node never asks which protocol is running. It talks to whichever one is
// selected through the LeaderProtocol interface, so everything around the
// protocols is identical for A and B by construction.

#pragma once

#include <stdint.h>

#include "app/command_processor.h"
#include "board/display.h"
#include "board/node_id.h"
#include "board/status_led.h"
#include "comms/espnow_radio.h"
#include "comms/fault_injector.h"
#include "comms/lora_radio.h"
#include "comms/msg.h"
#include "config.h"
#include "protocol/inert_protocol.h"
#include "protocol/leader_protocol.h"
#include "protocol/linear_protocol.h"
#include "protocol/raft_protocol.h"
#include "swarm/neighbor_table.h"
#include "swarm/presence_monitor.h"
#include "util/interval.h"

// The step 6.4 baseline switches. Accepted and remembered now, acted on when
// their phase arrives, so the same firmware runs every test.
struct Baselines {
  bool xband = true;  // Phase 5 cross-band check
  bool hmac = true;   // Phase 6 message authentication
};

class SwarmNode {
 public:
  SwarmNode();

  void begin();   // Arduino setup()
  void update();  // Arduino loop()

  // ---- what the serial commands ask the board to do ----

  // Stores a new node ID. Returns false if flash could not be written.
  bool setNodeId(uint8_t id);
  bool clearNodeId();

  // 'A', 'B' or 'C'. Returns false for anything else. Selecting a protocol
  // also clears its state, the neighbour table and the counters, so every run
  // starts from the same place.
  bool selectProtocol(char letter);

  void kill();    // silent and deaf, until revive()
  void revive();  // back as a fresh follower

  // RESET: protocol state, tables, counters and every injected fault.
  void resetAll();

  // Starts or stops the once-a-second LoRa ping. Returns false if asked to
  // start while the LoRa radio is not up.
  bool setPinging(bool on);

  // Heartbeat jitter in ms either side of ESPNOW_HB_MS, 0 for none. Returns
  // false above ESPNOW_HB_JITTER_MAX_MS. Not cleared by RESET, like PROTO: it
  // is a setting of the run, not a fault.
  bool setHeartbeatJitter(uint32_t ms);
  uint32_t heartbeatJitterMs() const { return hbJitterMs_; }

  // The longest a Raft voter waits before answering, in ms. A setting of the
  // run like the one above. It is Raft's own number, so it is kept by the
  // Raft object whichever protocol is selected.
  bool setVoteReplyDelay(uint32_t ms) { return raft_.setReplyDelay(ms); }
  uint32_t voteReplyDelayMs() const { return raft_.replyDelayMs(); }

  int16_t startLora();  // RadioLib status code, 0 is success
  void stopLora();

  void printStatus() const;
  void refreshScreen();

  // ---- parts the commands read or set directly ----

  const NodeId &nodeId() const { return nodeId_; }
  FaultInjector &faults() { return faults_; }
  Baselines &baselines() { return baselines_; }
  const char *mac() const { return mac_; }

 private:
  void receiveMessages();
  void sendHeartbeat();
  void logCounters();
  void resetProtocolState();
  const char *activeRadioName() const;

  // Declaration order is construction order, and several members are handed
  // references to earlier ones, so keep dependencies above their users.
  NodeId nodeId_;
  Display display_;
  StatusLed led_;
  msg::Builder builder_;
  NeighborTable neighbors_;
  PresenceMonitor presence_;
  FaultInjector faults_;
  EspNowRadio espnow_;
  LoRaRadio lora_;

  // One slot per PROTO letter. C is a placeholder until Phase 9 replaces it
  // with the real thing, as Phase 4 did for A; nothing else here changes then.
  InertProtocol noProtocol_{'-'};
  RaftProtocol raft_;
  LinearProtocol linear_;
  InertProtocol hierSlot_{'C'};
  LeaderProtocol *protocol_ = &noProtocol_;

  CommandProcessor commands_;
  Baselines baselines_;

  Interval heartbeatDue_{ESPNOW_HB_MS};
  uint32_t hbJitterMs_ = ESPNOW_HB_JITTER_MS;
  Interval countersDue_{COUNTERS_MS};
  Interval pingDue_{LORA_PING_MS};
  bool pinging_ = false;
  uint32_t pingSeq_ = 0;

  char mac_[18] = "00:00:00:00:00:00";
};
