// "Has this much time passed since I last said yes?"
//
// Every periodic job in the firmware (heartbeats, the presence scan, the
// counter summary, the LoRa ping) is one of these, so the loop reads as a list
// of jobs rather than a page of timestamp arithmetic.

#pragma once

#include <Arduino.h>
#include <stdint.h>

class Interval {
 public:
  explicit constexpr Interval(uint32_t periodMs) : periodMs_(periodMs) {}

  // True once per period. Answering yes is what restarts the clock, so call it
  // only when you are about to do the job.
  bool due() {
    const uint32_t now = millis();
    if (now - lastMs_ < periodMs_) {
      return false;
    }
    lastMs_ = now;
    return true;
  }

  // Changes how long the wait is, counted from the last yes. For a job whose
  // gap is not the same every time.
  void setPeriod(uint32_t periodMs) { periodMs_ = periodMs; }

  // Makes the next due() say yes straight away.
  void expire() { lastMs_ = millis() - periodMs_; }

 private:
  uint32_t periodMs_;
  uint32_t lastMs_ = 0;
};
