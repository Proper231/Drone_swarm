# Drone Swarm Failover

Comparing leader failover protocols for resilient drone swarm communication in
search and rescue. Eight Heltec WiFi LoRa 32 V3 boards run two leader-election
protocols over ESP-NOW with a LoRa backup link, and a Raspberry Pi drives the
tests and collects the logs.

The full plan is in [Docs/swarm_project_steps.md](Docs/swarm_project_steps.md).

## Layout

```
Firmware/   PlatformIO project. One binary for every board.
PI/         Python scripts that drive boards and collect logs.
Data/       Raw CSV logs, one folder per scenario.
Analysis/   Scripts that turn Data/ into figures for the report.
            link_health.py: heartbeat delivery, gaps, Raft vote replies.
Docs/       Plan, board inventory, lab notes, report drafts.
```

### Firmware layout

One small class per file, grouped by what it is responsible for. To read the
firmware, start at `SwarmNode::update()` in `app/swarm_node.cpp`: it is the
whole life of the board, one job per line.

```
Firmware/src/
  main.cpp               entry point: one SwarmNode, setup() and loop()
  config.h               pins, radio parameters, timing. One place for every number
  app/                   the board as a whole
    swarm_node.*         SwarmNode: owns every part, runs the main loop
    command_processor.*  CommandProcessor: the serial command table
  board/                 this board's own hardware
    node_id.*            NodeId: permanent node ID in NVS
    display.*            Display: OLED status screen
    status_led.*         StatusLed: the once-a-second "loop is alive" blink
  comms/                 getting bytes on and off the air
    msg.*                the packed Msg struct, and msg::Builder which fills it
    radio.h              Radio: the interface a protocol sends through
    espnow_radio.*       EspNowRadio: primary link, queued receive
    lora_radio.*         LoRaRadio: backup link (text ping until Phase 5)
    fault_injector.*     FaultInjector: emulated KILL / JAM / LOSS / SPLIT
  protocol/              how a leader is chosen
    leader_protocol.h    LeaderProtocol: the interface A, B and C all implement
    raft_protocol.*      RaftProtocol: Protocol A
    linear_protocol.*    LinearProtocol: Protocol B
    inert_protocol.h     InertProtocol: "no protocol", and the stand-in for C
  swarm/                 what this node believes about the others
    neighbor_table.*     NeighborTable: who was heard, how recently, how strongly
    presence_monitor.*   PresenceMonitor: NEIGHBOR_UP / NEIGHBOR_LOST events
  util/
    log.*                Log: the one writer of LOG lines
    interval.h           Interval: "is this periodic job due?"
```

Two interfaces carry the design. `LeaderProtocol` is what the node drives, so
nothing outside `protocol/` knows or asks which protocol is running, and
everything around the protocols is identical for A and B by construction.
`Radio` is what a protocol sends through, so no protocol knows which link is
carrying its messages.

Still to come, in the folder each belongs to: `comms/link_manager.*` (Phase 5,
another `Radio`), `swarm/attacker.*` and the HMAC key header (Phase 6),
`protocol/hier_protocol.*` (Phase 9).

Includes are written from the source root (`#include "comms/msg.h"`), which
`-I src` in `platformio.ini` makes work from any subfolder.

### Python layout

The scripts you run are thin command lines. What they share lives in one
package, so a new scenario is a short file holding only its command sequence
and its grading.

```
PI/
  discover_boards.py     which board is on which port
  ping_test.py           the two-board LoRa ping (step 1.6)
  logger.py              record everything for a fixed time
  scenarios/             runs that drive the boards, record, then grade
    smoke_two_boards.py    Phase 2 check on two boards
    swarm_bringup.py       Phase 2 check on all eight; also the regression test
    linear_failover.py     Phase 3, Protocol B
    raft_failover.py       Phase 4, Protocol A
    heartbeat_jitter.py    experiment: does HBJIT stop the heartbeat loss
    vote_reply_delay.py    experiment: does VOTEJIT stop the lost vote replies
  swarmlab/              the shared package
    ports.py             find the serial ports, read a STATUS reply
    board.py             BoardLink: one board's serial port
    event.py             Event: one line of a capture, written and read back
    recorder.py          SwarmLogger: every board into one CSV, on one clock
    capture.py           Capture: a recorded run, loaded and queried
    scenario.py          Scenario: the base class every scenario extends
```

