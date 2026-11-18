#include "comms/lora_radio.h"

#include <Arduino.h>
#include <SPI.h>

#include "config.h"
#include "util/log.h"

namespace {

// Set by the DIO1 interrupt, cleared by poll(). A plain flag rather than a
// member because an interrupt handler cannot be a member function, and a board
// has exactly one LoRa chip.
volatile bool g_packetFlag = false;

// Runs in interrupt context, so it only sets a flag. Everything else happens
// in poll().
IRAM_ATTR void onDio1() { g_packetFlag = true; }

}  // namespace

LoRaRadio::LoRaRadio()
    : radio_(new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST,
                        PIN_LORA_BUSY)) {}

int16_t LoRaRadio::begin() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

  lastError_ = radio_.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                            LORA_SYNC_WORD, LORA_POWER_DBM, LORA_PREAMBLE,
                            LORA_TCXO_VOLTS);
  if (lastError_ != RADIOLIB_ERR_NONE) {
    ready_ = false;
    return lastError_;
  }

  radio_.setPacketReceivedAction(onDio1);

  lastError_ = radio_.startReceive();
  ready_ = (lastError_ == RADIOLIB_ERR_NONE);
  return lastError_;
}

void LoRaRadio::stop() {
  radio_.standby();
  g_packetFlag = false;
  ready_ = false;
  txEnabled_ = false;
}

bool LoRaRadio::sendPing(uint8_t nodeId, uint32_t seq) {
  if (!ready_ || !txEnabled_) {
    return false;
  }

  char payload[32];
  snprintf(payload, sizeof(payload), "PING %u %lu", (unsigned)nodeId,
           (unsigned long)seq);

  // Blocking send. Fine for a 1 Hz ping at SF7 (about 50 ms on air). Phase 5
  // needs non-blocking sends plus backoff and scanChannel().
  const int16_t st = radio_.transmit(payload);

  // transmit() switches the radio out of receive and its completion also
  // trips DIO1, so clear the stale flag and go back to listening.
  g_packetFlag = false;
  radio_.startReceive();

  if (st != RADIOLIB_ERR_NONE) {
    lastError_ = st;
    Log::event("TX_FAIL,LORA,seq=%lu,err=%d", (unsigned long)seq, st);
    return false;
  }

  tx_++;
  Log::event("TX,LORA,seq=%lu", (unsigned long)seq);
  return true;
}

bool LoRaRadio::poll() {
  if (!ready_ || !g_packetFlag) {
    return false;
  }
  g_packetFlag = false;

  char buf[64];
  size_t len = radio_.getPacketLength();
  if (len == 0) {
    radio_.startReceive();
    return false;
  }
  if (len > sizeof(buf) - 1) {
    len = sizeof(buf) - 1;
  }

  const int16_t st = radio_.readData(reinterpret_cast<uint8_t *>(buf), len);
  buf[len] = '\0';

  if (st == RADIOLIB_ERR_NONE) {
    rx_++;
    rssi_ = static_cast<int>(radio_.getRSSI());
    snr_ = radio_.getSNR();
    Log::event("RX,LORA,data=%s,rssi=%d,snr=%.1f", buf, rssi_, snr_);
  } else if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    lastError_ = st;
    Log::event("DROP,LORA,reason=CRC");
  } else {
    lastError_ = st;
    Log::event("RX_FAIL,LORA,err=%d", st);
  }

  radio_.startReceive();
  return st == RADIOLIB_ERR_NONE;
}
