// Pins and timing for the Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262).
//
// Every pin below was checked against Heltec's official V3 pin map on
// 2026-11-08 and matches it exactly. The V2 board used different pins and most
// tutorials online are still written for V2, so do not "correct" these from a
// web example.
//
// Two things the pin map does not state, and only a powered board settles:
// that Vext is active LOW (its pull-up implies it), and the 1.8 V TCXO control
// voltage. radio.begin() returning 0 confirms the latter.

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------- board pins

// Onboard white LED.
constexpr uint8_t PIN_LED = 35;

// Vext controls power to the OLED. Active LOW: drive it LOW to turn the
// screen on. A blank screen is almost always this pin left HIGH.
constexpr uint8_t PIN_VEXT = 36;

// OLED (SSD1306 128x64) on I2C.
constexpr uint8_t PIN_OLED_SDA = 17;
constexpr uint8_t PIN_OLED_SCL = 18;
constexpr uint8_t PIN_OLED_RST = 21;

// SX1262 LoRa radio on SPI.
constexpr uint8_t PIN_LORA_NSS  = 8;
constexpr uint8_t PIN_LORA_SCK  = 9;
constexpr uint8_t PIN_LORA_MOSI = 10;
constexpr uint8_t PIN_LORA_MISO = 11;
constexpr uint8_t PIN_LORA_RST  = 12;
constexpr uint8_t PIN_LORA_BUSY = 13;
constexpr uint8_t PIN_LORA_DIO1 = 14;

// PRG button, shared with the ESP32-S3 boot strap pin.
constexpr uint8_t PIN_BUTTON = 0;

// ---------------------------------------------------------- radio parameters

// US 902-928 MHz band. SF7 keeps packets short, which matters once eight
// boards share one channel (Phase 5).
constexpr float   LORA_FREQ_MHZ    = 915.0f;
constexpr float   LORA_BW_KHZ      = 125.0f;
constexpr uint8_t LORA_SF          = 7;
constexpr uint8_t LORA_CR          = 5;      // 4/5
constexpr uint8_t LORA_SYNC_WORD   = 0x12;   // private network
constexpr int8_t  LORA_POWER_DBM   = 10;     // start low, raise only if needed
constexpr uint16_t LORA_PREAMBLE   = 8;
constexpr float   LORA_TCXO_VOLTS  = 1.8f;   // V3 has a TCXO; 0 would hang begin()

// ------------------------------------------------------------------- node IDs

// D1..D8 are 1..8, the Amazon boards are ATK 99 and SPARE 98 (step 1.4).
constexpr uint8_t NODE_ID_UNSET    = 0;
constexpr uint8_t NODE_ID_ATTACKER = 99;
constexpr uint8_t NODE_ID_SPARE    = 98;

// -------------------------------------------------------------------- timing

constexpr uint32_t LED_BLINK_MS     = 1000;  // step 1.3: one blink per second
constexpr uint32_t LED_FLASH_MS     = 50;    // how long each blink stays lit
constexpr uint32_t DISPLAY_MS       = 250;
constexpr uint32_t LORA_PING_MS     = 1000;  // step 1.6
constexpr uint32_t SERIAL_BAUD      = 115200;

// ------------------------------------------------------------------- ESP-NOW

// Every board must sit on the same channel or nothing arrives (step 2.1).
constexpr uint8_t  ESPNOW_CHANNEL   = 1;

// Appendix A: 200 ms heartbeat on ESP-NOW. The mean gap; see the jitter below.
constexpr uint32_t ESPNOW_HB_MS     = 200;

