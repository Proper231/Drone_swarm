#include "protocol/raft_protocol.h"

#include <Arduino.h>

#include "board/node_id.h"
#include "comms/radio.h"
#include "util/log.h"

namespace {

// ESP-NOW here is broadcast only, so a reply cannot be addressed. It names
// the candidate it is for and everyone else ignores it.
#pragma pack(push, 1)
struct VoteReply {
  uint8_t candidate;
  uint8_t granted;  // 1 granted, 0 denied
};
#pragma pack(pop)

// Only D1..D8 vote or stand. ATK, SPARE and a board with no ID are not part
// of the majority, so they must neither count towards it nor be heard by it.
bool isMember(uint8_t id) { return id >= 1 && id <= SWARM_SIZE; }

}  // namespace

RaftProtocol::RaftProtocol(const NodeId &self, msg::Builder &builder,
                           Radio &radio)
    : self_(self), builder_(builder), radio_(radio) {
  // Nothing is drawn or armed here. The node is built before setup() runs,
  // when the radio is down and esp_random() is not yet properly random.
  // reset() does it, and the protocol only becomes active through a reset.
}

void RaftProtocol::reset() {
  role_ = Role::FOLLOWER;
  term_ = 0;
  votedFor_ = 0;
  leader_ = 0;
  votes_ = 0;
  conflictLogged_ = false;
  // A reply still waiting belongs to the life this node is leaving.
  for (PendingReply &reply : pending_) {
    reply.waiting = false;
  }
  armDeadline();
}

bool RaftProtocol::setReplyDelay(uint32_t maxMs) {
  if (maxMs > RAFT_REPLY_DELAY_MAX_MS) {
    return false;
  }
  replyDelayMs_ = maxMs;
  return true;
}

void RaftProtocol::onMessage(const msg::Msg &m) {
  const uint8_t self = self_.get();
  if (!isMember(self) || !isMember(m.sender) || m.sender == self) {
    return;
  }

  if (m.term > term_) {
    // Whatever the message is, its sender is ahead. Catch up first, then
    // handle it as a message of the term this node is now in.
    advanceTerm(m.term);
  } else if (m.term < term_) {
    // The sender is behind. A stale candidate is told so, because the denial
    // carries the current term and that is what stops it asking again.
    if (m.type == msg::VOTE_REQ) {
      sendVoteReply(m.sender, false);
    }
    return;
  }

  switch (m.type) {
    case msg::HEARTBEAT:
      // Only the leader speaking for itself counts. A heartbeat from a peer
      // repeats who that peer believes in, and treating it as proof of life
      // means the followers keep each other's deadlines alive and the swarm
      // never notices the leader has gone (lab notes, 2026-11-15).
      if (m.leader == m.sender) {
        onLeaderHeartbeat(m.sender);
      }
      break;
    case msg::VOTE_REQ:
      onVoteRequest(m.sender);
      break;
    case msg::VOTE_REPLY:
      onVoteReply(m);
      break;
    default:
      break;
  }
}

void RaftProtocol::tick() {
  if (!isMember(self_.get())) {
    return;
  }

  // Before the role test: a leader can owe a reply too, a denial.
  sendDueReplies();

  if (role_ == Role::LEADER) {
    // A leader has no deadline running. Without a lease it stays leader until
    // it hears a higher term, even with nobody left to hear it.
    return;
  }

  const uint32_t waited = millis() - armedAtMs_;
  if (waited < timeoutMs_) {
    return;
  }

  if (role_ == Role::CANDIDATE) {
    // Its own election ran out: a split vote, lost replies, or fewer than a
    // majority of the swarm alive to answer.
    Log::event("ELECTION_TIMEOUT,%u,term=%lu,votes=%u", (unsigned)self_.get(),
               (unsigned long)term_, (unsigned)voteCount());
  }
  startElection(waited);
}

void RaftProtocol::advanceTerm(uint32_t term) {
  const bool wasLeader = (role_ == Role::LEADER);
  if (role_ != Role::FOLLOWER) {
    // term= is the term stepped down into, as on every other line: the term
    // this node is in once the event has happened.
    Log::event("STEP_DOWN,%u,term=%lu,was=%s,why=higher_term",
               (unsigned)self_.get(), (unsigned long)term,
               wasLeader ? "LEADER" : "CANDIDATE");
  }

  role_ = Role::FOLLOWER;
  term_ = term;
  votedFor_ = 0;
  leader_ = 0;
  votes_ = 0;
  conflictLogged_ = false;

  // A follower or candidate keeps the deadline it already has: hearing a
  // higher term is not hearing a leader. A leader had none running, so it
  // needs one now or it would never stand again.
  if (wasLeader) {
    armDeadline();
  }
}

