// The onboard LED as a heartbeat for the firmware itself (step 1.3): one short
// flash a second means the main loop is alive. A board whose LED has stopped
// has hung, whatever its screen still shows.

#pragma once

#include <stdint.h>

class StatusLed {
 public:
  void begin();

  // Call every loop.
  void update();

 private:
  uint32_t lastOnMs_ = 0;
  bool on_ = false;
};
