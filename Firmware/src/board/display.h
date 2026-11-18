// OLED status screen (steps 1.5 and 2.3).

#pragma once

#include <U8g2lib.h>
#include <stdint.h>

class NodeId;

class Display {
 public:
  // What step 2.3 asks the screen to show, plus the fault flags so you can see
  // at a glance which boards the Pi has killed or jammed during a run.
  struct Status {
    uint8_t  leader = 0;        // 0 for none
    uint32_t term = 0;
    const char *radio = nullptr;  // "ESPNOW" or "LORA"
    uint8_t  neighbors = 0;     // heard in the last NEIGHBOR_WINDOW_MS
    uint32_t tx = 0;
    uint32_t rx = 0;
    bool     killed = false;
    bool     jammed = false;
    bool     split = false;
    uint8_t  lossPct = 0;
    bool     loraOk = false;
  };

  // The screen prints the node's label large, so it needs to know whose it is.
  explicit Display(const NodeId &nodeId);

  // Powers the panel through Vext and draws the boot screen.
  // Returns false if the SSD1306 did not answer on I2C.
  bool begin();

  bool ok() const { return ok_; }

  // Hands over what to draw. Cheap: the redraw itself happens in update().
  void show(const Status &status);

  // Redraws at most every DISPLAY_MS, and only if something changed. Safe to
  // call every loop.
  void update();

 private:
  void draw();

  const NodeId &nodeId_;
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2_;
  Status status_;
  bool ok_ = false;
  bool dirty_ = true;
  uint32_t lastDrawMs_ = 0;
};
