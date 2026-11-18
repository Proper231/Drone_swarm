#include "app/command_processor.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/swarm_node.h"
#include "config.h"
#include "util/log.h"

namespace {

bool argIs(const char *arg, const char *want) {
  return arg != nullptr && strcmp(arg, want) == 0;
}

// "ON" gives true, "OFF" gives false. Returns false if it is neither.
bool parseOnOff(const char *arg, bool &on) {
  if (argIs(arg, "ON")) {
    on = true;
    return true;
  }
  if (argIs(arg, "OFF")) {
    on = false;
    return true;
  }
  return false;
}

const char *onOff(bool on) { return on ? "ON" : "OFF"; }

// "1,2,3,4" -> {1,2,3,4}. Returns how many ids were parsed.
uint8_t parseIdList(char *s, uint8_t *out, uint8_t max) {
  uint8_t n = 0;
  char *tok = strtok(s, ",");
  while (tok != nullptr && n < max) {
    const long v = strtol(tok, nullptr, 10);
    if (v > 0 && v < MAX_NODES) {
      out[n++] = static_cast<uint8_t>(v);
    }
    tok = strtok(nullptr, ",");
  }
  return n;
}

}  // namespace

// The whole command set. Some of these do nothing yet (PROTO C, XBAND, HMAC):
// they are accepted and remembered now so that the Pi scripts and the
// baseline switches in step 6.4 never need a firmware change mid-experiment.
const CommandProcessor::Command CommandProcessor::kCommands[] = {
    {"HELP",    "",              "list the commands",                    &CommandProcessor::cmdHelp},
    {"STATUS",  "",              "the whole state on one line",          &CommandProcessor::cmdStatus},
    {"ID",      "",              "print the stored node ID",             &CommandProcessor::cmdId},
    {"MAC",     "",              "print the Wi-Fi MAC address",          &CommandProcessor::cmdMac},
    {"SETID",   "<n>",           "store node ID (1-8, ATK 99, SPARE 98)", &CommandProcessor::cmdSetId},
    {"CLEARID", "",              "forget the stored node ID",            &CommandProcessor::cmdClearId},
    {"PROTO",   "A|B|C",         "pick the leader protocol",             &CommandProcessor::cmdProto},
    {"KILL",    "",              "go silent and deaf",                   &CommandProcessor::cmdKill},
    {"REVIVE",  "",              "come back as a fresh follower",        &CommandProcessor::cmdRevive},
    {"JAM",     "ON|OFF",        "emulated 2.4 GHz jamming",             &CommandProcessor::cmdJam},
    {"LOSS",    "<pct>",         "drop this % of received messages",     &CommandProcessor::cmdLoss},
    {"SPLIT",   "<a,b,c>|OFF",   "only hear these node IDs / everyone",  &CommandProcessor::cmdSplit},
    {"XBAND",   "ON|OFF",        "cross-band check (Phase 5)",           &CommandProcessor::cmdXband},
    {"HMAC",    "ON|OFF",        "message checking (Phase 6)",           &CommandProcessor::cmdHmac},
    {"HBJIT",   "<ms>",          "heartbeat jitter, +/- ms (0 is none)", &CommandProcessor::cmdHbJit},
    {"VOTEJIT", "<ms>",          "Raft vote reply waits 0..ms (0 is none)", &CommandProcessor::cmdVoteJit},
    {"LORA",    "ON|OFF",        "restart or stop the LoRa radio",       &CommandProcessor::cmdLora},
    {"PING",    "ON|OFF",        "LoRa ping once per second",            &CommandProcessor::cmdPing},
    {"RESET",   "",              "clear term, leader, tables and faults", &CommandProcessor::cmdReset},
    {"REBOOT",  "",              "restart the board",                    &CommandProcessor::cmdReboot},
};

const size_t CommandProcessor::kCommandCount =
    sizeof(CommandProcessor::kCommands) / sizeof(CommandProcessor::kCommands[0]);

CommandProcessor::CommandProcessor(SwarmNode &node) : node_(node) {}

