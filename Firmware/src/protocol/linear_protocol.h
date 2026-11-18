// Protocol B: linear predefined order (Phase 3).
//
// Node 1 leads, then 2, 3 ... 8. When the leader goes silent, each remaining
// node waits a delay proportional to its place in the order and the first to
// time out declares itself leader with a higher epoch.
//
// Non-pre-emptive: a revived node rejoins as a follower. See LINEAR_PREEMPTIVE
// in config.h for why.

#pragma once

#include <stdint.h>

#include "protocol/leader_protocol.h"

class NodeId;
class Radio;

class LinearProtocol : public LeaderProtocol {
 public:
  LinearProtocol(const NodeId &self, msg::Builder &builder, Radio &radio);

  char letter() const override { return 'B'; }
  void reset() override;
  void onMessage(const msg::Msg &m) override;
  void tick() override;
  uint8_t leader() const override { return leader_; }
  uint32_t term() const override { return term_; }

 private:
  // The only way this node's belief changes on someone else's say-so. Every
  // LEADER line in the log comes from here or from declareSelf(), so those
  // lines are exactly the transitions the analysis counts as a leader change.
  void adoptLeader(uint8_t id, uint32_t term, const char *why);

  // Takes the lead with a higher epoch and tells the swarm.
  void declareSelf();

  // How long this node waits, from the leader's last message, before taking
  // over from `deadLeader`.
  uint32_t takeoverDelayMs(uint8_t deadLeader) const;

  const NodeId &self_;
  msg::Builder &builder_;
  Radio &radio_;

  uint8_t leader_ = 1;
  uint32_t term_ = 0;
  uint32_t lastLeaderMs_ = 0;  // when the leader itself was last heard
  bool lost_ = false;          // LEADER_LOST has been logged for this silence
};
