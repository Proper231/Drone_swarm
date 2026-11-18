// Permanent per-board node ID stored in NVS flash (step 1.4).
//
// The ID survives reflashing, so every board runs the identical binary and you
// never have to remember which build flag went to which board.

#pragma once

#include <stdint.h>

#include "config.h"

class NodeId {
 public:
  // Reads the stored ID. Returns NODE_ID_UNSET when the board has never been
  // provisioned. Call once at startup, before anything else needs the ID.
  uint8_t load();

  uint8_t get() const { return id_; }
  bool isSet() const { return id_ != NODE_ID_UNSET; }

  // Writes the ID to flash. Returns false if id is 0 or the write failed.
  bool save(uint8_t id);

  // Forgets the stored ID, so the board boots showing "NO ID" again.
  bool clear();

  // "D3", "ATK", "SPARE" or "NO ID", for the screen and the boot line.
  const char *label() const { return label_; }

 private:
  void refreshLabel();

  uint8_t id_ = NODE_ID_UNSET;
  char label_[8] = "NO ID";
};
