// The one wire format, shared by both radios (step 2.2).
//
// ESP-NOW carries it from Week 2. LoRa keeps the Week 1 text ping until
// Phase 5, which switches it to this same struct.
//
// The HMAC tag and the replay check are Phase 6. Until then `tag` is zeroed
// and sign()/verify() are pass-throughs, so the field is on the wire from the
// start and adding security later does not change the packet size or any
// measurement taken before it.

#pragma once

#include <stdint.h>

class NodeId;

namespace msg {

constexpr uint8_t MAGIC = 0xA5;
constexpr uint8_t VERSION = 1;
constexpr uint8_t PAYLOAD_MAX = 32;
constexpr uint8_t TAG_LEN = 16;

enum Type : uint8_t {
  HEARTBEAT = 1,
  VOTE_REQ = 2,
  VOTE_REPLY = 3,
  TAKEOVER = 4,
  STATUS = 5,
};

#pragma pack(push, 1)
struct Msg {
  uint8_t  magic;    // MAGIC, quick sanity check
  uint8_t  version;  // VERSION
  uint8_t  type;     // Type
  uint8_t  sender;   // node ID
  uint32_t bootId;   // random per boot, see 6.2
  uint32_t seq;      // increases by 1 per message sent
  uint32_t term;     // Raft term, or leader epoch for linear
  uint8_t  leader;   // who the sender thinks is leader, 0 for none
  uint8_t  len;      // payload length
  uint8_t  payload[PAYLOAD_MAX];
  uint8_t  tag[TAG_LEN];  // HMAC, Phase 6. Zeros until then
};
#pragma pack(pop)

static_assert(sizeof(Msg) == 66, "Msg must stay packed at 66 bytes");
static_assert(sizeof(Msg) <= 250, "ESP-NOW allows at most 250 bytes");

// Makes this node's outgoing messages. It owns the two things every message
// from one boot must share: the boot id and the running sequence number.
class Builder {
 public:
  explicit Builder(const NodeId &self);

  // Draws the boot id. Call once at startup, after the radio is up, because
  // the hardware random source is only properly random with RF running.
  void begin();

  // Random value drawn once per boot. Lets other nodes tell "this node
  // restarted and its seq went back to 0" apart from "someone is replaying old
  // packets" (step 6.2).
  uint32_t bootId() const { return bootId_; }

  // A message from this node with the next sequence number, already signed.
  Msg make(uint8_t type, uint32_t term, uint8_t leader,
           const uint8_t *payload = nullptr, uint8_t payloadLen = 0);

 private:
  const NodeId &self_;
  uint32_t bootId_ = 0;
  uint32_t seq_ = 0;
};

// Structural check only: magic, version, length, sender in range. Says nothing
// about authenticity, which is Phase 6.
bool looksValid(const Msg &m, int len);

// Phase 6 hooks. sign() currently zeroes the tag and verify() always passes.
void sign(Msg &m);
bool verify(const Msg &m);

// Human-readable type, for log lines.
const char *typeName(uint8_t type);

}  // namespace msg
