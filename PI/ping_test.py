#!/usr/bin/env python3
"""Run the two-board LoRa ping of step 1.6 and report loss and RSSI.

One board transmits "PING <id> <seq>" once per second, the other listens. Both
serial streams are read at once, so the TX count comes from the sender's own
log lines rather than being assumed from the duration.

    python3 ping_test.py --tx <port-a> --rx <port-b> --seconds 30

ANTENNA: both boards must have their LoRa antenna attached before this runs.
"""

from __future__ import annotations

import argparse
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from swarmlab.board import open_together
from swarmlab.event import Event
from swarmlab.ports import BAUD


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--tx", required=True, help="serial port of the sending board")
    ap.add_argument("--rx", required=True, help="serial port of the listening board")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--baud", type=int, default=BAUD)
    args = ap.parse_args()

    links, failed = open_together([args.tx, args.rx], args.baud)
    if failed:
        for port, error in failed.items():
            print(f"{port}: {error}", file=sys.stderr)
        for link in links:
            link.close()
        return 1
    tx, rx = links

    # What each board logged, keyed by port.
    logged: dict[str, list[Event]] = {tx.port: [], rx.port: []}

    def keep(link, pi_time, line):
        logged[link.port].append(Event.from_board_line(pi_time, line, link.node))

    try:
        for link in links:
            link.start_reading(keep)

        # Make sure neither board is already transmitting, then start the sender.
        rx.write("PING OFF")
        tx.write("PING OFF")
        time.sleep(0.5)
        tx.write("PING ON")

        print(f"Pinging for {args.seconds:.0f} s...", file=sys.stderr)
        time.sleep(args.seconds)

        tx.write("PING OFF")
        time.sleep(1.0)
    finally:
        for link in links:
            link.stop_reading()
        time.sleep(0.4)
        for link in links:
            link.close()

    # Only LoRa lines count. A board also logs RX for ESP-NOW protocol
    # messages, and those are not pings.
    def lora(events, *names):
        return [e for e in events if e.name in names and e.first == "LORA"]

    sent = lora(logged[tx.port], "TX")
    got = lora(logged[rx.port], "RX")
    bad = lora(logged[rx.port], "DROP", "RX_FAIL")

    print(f"\nSent     : {len(sent)}")
    print(f"Received : {len(got)}")
    print(f"Bad/CRC  : {len(bad)}")
    if sent:
        loss = 100.0 * (1.0 - len(got) / len(sent))
        print(f"Loss     : {loss:.1f}%")

    rssi = [float(e.get("rssi")) for e in got if e.get("rssi")]
    snr = [float(e.get("snr")) for e in got if e.get("snr")]
    if rssi:
        print(
            f"RSSI     : mean {statistics.mean(rssi):.1f} dBm "
            f"(min {min(rssi):.0f}, max {max(rssi):.0f})"
        )
    if snr:
        print(f"SNR      : mean {statistics.mean(snr):.1f} dB")

    for link in links:
        if link.error:
            print(f"{link.port}: {link.error}", file=sys.stderr)

    if not got:
        print(
            "\nNothing received. Check that both boards report lora=OK "
            "(STATUS), that they are on the same frequency and sync word, and "
            "that both antennas are attached.",
            file=sys.stderr,
        )
        return 1

    print("\nRecord these numbers in Docs/lab_notes.md.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