// Heartbeat jitter. Each gap between heartbeats is drawn afresh from 200-J to
// 200+J ms, so the mean rate is still Appendix A's 200 ms.
//
// A deviation from Appendix A's fixed period, decided 2026-11-17 on a
// measurement. With a fixed period, boards whose heartbeats fall on the same
// instant keep colliding for seconds: five 30 s runs delivered 90.1% of
// heartbeats, the worst 79.7%, with healthy boards losing sight of each other
// for a second 68 times, and one such gap deposed a healthy Raft leader. With
// 20 ms of jitter the same boards delivered 99.5% with no gaps
// (Data/hbjitter/none_run1.csv, Docs/lab_notes.md).
//
// HBJIT <ms> changes it at run time. HBJIT 0 is the fixed 200 ms that every
// capture before 2026-11-17 was taken with.
constexpr uint32_t ESPNOW_HB_JITTER_MS     = 20;
constexpr uint32_t ESPNOW_HB_JITTER_MAX_MS = 50;

// "Neighbours heard in the last second" on the screen, and the figure the
// Phase 5 link manager will turn into a loss percentage.
constexpr uint32_t NEIGHBOR_WINDOW_MS = 1000;

// How often that window is re-checked. Much finer than the window itself,
// because the Phase 2 check is that a KILL is noticed within about a second
// and a 1 Hz scan could not resolve that.
constexpr uint32_t PRESENCE_SCAN_MS = 100;

// Per-second counter summary instead of a log line per heartbeat (step 2.5).
constexpr uint32_t COUNTERS_MS      = 1000;

// Node IDs run 1..8 for the swarm plus 98 and 99, so tables are indexed
// directly by ID. 100 slots of a few bytes each is not worth compressing.
constexpr uint8_t  MAX_NODES        = 100;

// Receive queue between the ESP-NOW callback (WiFi task) and loop().
constexpr uint8_t  RX_QUEUE_LEN     = 24;

// ------------------------------------------- Protocol B, linear (Appendix A)

// Node 1 leads, then 2, 3 ... 8. When the leader goes quiet, every other node
// waits T_BASE plus one T_STEP per place it sits behind the first node still
// in line, so the successor declares first and the rest stay quiet.
constexpr uint32_t LINEAR_T_BASE_MS = 1000;  // 5 missed heartbeats
constexpr uint32_t LINEAR_T_STEP_MS = 500;

// Non-pre-emptive: a revived node rejoins as a follower and does not take the
// lead back from a healthy leader. Decided 2026-11-15 so that Protocol B
// differs from Raft only in how a leader is chosen, not also in what happens
// on recovery, and so that recovery does not add leader changes to the counts
// scenarios 2 and 4 measure. See Docs/lab_notes.md.
constexpr bool LINEAR_PREEMPTIVE = false;

// --------------------------------------------- Protocol A, Raft (Appendix A)

// A node that hears no leader for a random time in this range stands for
// election. Each wait is a fresh draw, which is what keeps two nodes from
// standing at the same moment round after round.
constexpr uint32_t RAFT_TIMEOUT_MIN_MS = 1000;
constexpr uint32_t RAFT_TIMEOUT_MAX_MS = 2000;

// Of the full swarm, not of the nodes alive. That is why Raft stops electing
// below 5 nodes, which is one of the things the tests measure (step 4.2).
constexpr uint8_t RAFT_MAJORITY = SWARM_SIZE / 2 + 1;

// The longest a voter waits before answering a VOTE_REQ. Each reply waits a
// fresh random time from 0 to this.
//
// A deviation from raft_design.md, which answers at once, decided 2026-11-17
// on a measurement. Every voter hears a VOTE_REQ at the same instant, so
// answering at once means answering together, and replies sent together do
// not all arrive. Over four runs of Data/votejitter/, replies sent at once
// reached the candidate 62 to 66% of the time and 16 of 120 first rounds
// failed, each costing about a second. With a wait of 0 to 20 ms, 85 to 90%
// and none failed; with 0 to 50 ms, 95 to 98% and none failed, and 50 beat 20
// in every block of a run that tested both. A won round takes about 6 ms
// longer. See Docs/lab_notes.md.
//
// VOTEJIT <ms> changes it at run time. VOTEJIT 0 answers at once, as every
// Raft capture before 2026-11-17 (late night) was taken.
constexpr uint32_t RAFT_REPLY_DELAY_MS     = 50;
constexpr uint32_t RAFT_REPLY_DELAY_MAX_MS = 100;
