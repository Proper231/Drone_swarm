#include "app/swarm_node.h"

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>

#include "util/log.h"

SwarmNode::SwarmNode()
    : display_(nodeId_),
      builder_(nodeId_),
      presence_(neighbors_),
      espnow_(faults_),
      raft_(nodeId_, builder_, espnow_),
      linear_(nodeId_, builder_, espnow_),
      commands_(*this) {
  Log::bind(nodeId_);
}

// ------------------------------------------------------------------ startup

void SwarmNode::begin() {
  Serial.begin(SERIAL_BAUD);
  delay(300);  // let the serial port come up before the first print

  led_.begin();
  nodeId_.load();
  display_.begin();

  // Brings up WiFi STA and pins the channel, so it must run before the MAC is
  // read or anything is sent.
  const bool espnowOk = espnow_.begin();
  snprintf(mac_, sizeof(mac_), "%s", WiFi.macAddress().c_str());

  builder_.begin();
  const int16_t loraSt = lora_.begin();

  Serial.println();
  Serial.printf("MAC: %s\n", mac_);
  Log::event("BOOT,label=%s,mac=%s,bootid=%lu,espnow=%u,ch=%u,lora=%d,oled=%u",
             nodeId_.label(), mac_, (unsigned long)builder_.bootId(),
             espnowOk ? 1u : 0u, (unsigned)espnow_.channel(), loraSt,
             display_.ok() ? 1u : 0u);

  if (!nodeId_.isSet()) {
    Serial.println(F("NO ID. Run: SETID <n>"));
  }
  if (!espnowOk) {
    Serial.println(F("ESP-NOW failed to start. Heartbeats will not be sent."));
  }
  if (!lora_.ready()) {
    Serial.printf("LoRa begin failed: %d. Check pins and TCXO voltage.\n",
                  loraSt);
  }

  commands_.printHelp();
  refreshScreen();
}

// ---------------------------------------------------------------- main loop

// The whole life of the board, one job per line. The order is part of the
// measured behaviour, so it stays as it is: commands first, then everything
// that has arrived, then what this node sends, then the protocol's timers.
void SwarmNode::update() {
  commands_.update();
  led_.update();
  receiveMessages();

  // A board with no ID must not join the swarm: it would show up as node 0 in
  // everyone's tables and corrupt the run.
  if (nodeId_.isSet() && heartbeatDue_.due()) {
    sendHeartbeat();
  }

  // A killed board is isolated by definition, so its protocol stops with it.
  // Without this it would sit there timing out and declaring itself leader of
  // a swarm that cannot hear it.
  if (!faults_.killed()) {
    protocol_->tick();
  }

  presence_.update();

  if (countersDue_.due()) {
    logCounters();
    refreshScreen();
  }

  lora_.poll();

  if (pinging_ && pingDue_.due()) {
    lora_.sendPing(nodeId_.get(), pingSeq_++);
  }

  display_.update();
}

void SwarmNode::receiveMessages() {
  Received rx;
  while (espnow_.poll(rx)) {
    neighbors_.record(rx.m.sender, rx.rssi, rx.m.seq, rx.m.bootId);
    protocol_->onMessage(rx.m);

    // Deliberately no log line per heartbeat. The counter summary and the
    // presence events carry what the analysis needs, and logging every
    // heartbeat would swamp the serial link. The protocol's own LEADER,
    // TAKEOVER and LEADER_LOST lines are what failover timing is measured
    // from.
    if (rx.m.type != msg::HEARTBEAT) {
      Log::event("RX,ESPNOW,from=%u,type=%s,term=%lu,leader=%u,rssi=%d",
                 (unsigned)rx.m.sender, msg::typeName(rx.m.type),
                 (unsigned long)rx.m.term, (unsigned)rx.m.leader,
                 (int)rx.rssi);
    }
  }
}

void SwarmNode::sendHeartbeat() {
  espnow_.send(
      builder_.make(msg::HEARTBEAT, protocol_->term(), protocol_->leader()));

  // The wait to the next heartbeat is drawn afresh each time, evenly either
  // side of the same 200 ms. Two boards that happen to transmit at the same
  // instant then do it once, not every beat until their clocks drift apart.
  // With HBJIT 0 nothing here runs and the period never moves.
  if (hbJitterMs_ > 0) {
    heartbeatDue_.setPeriod(ESPNOW_HB_MS - hbJitterMs_ +
                            esp_random() % (2 * hbJitterMs_ + 1));
  }
}

void SwarmNode::logCounters() {
  const EspNowRadio::Counters &c = espnow_.counters();
  const FaultInjector::Drops &d = faults_.drops();
  Log::event("COUNTERS,en_tx=%lu,en_rx=%lu,en_txfail=%lu,d_kill=%lu,d_jam=%lu,"
             "d_split=%lu,d_loss=%lu,d_bad=%lu,d_queue=%lu,lora_tx=%lu,"
             "lora_rx=%lu,neighbors=%u",
             (unsigned long)c.tx, (unsigned long)c.rx, (unsigned long)c.txFail,
             (unsigned long)d.kill, (unsigned long)d.jam,
             (unsigned long)d.split, (unsigned long)d.loss,
             (unsigned long)c.dropBad, (unsigned long)c.dropQueue,
             (unsigned long)lora_.txCount(), (unsigned long)lora_.rxCount(),
             (unsigned)neighbors_.heardWithin(NEIGHBOR_WINDOW_MS));
}

