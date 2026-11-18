// A protocol slot that does nothing: no leader, term 0, ignores every message.
//
// Two uses. With no protocol selected ('-') it is the state Phase 2 was
// measured in, heartbeats carrying leader 0 and term 0. And it holds the place
// of a protocol that is not written yet, so `PROTO C` is accepted and reported
// today and the Pi scripts never need a firmware change when the real
// implementation lands. It held Protocol A's place the same way until Phase 4.

#pragma once

#include "protocol/leader_protocol.h"

class InertProtocol : public LeaderProtocol {
 public:
  explicit InertProtocol(char letter) : letter_(letter) {}

  char letter() const override { return letter_; }
  void reset() override {}
  void onMessage(const msg::Msg &) override {}
  void tick() override {}
  uint8_t leader() const override { return 0; }
  uint32_t term() const override { return 0; }

 private:
  char letter_;
};
