#include "comms/espnow_radio.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "comms/fault_injector.h"

namespace {

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// The driver's receive callback carries no user pointer, so the one radio a
// board has is reached through this.
EspNowRadio *g_instance = nullptr;

// Arduino core 3.x signature. Core 2.x examples use a different one and will
// not compile here.
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (g_instance == nullptr) {
    return;
  }
  const int16_t rssi = (info != nullptr && info->rx_ctrl != nullptr)
                           ? static_cast<int16_t>(info->rx_ctrl->rssi)
                           : 0;
  g_instance->enqueueFromWifiTask(data, len, rssi);
}

}  // namespace

EspNowRadio::EspNowRadio(FaultInjector &faults) : faults_(faults) {}

bool EspNowRadio::begin() {
  memset(&counters_, 0, sizeof(counters_));

  if (rxQueue_ == nullptr) {
    rxQueue_ = xQueueCreate(RX_QUEUE_LEN, sizeof(Received));
    if (rxQueue_ == nullptr) {
      return false;
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  delay(100);

  // Pinning the channel only takes effect reliably with promiscuous mode
  // toggled around it on some core versions. Harmless when it is not needed.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    ready_ = false;
    return false;
  }
  g_instance = this;
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  if (!esp_now_is_peer_exist(BROADCAST) &&
      esp_now_add_peer(&peer) != ESP_OK) {
    ready_ = false;
    return false;
  }

  ready_ = true;
  return true;
}

bool EspNowRadio::send(const msg::Msg &m) {
  if (!ready_ || faults_.blocksSend()) {
    return false;
  }
  const esp_err_t st =
      esp_now_send(BROADCAST, reinterpret_cast<const uint8_t *>(&m),
                   sizeof(msg::Msg));
  counters_.tx++;
  if (st != ESP_OK) {
    counters_.txFail++;
    return false;
  }
  return true;
}

bool EspNowRadio::poll(Received &out) {
  if (rxQueue_ == nullptr) {
    return false;
  }
  Received item;
  while (xQueueReceive(rxQueue_, &item, 0) == pdTRUE) {
    if (accept(item)) {
      out = item;
      return true;
    }
  }
  return false;
}

void EspNowRadio::resetCounters() { memset(&counters_, 0, sizeof(counters_)); }

// Runs on the WiFi task, not in loop(). It only copies into the queue so that
// nothing in the protocol path can stall ESP-NOW reception.
void EspNowRadio::enqueueFromWifiTask(const uint8_t *data, int len,
                                      int16_t rssi) {
  if (len != static_cast<int>(sizeof(msg::Msg))) {
    return;
  }
  Received item;
  memcpy(&item.m, data, sizeof(msg::Msg));
  item.rssi = rssi;
  item.rxMillis = millis();

  if (xQueueSend(rxQueue_, &item, 0) != pdTRUE) {
    counters_.dropQueue++;
  }
}

// Order matters here, in two ways.
//
// KILL and JAM mean "the radio is not working", so they come before anything
// that looks inside the frame.
//
// The validity check must come before the SPLIT lookup, because SPLIT indexes
// a table by sender and validity is what guarantees the sender is in range.
bool EspNowRadio::accept(const Received &item) {
  if (faults_.blocksReceive()) {
    return false;
  }
  if (!msg::looksValid(item.m, sizeof(msg::Msg)) || !msg::verify(item.m)) {
    counters_.dropBad++;
    return false;
  }
  if (faults_.dropsMessageFrom(item.m.sender)) {
    return false;
  }
  counters_.rx++;
  return true;
}
