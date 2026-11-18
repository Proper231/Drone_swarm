// ESP-NOW broadcast link, the swarm's primary radio (steps 2.1 and 2.3).
//
// Reception is queued, not handled inline. The ESP-NOW driver calls back on
// the WiFi task; that callback only copies the frame into a queue, and poll()
// drains it from the main loop. Doing protocol work inside the callback would
// stall reception and bend the very failover timings this project measures.

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

#include "comms/radio.h"
#include "config.h"

class FaultInjector;

class EspNowRadio : public Radio {
 public:
  // The radio's own half of the COUNTERS log line. What the emulated faults
  // dropped is counted by the FaultInjector.
  struct Counters {
    uint32_t tx;         // messages handed to the driver
    uint32_t txFail;     // the driver refused one
    uint32_t rx;         // accepted after every filter
    uint32_t dropBad;    // malformed, or failed verification
    uint32_t dropQueue;  // receive queue was full
  };

  explicit EspNowRadio(FaultInjector &faults);

  // Brings up WiFi STA, pins the channel and starts ESP-NOW with a broadcast
  // peer. Returns false if any of that failed.
  bool begin();

  const char *name() const override { return "ESPNOW"; }
  bool ready() const override { return ready_; }
  bool send(const msg::Msg &m) override;
  bool poll(Received &out) override;

  uint8_t channel() const { return ESPNOW_CHANNEL; }

  const Counters &counters() const { return counters_; }
  void resetCounters();

  // For the ESP-NOW driver's callback only. Runs on the WiFi task.
  void enqueueFromWifiTask(const uint8_t *data, int len, int16_t rssi);

 private:
  // Applies the fault filters and the validity check, in the one order that
  // is safe. See the .cpp.
  bool accept(const Received &item);

  FaultInjector &faults_;
  QueueHandle_t rxQueue_ = nullptr;
  bool ready_ = false;
  Counters counters_ = {};
};