Every scenario takes `--grade <csv>` to re-grade a capture already on disk,
with no boards connected and no pyserial installed. A run and its grade only
ever meet through the CSV file, which is what makes a capture evidence.

## What works today (Week 4)

Phases 1 to 4. Boards bring themselves up, find each other over ESP-NOW, elect
a leader by either protocol, and the host records everything they say:

- node ID in NVS, OLED status screen, LoRa ping between two boards (Phase 1)
- one fixed WiFi channel, one packed `Msg` struct, heartbeats every 200 ms
  (each gap jittered by 20 ms, so boards do not collide beat after beat),
  a neighbour table, and the full step 2.4 command set (Phase 2)
- emulated faults that actually work: `KILL`, `JAM`, `LOSS <pct>`, `SPLIT`
- one CSV per run, stamped with the host's clock, never overwritten
- **Protocol B** (`PROTO B`): node 1 leads, and when the leader dies the next
  in line takes over; a revived leader rejoins as a follower (Phase 3)
- **Protocol A** (`PROTO A`): Raft leader election. Random 1 to 2 s timeouts,
  one vote per term, a majority of 5 of the 8. When the leader dies exactly
  one new leader is elected on a higher term; with four boards left nobody
  is; a leader that comes back steps down (Phase 4)

On 8 boards both protocols fail over in about 0.9 s. Two problems turned up
on the way, both in [Docs/lab_notes.md](Docs/lab_notes.md). Heartbeats sent
on a fixed period were lost in bursts long enough to depose a healthy
leader; jittering them fixed it. Raft's vote replies collided, because every
voter answered at once, and a third were lost; a random wait of up to 50 ms
before each reply fixed that (both 2026-11-17).

**The LoRa backup link is next.** `PROTO C` is accepted and reported but
selects a stand-in that does nothing. `XBAND` and `HMAC` are likewise flags
waiting for Phases 5 and 6, so the baseline switches in step 6.4 will never need a
firmware change.

Every event is printed in the Phase 2.5 log format:

```
LOG,<node>,<millis>,<event>,<field>=<value>,...
```

## Getting started

### 1. Tools

Install VS Code, the PlatformIO extension, Python 3 and Git. PlatformIO's CLI
comes with the extension; to get `pio` on its own:

```bash
pip install platformio
```

### 2. Build and flash

Full procedure, from the CP210x driver through to the bring-up tests and what
their numbers should look like, is in
[Docs/flashing_boards.md](Docs/flashing_boards.md). The short version, with one
board on USB:

```bash
pio run -d Firmware -t upload -t monitor
```

The board prints its MAC address, then `NO ID` until you give it one.

### 3. Give the board its ID

Type into the serial monitor (D1-D8 are 1-8, ATK is 99, SPARE is 98):

```
SETID 1
```

The ID lives in NVS flash, so it survives reflashing and every board runs the
identical binary. `STATUS` prints the current state on one line.

### 4. Check the screen

The OLED should show the label (`D1`) and `BOOT OK`. A blank screen is almost
always Vext (GPIO 36) not pulled LOW.

### 5. LoRa ping between two boards

**Attach both antennas first.** Transmitting without an antenna can damage the
SX1262, so the firmware boots receive-only and only transmits after `PING ON`.

Driven by hand, with two serial monitors open:

```
PING ON
```

on one board; the other logs each packet with its RSSI. Or let the Pi do it and
get loss and RSSI statistics:

```bash
python3 PI/ping_test.py --tx /dev/serial/by-path/<a> --rx /dev/serial/by-path/<b> --seconds 30
```