void CommandProcessor::update() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (lineLen_ > 0) {
        line_[lineLen_] = '\0';
        execute(line_);
        lineLen_ = 0;
      }
    } else if (lineLen_ < sizeof(line_) - 1) {
      line_[lineLen_++] = c;
    } else {
      // Drop an over-long line rather than run a truncated, wrong command.
      lineLen_ = 0;
      Serial.println(F("ERR line too long"));
    }
  }
}

void CommandProcessor::printHelp() const {
  Serial.println(F("Commands:"));
  for (size_t i = 0; i < kCommandCount; ++i) {
    char usage[32];
    snprintf(usage, sizeof(usage), "%s %s", kCommands[i].name,
             kCommands[i].args);
    Serial.printf("  %-18s %s\n", usage, kCommands[i].help);
  }
}

void CommandProcessor::execute(char *line) {
  // Commands and their arguments are uppercase or numeric.
  for (char *p = line; *p != '\0'; ++p) {
    *p = toupper(static_cast<unsigned char>(*p));
  }

  char *verb = strtok(line, " ");
  if (verb == nullptr) {
    return;
  }
  char *arg = strtok(nullptr, " ");

  const Command *found = nullptr;
  for (size_t i = 0; i < kCommandCount; ++i) {
    if (strcmp(verb, kCommands[i].name) == 0) {
      found = &kCommands[i];
      break;
    }
  }

  if (found == nullptr) {
    Serial.printf("ERR unknown command: %s (try HELP)\n", verb);
  } else if (!(this->*(found->run))(arg)) {
    Serial.printf("ERR usage: %s %s\n", found->name, found->args);
  }

  // Most commands change something the screen shows, so refresh once here
  // rather than remembering to in every handler.
  node_.refreshScreen();
}

// ---------------------------------------------------------------- handlers

bool CommandProcessor::cmdHelp(char *) {
  printHelp();
  return true;
}

bool CommandProcessor::cmdStatus(char *) {
  node_.printStatus();
  return true;
}

bool CommandProcessor::cmdId(char *) {
  Serial.printf("ID %u %s\n", (unsigned)node_.nodeId().get(),
                node_.nodeId().label());
  return true;
}

bool CommandProcessor::cmdMac(char *) {
  Serial.printf("MAC %s\n", node_.mac());
  return true;
}

bool CommandProcessor::cmdSetId(char *arg) {
  if (arg == nullptr) {
    return false;
  }
  const long n = strtol(arg, nullptr, 10);
  if (n < 1 || n > 99) {
    Serial.println(F("ERR id must be 1..99"));
    return true;
  }
  if (!node_.setNodeId(static_cast<uint8_t>(n))) {
    Serial.println(F("ERR could not write NVS"));
    return true;
  }
  Serial.printf("OK SETID %ld %s\n", n, node_.nodeId().label());
  Log::event("SETID,%ld", n);
  return true;
}

bool CommandProcessor::cmdClearId(char *) {
  Serial.println(node_.clearNodeId() ? F("OK CLEARID") : F("ERR CLEARID"));
  return true;
}

bool CommandProcessor::cmdProto(char *arg) {
  if (arg == nullptr || arg[1] != '\0' || !node_.selectProtocol(arg[0])) {
    return false;
  }
  Serial.printf("OK PROTO %c\n", arg[0]);
  Log::event("PROTO,%c", arg[0]);
  return true;
}

bool CommandProcessor::cmdKill(char *) {
  node_.kill();
  Serial.println(F("OK KILL"));
  Log::event("KILL");
  return true;
}

bool CommandProcessor::cmdRevive(char *) {
  node_.revive();
  Serial.println(F("OK REVIVE"));
  Log::event("REVIVE");
  return true;
}

bool CommandProcessor::cmdJam(char *arg) {
  bool on;
  if (!parseOnOff(arg, on)) {
    return false;
  }
  node_.faults().setJammed(on);
  Serial.printf("OK JAM %s\n", onOff(on));
  Log::event("JAM,%s", onOff(on));
  return true;
}

