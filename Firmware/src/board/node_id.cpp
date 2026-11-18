#include "board/node_id.h"

#include <Preferences.h>
#include <stdio.h>

namespace {

constexpr const char *NVS_NAMESPACE = "swarm";
constexpr const char *NVS_KEY_ID = "nodeid";

}  // namespace

uint8_t NodeId::load() {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
    id_ = prefs.getUChar(NVS_KEY_ID, NODE_ID_UNSET);
    prefs.end();
  } else {
    // Namespace does not exist yet. That is the normal first-boot case.
    id_ = NODE_ID_UNSET;
  }
  refreshLabel();
  return id_;
}

bool NodeId::save(uint8_t id) {
  if (id == NODE_ID_UNSET) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
    return false;
  }
  const bool ok = prefs.putUChar(NVS_KEY_ID, id) == sizeof(uint8_t);
  prefs.end();
  if (ok) {
    id_ = id;
    refreshLabel();
  }
  return ok;
}

bool NodeId::clear() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
    return false;
  }
  const bool ok = prefs.remove(NVS_KEY_ID);
  prefs.end();
  if (ok) {
    id_ = NODE_ID_UNSET;
    refreshLabel();
  }
  return ok;
}

void NodeId::refreshLabel() {
  if (id_ == NODE_ID_UNSET) {
    snprintf(label_, sizeof(label_), "NO ID");
  } else if (id_ == NODE_ID_ATTACKER) {
    snprintf(label_, sizeof(label_), "ATK");
  } else if (id_ == NODE_ID_SPARE) {
    snprintf(label_, sizeof(label_), "SPARE");
  } else {
    snprintf(label_, sizeof(label_), "D%u", (unsigned)id_);
  }
}
