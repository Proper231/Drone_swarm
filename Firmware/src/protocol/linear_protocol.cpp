#include "protocol/linear_protocol.h"

#include <Arduino.h>

#include "board/node_id.h"
#include "comms/radio.h"
#include "config.h"
#include "util/log.h"

LinearProtocol::LinearProtocol(const NodeId &self, msg::Builder &builder,
                               Radio &radio)
    : self_(self), builder_(builder), radio_(radio) {}

void LinearProtocol::reset() {
  // The order is fixed and node 1 starts as leader by definition. Treating the
  // leader as freshly heard gives the swarm one T_BASE of grace to actually
  // hear node 1 before anyone starts counting down.
  leader_ = 1;
  term_ = 0;
  lastLeaderMs_ = millis();
  lost_ = false;
}

void LinearProtocol::onMessage(const msg::Msg &m) {
  if (m.sender == self_.get()) {
    return;
  }

  // A TAKEOVER names its sender as leader; a heartbeat carries whoever the
  // sender believes in.
  const uint8_t claimed = (m.type == msg::TAKEOVER) ? m.sender : m.leader;
  if (claimed == 0) {
    return;
  }

  if (m.term > term_) {
    // A later epoch always wins, which is also how a node that has been away
    // catches up without anyone special-casing it.
    adoptLeader(claimed, m.term,
                m.type == msg::TAKEOVER ? "takeover" : "higher_term");
    return;
  }

  if (m.term == term_) {
    if (m.sender == leader_) {
      // Heard from the leader ITSELF. This is the only evidence it is still
      // transmitting, and so the only thing allowed to reset the timeout.
      lastLeaderMs_ = millis();
      lost_ = false;
      return;
    }
    if (claimed == leader_) {
      // A peer that merely agrees with us about who leads. Every node
      // advertises the leader in its own heartbeats, so accepting this as
      // proof of life means the followers keep each other's timers alive and
      // the swarm never notices the leader has gone. Say nothing, let the
      // timeout run.
      return;
    }
    // Two nodes declared in the same epoch. The linear order breaks the tie,
    // and both sides reach the same answer without another round of messages.
    if (claimed < leader_) {
      adoptLeader(claimed, m.term, "tie_lower_id");
    }
    return;
  }

  // m.term < term_: the sender is behind. If we are the leader it will learn
  // from our next heartbeat, so there is nothing to do here.
  if (LINEAR_PREEMPTIVE && claimed < leader_) {
    adoptLeader(claimed, term_, "preempt");
  }
}

void LinearProtocol::tick() {
  const uint8_t self = self_.get();
  if (self == 0 || leader_ == 0) {
    return;
  }

  if (leader_ == self) {
    // The leader never times itself out. Its heartbeats go out from the node
    // at the normal interval carrying this leader and epoch.
    lastLeaderMs_ = millis();
    return;
  }

  const uint32_t silent = millis() - lastLeaderMs_;

  if (!lost_ && silent >= LINEAR_T_BASE_MS) {
    lost_ = true;
    Log::event("LEADER_LOST,%u,silent_ms=%lu", (unsigned)leader_,
               (unsigned long)silent);
  }

  if (silent >= takeoverDelayMs(leader_)) {
    declareSelf();
  }
}

void LinearProtocol::adoptLeader(uint8_t id, uint32_t term, const char *why) {
  const bool changed = (id != leader_) || (term != term_);
  leader_ = id;
  term_ = term;
  lastLeaderMs_ = millis();
  lost_ = false;
  if (changed) {
    Log::event("LEADER,%u,term=%lu,why=%s", (unsigned)id, (unsigned long)term,
               why);
  }
}

void LinearProtocol::declareSelf() {
  const uint8_t self = self_.get();
  term_ += 1;
  leader_ = self;
  lastLeaderMs_ = millis();
  lost_ = false;

  Log::event("TAKEOVER,%u,term=%lu", (unsigned)self, (unsigned long)term_);
  Log::event("LEADER,%u,term=%lu,why=self", (unsigned)self,
             (unsigned long)term_);

  radio_.send(builder_.make(msg::TAKEOVER, term_, self));
}

uint32_t LinearProtocol::takeoverDelayMs(uint8_t deadLeader) const {
  const uint8_t self = self_.get();
  if (self == deadLeader || self == 0) {
    return 0;
  }
  // Rank among nodes 1..SWARM_SIZE ordered by ID with the dead leader removed:
  // the first node still in line ranks 0 and waits only T_BASE, the next waits
  // one T_STEP more, and so on. Staggering this way means the rightful
  // successor declares before anyone else has reason to.
  const uint8_t rank = (self < deadLeader) ? (self - 1) : (self - 2);
  return LINEAR_T_BASE_MS + rank * LINEAR_T_STEP_MS;
}