void RaftProtocol::onLeaderHeartbeat(uint8_t from) {
  if (role_ == Role::LEADER) {
    reportConflict(from);
    return;
  }

  if (role_ == Role::CANDIDATE) {
    // Someone else won this term while this node was still collecting votes.
    Log::event("STEP_DOWN,%u,term=%lu,was=CANDIDATE,why=leader_seen",
               (unsigned)self_.get(), (unsigned long)term_);
    role_ = Role::FOLLOWER;
    votes_ = 0;
  }

  if (leader_ == 0) {
    leader_ = from;
    Log::event("LEADER,%u,term=%lu,why=heartbeat", (unsigned)from,
               (unsigned long)term_);
  } else if (leader_ != from) {
    // Not the leader this node accepted, so not a reason to keep waiting.
    reportConflict(from);
    return;
  }

  armDeadline();
}

void RaftProtocol::onVoteRequest(uint8_t candidate) {
  // No role test is needed: a candidate or leader has already voted for
  // itself in this term, so it denies.
  const bool granted = (votedFor_ == 0 || votedFor_ == candidate);
  if (granted) {
    votedFor_ = candidate;
    armDeadline();
    Log::event("VOTE_GRANTED,%u,term=%lu", (unsigned)candidate,
               (unsigned long)term_);
  }
  sendVoteReply(candidate, granted);
}

void RaftProtocol::onVoteReply(const msg::Msg &m) {
  if (role_ != Role::CANDIDATE || m.len < sizeof(VoteReply)) {
    return;
  }
  if (m.payload[0] != self_.get() || m.payload[1] != 1) {
    return;
  }

  votes_ |= static_cast<uint16_t>(1u << m.sender);
  if (voteCount() >= RAFT_MAJORITY) {
    becomeLeader();
  }
}

void RaftProtocol::startElection(uint32_t waitedMs) {
  const uint8_t self = self_.get();
  term_ += 1;
  role_ = Role::CANDIDATE;
  votedFor_ = self;
  votes_ = static_cast<uint16_t>(1u << self);
  leader_ = 0;
  conflictLogged_ = false;
  armDeadline();

  Log::event("ELECTION_START,%u,term=%lu,waited_ms=%lu", (unsigned)self,
             (unsigned long)term_, (unsigned long)waitedMs);

  radio_.send(builder_.make(msg::VOTE_REQ, term_, 0));
}

void RaftProtocol::becomeLeader() {
  const uint8_t self = self_.get();
  role_ = Role::LEADER;
  leader_ = self;

  Log::event("BECAME_LEADER,%u,term=%lu,votes=%u", (unsigned)self,
             (unsigned long)term_, (unsigned)voteCount());
  Log::event("LEADER,%u,term=%lu,why=self", (unsigned)self,
             (unsigned long)term_);

  // At once, not at the node's next 200 ms slot: every follower still has a
  // deadline running, and the sooner they hear a leader the fewer of them
  // stand against it.
  radio_.send(builder_.make(msg::HEARTBEAT, term_, leader_));
}

void RaftProtocol::sendVoteReply(uint8_t candidate, bool granted) {
  PendingReply reply;
  reply.candidate = candidate;
  reply.granted = granted;
  reply.term = term_;
  reply.leader = leader_;

  if (replyDelayMs_ == 0) {
    transmitVoteReply(reply);
    return;
  }

  // Every voter hears the request at the same instant. Answering at once
  // means answering together, and replies sent together do not all arrive. A
  // wait drawn in microseconds spreads them finer than the loop can tell two
  // boards apart.
  for (PendingReply &slot : pending_) {
    if (!slot.waiting) {
      slot = reply;
      slot.waiting = true;
      slot.queuedUs = micros();
      slot.waitUs = esp_random() % (replyDelayMs_ * 1000 + 1);
      return;
    }
  }

  // More requests at once than there are slots. Late and certain beats
  // dropped, so this one goes now.
  transmitVoteReply(reply);
}

void RaftProtocol::transmitVoteReply(const PendingReply &reply) {
  const VoteReply payload = {reply.candidate,
                             static_cast<uint8_t>(reply.granted ? 1 : 0)};
  radio_.send(builder_.make(msg::VOTE_REPLY, reply.term, reply.leader,
                            reinterpret_cast<const uint8_t *>(&payload),
                            sizeof(payload)));
}

void RaftProtocol::sendDueReplies() {
  for (PendingReply &reply : pending_) {
    // Elapsed-since-queued, so it survives micros() wrapping every 71 minutes.
    if (reply.waiting && micros() - reply.queuedUs >= reply.waitUs) {
      reply.waiting = false;
      transmitVoteReply(reply);
    }
  }
}

void RaftProtocol::armDeadline() {
  // Elapsed-since-armed rather than an absolute deadline, so it survives
  // millis() wrapping.
  armedAtMs_ = millis();
  timeoutMs_ = RAFT_TIMEOUT_MIN_MS +
               esp_random() % (RAFT_TIMEOUT_MAX_MS - RAFT_TIMEOUT_MIN_MS + 1);
}

void RaftProtocol::reportConflict(uint8_t other) {
  if (conflictLogged_) {
    return;
  }
  conflictLogged_ = true;
  Log::event("LEADER_CONFLICT,%u,term=%lu", (unsigned)other,
             (unsigned long)term_);
}

uint8_t RaftProtocol::voteCount() const {
  return static_cast<uint8_t>(__builtin_popcount(votes_));
}