### 6. Pi inventory — deferred to Week 7

The Raspberry Pi is not set up, and nothing before Phase 7 needs it: Windows
gives stable per-board COM ports via the duplicate-serial fix, and the scripts
handle either platform unchanged. See [Docs/lab_notes.md](Docs/lab_notes.md)
for the reasoning. When you build it:

```bash
pip install -r PI/requirements.txt
python3 PI/discover_boards.py
```

This enumerates `/dev/serial/by-path/` (not `/dev/ttyUSB*`, which gets
reshuffled on reboot) and asks every board for its ID and MAC. Copy the results
into [Docs/boards.md](Docs/boards.md).

### 7. Record a run

With boards connected and provisioned:

```bash
python3 PI/logger.py --scenario smoke --protocol B --seconds 30
```

That writes `Data/smoke/B_run<N>.csv`, where N is the next number not already
on disk. A capture is evidence, so nothing ever writes over one: re-running a
scenario adds a file.

A run that sends commands at particular moments and then grades itself is a
scenario. It extends `Scenario` and supplies two methods:

```python
class LeaderKill(Scenario):
    name = "s1_leader_kill"          # Data/s1_leader_kill/
    protocol = "B"                   # B_run<N>.csv

    def drive(self, log):
        log.broadcast("PROTO B")
        log.broadcast("RESET")
        self.wait(10, "settling")
        log.send("1", "KILL")        # recorded on the same clock as the boards
        self.wait(20, "leader killed")

    def grade(self, capture):
        t_kill = capture.command_time("1", "KILL")
        takeovers = capture.select("TAKEOVER", start=t_kill)
        return [Check("exactly one new leader", len(takeovers) == 1)]
```

The base class checks the boards are present, picks the run number, prints the
PASS/FAIL summary and provides `--grade`.

## Serial commands

| Command | What it does |
| --- | --- |
| `HELP` | List the commands |
| `STATUS` | One line of current state |
| `ID` / `MAC` | Print the stored node ID / the Wi-Fi MAC |
| `SETID <n>` / `CLEARID` | Store or forget the node ID in flash |
| `PROTO A\|B\|C` | Pick the leader protocol: A Raft, B linear order. C is stored and inert until Phase 9 |
| `KILL` / `REVIVE` | Go silent and deaf / come back as a fresh follower |
| `JAM ON\|OFF` | Emulated 2.4 GHz jamming: drop all ESP-NOW traffic |
| `LOSS <pct>` | Randomly drop this percent of received messages |
| `SPLIT <a,b,c>` / `SPLIT OFF` | Only hear these node IDs / hear everyone |
| `XBAND ON\|OFF` | Cross-band check (flag only until Phase 5) |
| `HMAC ON\|OFF` | Message authentication (flag only until Phase 6) |
| `HBJIT <ms>` | Heartbeat jitter: each gap drawn from 200 ± ms. 20 by default; 0 is the fixed 200 ms every capture before 2026-11-17 was taken with |
| `VOTEJIT <ms>` | Raft only: each voter holds its reply for a random 0 to ms. 50 by default, so replies do not collide; 0 answers at once, as every Raft capture before 2026-11-17 (late night) was taken |
| `LORA ON\|OFF` | Restart or stop the LoRa radio |
| `PING ON\|OFF` | Send a LoRa ping once per second |
| `RESET` | Clear term, leader, tables and all injected faults |
| `REBOOT` | Restart the board |

`SPLIT` takes a list of node IDs rather than a group name. The effect asked for
in scenario 7 is the same — send `SPLIT 1,2,3,4` to nodes 1-4 and
`SPLIT 5,6,7,8` to nodes 5-8 — and a list is what the Phase 9 movement
extension needs anyway, since it sends per-board ignore lists as drones
"move" apart.

## Safety

- Attach the LoRa antenna before a board transmits.
- Keep boards on a non-conductive surface.
- Jamming and network splits are emulated in firmware. Never use a real jammer.
