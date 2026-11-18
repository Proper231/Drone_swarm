# Flashing and board bring-up

How to get firmware onto a Heltec WiFi LoRa 32 V3, give it a permanent node ID,
and prove it actually works before trusting any measurement it produces.

Covers Phase 1 steps 1.1 to 1.7 and the Phase 2 acceptance checks.

Every board runs the **same binary**. What makes a board D1 rather than D5 is
one value in NVS flash, set once over serial. There is no per-board build.

Contents:

1. [One-time setup on a new PC](#1-one-time-setup-on-a-new-pc)
2. [Flashing and provisioning a board](#2-flashing-and-provisioning-a-board)
3. [Single-board checks](#3-single-board-checks)
4. [Two-board tests](#4-two-board-tests)
5. [Full-swarm tests](#5-full-swarm-tests)
6. [Serial commands](#6-serial-commands)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. One-time setup on a new PC

### Install the CP210x driver

The V3 talks to the host through a Silicon Labs CP2102 bridge
(`VID_10C4 / PID_EA60`). Windows does not ship a driver for it. Without one the
board enumerates but no COM port appears, and `pio device list` prints nothing
at all.

To check, open Device Manager and look for **CP2102 USB to UART Bridge
Controller** with a yellow warning triangle. Problem code 28 means the driver
is missing.

1. Right-click the device, **Update driver**, **Search automatically**
2. If Windows finds nothing, get the CP210x Universal Windows Driver from
   <https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers>,
   unzip it, then **Update driver**, **Browse my computer**, point at the folder
3. Unplug the board and plug it back in, so Windows re-detects it and binds the
   new driver

### Note on paths

PlatformIO was installed without shell commands, so `pio` on its own will not
resolve. Every command below uses the full path. The leading `&` is PowerShell's
call operator and is required because the path contains a space.

Python commands use PlatformIO's bundled interpreter, which already has
pyserial, so nothing needs installing into the system Python.

In VS Code you can skip the CLI for building: open `Firmware/` as the folder
(not the repository root, or the extension will not activate) and use the
toolbar.

---

## 2. Flashing and provisioning a board

Do the whole thing for one board before starting the next.

**Only one board plugged in while uploading.** These CP2102 chips carry no
unique serial number, so with several attached PlatformIO cannot tell them
apart and may upload to the wrong one. If you must have more than one
connected, pass `--upload-port COMx` explicitly.

The tests in sections 4 and 5 are the opposite: they need every board
connected at once.

### 2.1 Attach the antenna

Before power, every time. Transmitting without an antenna can damage the
SX1262. The firmware boots receive-only and will not transmit until something
sends `PING ON`, but make it a habit.

### 2.2 Plug in, using a data cable

Charge-only USB-C cables are the most common reason nothing appears.

### 2.3 Confirm the port

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
```

Expect a `COMx` entry. Nothing at all means the driver or the cable.

### 2.4 Flash and open the monitor

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d C:\drone\Drone_swarm\Firmware -t upload -t monitor
```

If it reports "Failed to connect": hold **PRG**, tap **RST**, release **PRG**,
run it again.

### 2.5 Set the node ID

Type into the same terminal and press Enter. `monitor_echo` is on, so you will
see what you type.

```
SETID 1
```

Expect `OK SETID 1 D1`, and the screen to change to `D1` / `BOOT OK`.

The ID lives in NVS and **survives reflashing**, so this is once per board, not
once per build. Only `pio run -t erase` wipes it; `CLEARID` forgets it
deliberately.

### 2.6 Record and label

Copy the MAC from the boot output into that board's row in
[boards.md](boards.md), then put a physical sticker on the board **before
unplugging it**. This is where boards get mixed up.

### 2.7 Exit

**Ctrl+C** closes the monitor. Unplug, and move to the next board.

### 2.8 Reflashing every connected board

For a new firmware build on boards that are already provisioned. This needs
the Windows duplicate-serial fix from section 7 in place, so that each board
holds its own COM port; then nothing has to be unplugged. Node IDs live in NVS
and survive, so there is no `SETID` afterwards.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"; $ports = @((& $pio device list --json-output | ConvertFrom-Json) | Where-Object { $_.hwid -match '10C4:EA60' } | Select-Object -ExpandProperty port | Sort-Object { [int]($_ -replace '\D','') }); "Flashing $($ports.Count): $($ports -join ', ')"; $fail = @(); foreach ($p in $ports) { Write-Host "=== $p ===" -ForegroundColor Cyan; & $pio run -d C:\drone\Drone_swarm\Firmware -t upload --upload-port $p; if ($LASTEXITCODE -ne 0) { $fail += $p } }; if ($fail) { Write-Host "FAILED: $($fail -join ', ')" -ForegroundColor Red } else { Write-Host "all $($ports.Count) uploaded OK" -ForegroundColor Green }
```

It picks out the boards by the CP2102's USB id, so it will not try to flash a
Bluetooth COM port, and it lists any that failed instead of stopping at the
first. **Check the count it prints before trusting the result**: fewer than
expected means a board is not enumerating and was silently left on the old
build.

Then confirm every board still knows who it is:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" C:\drone\Drone_swarm\PI\discover_boards.py
```

### ID assignment

| Sticker | Command | Role |
| --- | --- | --- |
| D1 to D8 | `SETID 1` to `SETID 8` | the measured swarm |
| ATK | `SETID 99` | attacker, Phase 6. Not a swarm member |
| SPARE | `SETID 98` | spare / second ping board |

With only some boards delivered, provision them as D1, D2, D3 and so on. ATK is
a Phase 6 role and nothing before then uses it, so there is no reason to assign
99 early.

---

## 3. Single-board checks

### 3.1 Boot output

```
MAC: 44:1B:F6:F8:5A:6C
LOG,0,1078,BOOT,label=NO ID,mac=44:1B:F6:F8:5A:6C,bootid=2893417,espnow=1,ch=1,lora=0,oled=1
```

| Field | Good value | Meaning |
| --- | --- | --- |
| `espnow` | `1` | ESP-NOW started and the broadcast peer was added |
| `ch` | `1` | WiFi channel. Must be the same on every board |
| `lora` | `0` | **0 means success.** It is a RadioLib status code, not a flag |
| `oled` | `1` | The screen acknowledged on I2C at 0x3C |

`lora=0` meaning success is the single most confusing line in the output. A
non-zero value there is the error code.

`LOG,0,...` starts with 0 until the node ID is set.

### 3.2 Screen

Unprovisioned: `NO ID`, `SETID <n> ?`, `LoRa: OK`, `TX0 RX0`.

Provisioned and running: the node label, the active radio, `L:` leader and
`T:` term, `N:` neighbours heard in the last second, and TX/RX counts. Any
active fault appears as a flag: `KILL`, `JAM`, `SPLIT`, `L20%`.

### 3.3 STATUS

`STATUS` prints the whole state on one line, which is the easiest thing to
paste into notes or an issue.

### What this proves

A board that boots with `espnow=1 lora=0 oled=1` and shows its label has
cleared the per-board half of the Phase 1 "Done when". It says nothing yet
about whether two boards can actually talk.

---

## 4. Two-board tests

These need **both** boards connected at the same time.

### 4.1 LoRa ping (step 1.6)

**Both antennas attached.** This is the first thing in the project that
transmits.

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" C:\drone\Drone_swarm\PI\ping_test.py --tx COM3 --rx COM4 --seconds 30
```

Reference result, two boards about 30 cm apart on a desk, Appendix A radio
settings:

```
Sent     : 31
Received : 31
Bad/CRC  : 0
Loss     : 0.0%
RSSI     : mean -12.9 dBm (min -16, max -12)
SNR      : mean 12.3 dB
```

31 packets in 30 s is correct, not an off-by-one: the first ping fires
immediately, then once a second.

An RSSI around -13 dBm is what 30 cm at 10 dBm gives. It is a best-case
control, not a representative link. Do not quote it in the report as a link
budget; that needs a distance sweep.

### 4.2 ESP-NOW bring-up and KILL (Phase 2 "Done when")

```powershell
cd C:\drone\Drone_swarm\PI
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" scenarios\smoke_two_boards.py
```

It settles for 10 s, kills node 1, revives it, and then measures the run from
the CSV it just wrote under `Data/smoke/`. Each run gets the next free number;
no script ever writes over an earlier capture.

Reference result:

```
--- heartbeats ---
  node 1: en_tx=71 en_rx=71
  node 2: en_tx=95 en_rx=71

--- KILL detection ---
  (1 before the kill, from boards resetting as their ports opened - ignored)
  node 2 noticed after 0.888 s  [ok]

--- result ---
  PASS  heartbeats both ways
  PASS  neighbour discovery
  PASS  KILL noticed in time
```

**Reading the detection time.** A node is declared lost 1000 ms after the last
message heard from it. When the KILL lands, that message is already up to one
heartbeat interval (200 ms) old, so a correct detection arrives between 0.8 s
and 1.1 s, not at exactly 1.0 s. A figure outside that band means the timing is
not behaving as designed.

**Both boards logging `NEIGHBOR_LOST` is correct.** Step 2.4 defines KILL as
"stop sending and ignore everything", so a killed node goes deaf as well as
silent and loses sight of its own neighbours.

**A free consistency check.** The gap between the two `en_tx` counters should
equal the silent period divided by the heartbeat interval. Above: 95 - 71 = 24,
and 5.001 s of silence at 200 ms is 25 heartbeats. Agreement to within one
heartbeat, from a counter that nothing in the timing path feeds.

### 4.3 Recording a plain capture

Without any fault injection:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" C:\drone\Drone_swarm\PI\logger.py --scenario smoke --protocol B --seconds 30
```

---

## 5. Full-swarm tests

### 5.1 Pi port inventory (step 1.7) — deferred to Week 7

**The Raspberry Pi is not set up and is not needed before Phase 7.** Decided
2026-11-15: its job was stable per-board identification, which the Windows
duplicate-serial fix in section 7 already provides on this host, and the
scripts work on either platform unchanged. It earns its keep at Phase 7's
160-plus unattended runs. Reasoning in [lab_notes.md](lab_notes.md).

Until then the eight-board test in 5.2 runs on Windows. When the Pi is built,
this is the procedure:

```bash
pip install -r PI/requirements.txt
python3 PI/discover_boards.py --markdown
```

This enumerates `/dev/serial/by-path/` and asks each board for `STATUS`.

Use `by-path`, never `by-id`: these CP2102 chips report no unique serial
number, so every board looks identical by id. `by-path` encodes the physical
hub port instead, and survives reboots.

Paste the output into the table in [boards.md](boards.md).

### 5.2 Eight-board acceptance test (Phase 2 "Done when")

The full form of the Phase 2 check, which two boards can only partly show. Run
with every swarm board connected:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" C:\drone\Drone_swarm\PI\scenarios\swarm_bringup.py
```

It settles the swarm, kills node 1, revives it, and grades the capture it
writes to `Data/bringup/`. Unlike `smoke_two_boards.py` it verifies that every
board hears **all seven** others, and times the detection separately for each.

Reference result, 2026-11-15, D1 to D8 on the pinned build:

```
--- neighbour discovery (expect 7 each) ---
  all eight boards at neighbours=7

--- KILL detection ---
  -> 7/7 noticed; mean 0.884 s, min 0.836, max 0.909

--- REVIVE recovery ---
  -> 7/7 saw it return; mean 0.097 s, max 0.134 s
```

Every board's screen should read `N:7` at the same time.

Two things to check in the capture beyond the PASS lines:

- **`d_queue` must be 0.** At eight boards each node receives about 35 messages
  a second against a 24-slot receive queue. Non-zero means messages are being
  dropped before the protocol sees them, and `RX_QUEUE_LEN` in `config.h` needs
  raising. It was 0 on every board in the reference run.
- **Detection times should stay in the 0.8 to 1.1 s band.** Going from two
  boards to eight moved the mean by 4 ms, so a figure well outside the band
  means congestion or a timing regression, not normal variation.

---

## 6. Serial commands

| Command | What it does |
| --- | --- |
| `HELP` / `STATUS` | List the commands / print the whole state on one line |
| `ID` / `MAC` | Print the stored node ID / the Wi-Fi MAC |
| `SETID <n>` / `CLEARID` | Store or forget the node ID in flash |
| `PROTO A\|B\|C` | Pick the leader protocol (stored, inert until Week 3) |
| `KILL` / `REVIVE` | Go silent and deaf / come back as a fresh follower |
| `JAM ON\|OFF` | Emulated 2.4 GHz jamming: drop all ESP-NOW traffic |
| `LOSS <pct>` | Randomly drop this percent of received messages |
| `SPLIT <a,b,c>` / `SPLIT OFF` | Only hear these node IDs / hear everyone |
| `XBAND ON\|OFF` | Cross-band check (flag only until Phase 5) |
| `HMAC ON\|OFF` | Message authentication (flag only until Phase 6) |
| `LORA ON\|OFF` | Restart or stop the LoRa radio |
| `PING ON\|OFF` | Send a LoRa ping once per second |
| `RESET` | Clear term, leader, tables and all injected faults |
| `REBOOT` | Restart the board |

---

## 7. Troubleshooting

| Symptom | Cause |
| --- | --- |
| `pio device list` prints nothing | CP210x driver missing (problem code 28), or a charge-only cable |
| "Failed to connect" during upload | Hold PRG, tap RST, release PRG, retry |
| Uploaded to the wrong board | Two boards attached at once. One at a time, or use `--upload-port` |
| "Access is denied" on a COM port | Something else already holds it. A monitor and `logger.py` cannot share a port; serial ports are exclusive |
| Serial works, screen blank | Vext (GPIO 36) not driven LOW, or the OLED pins are wrong |
| `oled=0` in the BOOT line | Screen did not acknowledge on I2C at 0x3C |
| `lora=` non-zero | Wrong SX1262 pins, or the TCXO voltage |
| `en_tx` climbing but `en_rx=0` on every board | Heartbeats leave but never arrive. Check every board reports the same `ch=` |
| `N:` stuck at 0 with `en_rx` rising | Messages arrive but are filtered. Check for a leftover `SPLIT` or `LOSS`; `RESET` clears both |
| `NEIGHBOR_LOST` events before a run starts | Normal. Opening a port resets that board and the others notice. Ignore anything before the first command |
| Garbage characters in the monitor | Baud mismatch; must be 115200 |
| Cannot see what you type | `monitor_echo = yes` missing from `platformio.ini` |

---

## Recording results

Every bring-up run goes in [lab_notes.md](lab_notes.md) with the date, the
boards used, and the numbers. Tick the Phase 1 sign-off boxes in
[boards.md](boards.md) only after seeing each one with your own eyes.

The report needs the measured values, not the intended ones.
