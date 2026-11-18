// Protocol A: Raft leader election (Phase 4).
//
// The election half of the Raft paper and nothing else: terms, randomised
// timeouts, one vote per term, majority of the full swarm. No log, no PreVote,
// no leader lease, nothing persisted. Those omissions are deliberate, because
// each one hides a weakness the Phase 7 scenarios exist to measure. See
// Docs/raft_design.md section 7.
//
// One thing differs from the textbook. Here every node heartbeats, not just
// the leader, because presence detection needs it, and each heartbeat carries
// its sender's term() and leader(). So:
//
//   - the leader's heartbeat is the one whose `leader` field names its own
//     sender. That is Raft's empty AppendEntries.
//   - a peer's heartbeat is NOT evidence about the leader. A follower never
//     learns who leads from one and never resets its deadline on one.
//   - terms travel on everything, so a node that is behind catches up from
//     the first message it hears from anyone.

#pragma once

#include <stdint.h>

#include "config.h"
#include "protocol/leader_protocol.h"

class NodeId;
class Radio;

class RaftProtocol : public LeaderProtocol {
 public:
  RaftProtocol(const NodeId &self, msg::Builder &builder, Radio &radio);

  char letter() const override { return 'A'; }
  void reset() override;
  void onMessage(const msg::Msg &m) override;
  void tick() override;
  uint8_t leader() const override { return leader_; }
  uint32_t term() const override { return term_; }

  // The longest a voter waits before answering a VOTE_REQ, 0 to answer at
  // once. Returns false above RAFT_REPLY_DELAY_MAX_MS. Like PROTO, it is a
  // setting of the run and reset() leaves it alone.
  bool setReplyDelay(uint32_t maxMs);
  uint32_t replyDelayMs() const { return replyDelayMs_; }

 private:
  enum class Role : uint8_t { FOLLOWER, CANDIDATE, LEADER };

  // A vote reply decided on but not yet sent. It holds the term and leader of
  // the moment the vote was cast, so that waiting changes when the reply goes
  // out and nothing in it.
  struct PendingReply {
    bool waiting = false;
    uint8_t candidate = 0;
    bool granted = false;
    uint32_t term = 0;
    uint8_t leader = 0;
    uint32_t queuedUs = 0;  // micros() when it was decided
    uint32_t waitUs = 0;    // the random wait drawn at that moment
  };
  static constexpr uint8_t MAX_PENDING_REPLIES = 4;

  // A higher term always wins: whatever this node was, it is a follower of
  // nobody in the new term. Every STEP_DOWN for that reason comes from here.
  void advanceTerm(uint32_t term);

  void onLeaderHeartbeat(uint8_t from);
  void onVoteRequest(uint8_t candidate);
  void onVoteReply(const msg::Msg &m);

  void startElection(uint32_t waitedMs);
  void becomeLeader();
  void sendVoteReply(uint8_t candidate, bool granted);
  void transmitVoteReply(const PendingReply &reply);
  void sendDueReplies();

  // A fresh random timeout, counted from now.
  void armDeadline();

  // A second leader in one term, which Raft promises cannot happen. Logged
  // once per term so a violation fails a run loudly without flooding the log.
  void reportConflict(uint8_t other);

  uint8_t voteCount() const;

  const NodeId &self_;
  msg::Builder &builder_;
  Radio &radio_;

  Role role_ = Role::FOLLOWER;
  uint32_t term_ = 0;
  uint8_t votedFor_ = 0;     // 0 is nobody, in term_
  uint8_t leader_ = 0;       // 0 is nobody, in term_

  // One bit per node ID, own bit included, so a reply that arrives twice
  // cannot be counted twice.
  uint16_t votes_ = 0;
  static_assert(SWARM_SIZE < 16, "votes_ holds one bit per node ID");

  uint32_t armedAtMs_ = 0;   // when the election deadline was last armed
  uint32_t timeoutMs_ = 0;   // the random timeout drawn at that moment
  bool conflictLogged_ = false;

  uint32_t replyDelayMs_ = RAFT_REPLY_DELAY_MS;
  PendingReply pending_[MAX_PENDING_REPLIES];
};
