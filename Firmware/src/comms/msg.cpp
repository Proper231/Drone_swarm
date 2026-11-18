#include "comms/msg.h"

#include <Arduino.h>
#include <string.h>

#include "board/node_id.h"
#include "config.h"

namespace msg {

Builder::Builder(const NodeId &self) : self_(self) {}

void Builder::begin() {
  bootId_ = esp_random();
  seq_ = 0;
}

Msg Builder::make(uint8_t type, uint32_t term, uint8_t leader,
                  const uint8_t *payload, uint8_t payloadLen) {
  Msg m;
  memset(&m, 0, sizeof(m));
  m.magic = MAGIC;
  m.version = VERSION;
  m.type = type;
  m.sender = self_.get();
  m.bootId = bootId_;
  m.seq = ++seq_;
  m.term = term;
  m.leader = leader;

  if (payload != nullptr && payloadLen > 0) {
    if (payloadLen > PAYLOAD_MAX) {
      payloadLen = PAYLOAD_MAX;
    }
    memcpy(m.payload, payload, payloadLen);
    m.len = payloadLen;
  }

  sign(m);
  return m;
}

bool looksValid(const Msg &m, int len) {
  if (len != static_cast<int>(sizeof(Msg))) {
    return false;
  }
  if (m.magic != MAGIC || m.version != VERSION) {
    return false;
  }
  // Tables are indexed by sender, so this range check is also what keeps
  // every later lookup in bounds.
  if (m.sender == 0 || m.sender >= MAX_NODES) {
    return false;
  }
  if (m.len > PAYLOAD_MAX) {
    return false;
  }
  return true;
}

void sign(Msg &m) {
  // Phase 6: HMAC-SHA256 over every field except tag, first 16 bytes kept.
  memset(m.tag, 0, TAG_LEN);
}

bool verify(const Msg &m) {
  // Phase 6: recompute and compare, drop with DROP,BAD_TAG on mismatch.
  (void)m;
  return true;
}

const char *typeName(uint8_t type) {
  switch (type) {
    case HEARTBEAT:  return "HB";
    case VOTE_REQ:   return "VOTE_REQ";
    case VOTE_REPLY: return "VOTE_REPLY";
    case TAKEOVER:   return "TAKEOVER";
    case STATUS:     return "STATUS";
    default:         return "?";
  }
}

}  // namespace msg