// ------------------------------------------------- what the commands ask for

bool SwarmNode::setNodeId(uint8_t id) {
  if (!nodeId_.save(id)) {
    return false;
  }
  // A protocol's behaviour depends on this node's place in the order, so a
  // new identity means starting it over.
  protocol_->reset();
  return true;
}

bool SwarmNode::clearNodeId() { return nodeId_.clear(); }

bool SwarmNode::selectProtocol(char letter) {
  LeaderProtocol *const choices[] = {&raft_, &linear_, &hierSlot_};
  for (LeaderProtocol *choice : choices) {
    if (choice->letter() == letter) {
      protocol_ = choice;
      resetProtocolState();
      return true;
    }
  }
  return false;
}

void SwarmNode::kill() {
  faults_.setKilled(true);
  setPinging(false);
}

void SwarmNode::revive() {
  faults_.setKilled(false);
  // Comes back as a fresh follower: no memory of who led before. It assumes
  // the protocol's starting state again and learns the real leader from the
  // first message carrying a higher term, exactly as a rebooted board would.
  protocol_->reset();
}

void SwarmNode::resetAll() {
  resetProtocolState();
  faults_.clear();
}

bool SwarmNode::setPinging(bool on) {
  if (on && !lora_.ready()) {
    return false;
  }
  pinging_ = on;
  lora_.setTxEnabled(on);
  if (on) {
    // Fire the first ping immediately instead of waiting a full second.
    pingDue_.expire();
  }
  return true;
}

bool SwarmNode::setHeartbeatJitter(uint32_t ms) {
  if (ms > ESPNOW_HB_JITTER_MAX_MS) {
    return false;
  }
  hbJitterMs_ = ms;
  // Back to the fixed period at once. A jittered one is drawn at the next
  // heartbeat, so the gap already running is left as it is.
  heartbeatDue_.setPeriod(ESPNOW_HB_MS);
  return true;
}

int16_t SwarmNode::startLora() { return lora_.begin(); }

void SwarmNode::stopLora() {
  setPinging(false);
  lora_.stop();
}

void SwarmNode::resetProtocolState() {
  protocol_->reset();
  neighbors_.reset();
  presence_.reset();
  espnow_.resetCounters();
  faults_.resetDrops();
}

// ------------------------------------------------------------------ reports

const char *SwarmNode::activeRadioName() const {
  // ESP-NOW is the only link carrying messages until Phase 5, when the link
  // manager picks between the two and this becomes a real decision.
  return espnow_.ready() ? espnow_.name() : "--";
}

void SwarmNode::refreshScreen() {
  Display::Status s;
  s.leader = protocol_->leader();
  s.term = protocol_->term();
  s.radio = activeRadioName();
  s.neighbors = neighbors_.heardWithin(NEIGHBOR_WINDOW_MS);
  s.tx = espnow_.counters().tx;
  s.rx = espnow_.counters().rx;
  s.killed = faults_.killed();
  s.jammed = faults_.jammed();
  s.split = faults_.splitOn();
  s.lossPct = faults_.lossPct();
  s.loraOk = lora_.ready();
  display_.show(s);
}

// One line, key=value, because the Pi parses it to learn which board is on
// which port. Add fields at the end; do not rename or reorder the others.
void SwarmNode::printStatus() const {
  const EspNowRadio::Counters &c = espnow_.counters();
  Serial.printf(
      "STATUS id=%u label=%s mac=%s proto=%c term=%lu leader=%u "
      "espnow=%s ch=%u neighbors=%u killed=%u jammed=%u loss=%u split=%u "
      "xband=%u hmac=%u lora=%s tx=%lu rx=%lu bootid=%lu hbjit=%lu "
      "votejit=%lu\n",
      (unsigned)nodeId_.get(), nodeId_.label(), mac_, protocol_->letter(),
      (unsigned long)protocol_->term(), (unsigned)protocol_->leader(),
      espnow_.ready() ? "OK" : "FAIL", (unsigned)espnow_.channel(),
      (unsigned)neighbors_.heardWithin(NEIGHBOR_WINDOW_MS),
      faults_.killed() ? 1u : 0u, faults_.jammed() ? 1u : 0u,
      (unsigned)faults_.lossPct(), faults_.splitOn() ? 1u : 0u,
      baselines_.xband ? 1u : 0u, baselines_.hmac ? 1u : 0u,
      lora_.ready() ? "OK" : "FAIL", (unsigned long)c.tx,
      (unsigned long)c.rx, (unsigned long)builder_.bootId(),
      (unsigned long)hbJitterMs_, (unsigned long)raft_.replyDelayMs());
}