bool CommandProcessor::cmdLoss(char *arg) {
  if (arg == nullptr) {
    return false;
  }
  const long pct = strtol(arg, nullptr, 10);
  if (pct < 0 || pct > 100) {
    Serial.println(F("ERR pct must be 0..100"));
    return true;
  }
  node_.faults().setLossPct(static_cast<uint8_t>(pct));
  Serial.printf("OK LOSS %ld\n", pct);
  Log::event("LOSS,%ld", pct);
  return true;
}

bool CommandProcessor::cmdSplit(char *arg) {
  if (argIs(arg, "OFF")) {
    node_.faults().splitOff();
    Serial.println(F("OK SPLIT OFF"));
    Log::event("SPLIT,OFF");
    return true;
  }
  if (arg == nullptr) {
    return false;
  }
  uint8_t ids[16];
  const uint8_t n = parseIdList(arg, ids, sizeof(ids));
  if (n == 0) {
    return false;
  }
  node_.faults().splitTo(ids, n);
  Serial.printf("OK SPLIT %u nodes\n", (unsigned)n);
  Log::event("SPLIT,n=%u", (unsigned)n);
  return true;
}

bool CommandProcessor::cmdXband(char *arg) {
  bool on;
  if (!parseOnOff(arg, on)) {
    return false;
  }
  node_.baselines().xband = on;
  Serial.printf("OK XBAND %s\n", onOff(on));
  Log::event("XBAND,%s", onOff(on));
  return true;
}

bool CommandProcessor::cmdHmac(char *arg) {
  bool on;
  if (!parseOnOff(arg, on)) {
    return false;
  }
  node_.baselines().hmac = on;
  Serial.printf("OK HMAC %s\n", onOff(on));
  Log::event("HMAC,%s", onOff(on));
  return true;
}

bool CommandProcessor::cmdHbJit(char *arg) {
  if (arg == nullptr) {
    return false;
  }
  const long ms = strtol(arg, nullptr, 10);
  if (ms < 0 || !node_.setHeartbeatJitter(static_cast<uint32_t>(ms))) {
    Serial.printf("ERR ms must be 0..%lu\n",
                  (unsigned long)ESPNOW_HB_JITTER_MAX_MS);
    return true;
  }
  Serial.printf("OK HBJIT %ld\n", ms);
  Log::event("HBJIT,%ld", ms);
  return true;
}

bool CommandProcessor::cmdVoteJit(char *arg) {
  if (arg == nullptr) {
    return false;
  }
  const long ms = strtol(arg, nullptr, 10);
  if (ms < 0 || !node_.setVoteReplyDelay(static_cast<uint32_t>(ms))) {
    Serial.printf("ERR ms must be 0..%lu\n",
                  (unsigned long)RAFT_REPLY_DELAY_MAX_MS);
    return true;
  }
  Serial.printf("OK VOTEJIT %ld\n", ms);
  Log::event("VOTEJIT,%ld", ms);
  return true;
}

bool CommandProcessor::cmdLora(char *arg) {
  bool on;
  if (!parseOnOff(arg, on)) {
    return false;
  }
  if (on) {
    const int16_t st = node_.startLora();
    Serial.printf("OK LORA ON st=%d\n", st);
    Log::event("LORA,ON,st=%d", st);
  } else {
    node_.stopLora();
    Serial.println(F("OK LORA OFF"));
    Log::event("LORA,OFF");
  }
  return true;
}

bool CommandProcessor::cmdPing(char *arg) {
  bool on;
  if (!parseOnOff(arg, on)) {
    return false;
  }
  if (!node_.setPinging(on)) {
    Serial.println(F("ERR lora not ready, cannot ping"));
    return true;
  }
  Serial.printf("OK PING %s\n", onOff(on));
  return true;
}

bool CommandProcessor::cmdReset(char *) {
  node_.resetAll();
  Serial.println(F("OK RESET"));
  Log::event("RESET");
  return true;
}

bool CommandProcessor::cmdReboot(char *) {
  Serial.println(F("OK REBOOT"));
  Serial.flush();
  delay(50);
  ESP.restart();
  return true;
}
