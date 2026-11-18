#!/usr/bin/env python3
"""Does heartbeat jitter stop the bursts of lost heartbeats? An experiment,
not an acceptance test.

What was seen (lab notes, 2026-11-16): in some runs, pairs of healthy boards
stop hearing each other for a second or more, under either protocol, and one
such gap deposed a healthy Raft leader.

The theory: every board sends a heartbeat every 200 ms exactly, and the
scripts reset all the boards at the same instant, so the heartbeats start in
step. Two boards that land on the same instant transmit together and keep
doing so, beat after beat, until their clocks drift apart.

The test: the same boards, in the same place, in the same few minutes, run
with the heartbeat as it is (`HBJIT 0`) and with each gap drawn afresh from
200 +/- J ms (`HBJIT J`), turn and turn about. Every arm starts from a REBOOT
of all the boards, which gives a fresh draw of how their heartbeats line up,
and no protocol is selected, so heartbeats are the only traffic. A STATUS to
every board every few seconds does what a scenario does anyway: it stalls
each loop for a moment and shuffles the line-up.

    python3 scenarios/heartbeat_jitter.py
    python3 scenarios/heartbeat_jitter.py --rounds 5 --seconds 30 --jitter 20
    python3 scenarios/heartbeat_jitter.py --grade ../Data/hbjitter/none_run1.csv

The theory is supported if the arms without jitter show the loss and the arms
with it do not. If the arms without jitter show nothing either, the run says
nothing: the loss comes and goes, so run it again.
"""

from __future__ import annotations

import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swarmlab.scenario import Check, Scenario

BOOT_S = 5.0      # REBOOT to heartbeats flowing again
STIR_S = 5.0      # a STATUS to every board this often
SKIP_S = 2.0      # start of each arm left out: tables and counters refilling

# What "the loss" means, from the captures on disk: a healthy quiet swarm
# delivers 98 to 100% of heartbeats and never loses sight of a neighbour.
GOOD_DELIVERY = 0.98
RATE_TOLERANCE = 0.02


class HeartbeatJitter(Scenario):
    name = "hbjitter"
    protocol = "none"
    jitter = 20
    rounds = 5
    seconds = 30.0
    reboot = True

    @classmethod
    def add_arguments(cls, parser) -> None:
        parser.add_argument("--jitter", type=int, default=cls.jitter,
                            help="ms either side of 200 in the jittered arms "
                                 f"(default {cls.jitter}, at most 50)")
        parser.add_argument("--rounds", type=int, default=cls.rounds,
                            help="pairs of arms to run "
                                 f"(default {cls.rounds})")
        parser.add_argument("--seconds", type=float, default=cls.seconds,
                            help=f"length of each arm (default {cls.seconds:.0f})")
        parser.add_argument("--no-reboot", action="store_true",
                            help="do not REBOOT the boards before each arm; "
                                 "every arm then keeps the line-up the run "
                                 "started with")

    def configure(self, args) -> None:
        self.jitter = args.jitter
        self.rounds = args.rounds
        self.seconds = args.seconds
        self.reboot = not args.no_reboot

    # ------------------------------------------------------------- driving

    def drive(self, log) -> None:
        for round_no in range(1, self.rounds + 1):
            # Which arm goes first swaps each round, so neither always runs
            # on boards that have been up longer.
            order = (0, self.jitter) if round_no % 2 else (self.jitter, 0)
            for jitter in order:
                self.arm(log, round_no, jitter)
        # Leave the boards as a fresh boot would, on the firmware's own
        # default, not on whatever the last arm set. Without the reboot they
        # keep that setting until a port is next opened, which resets them.
        if self.reboot:
            log.broadcast("REBOOT")

    def arm(self, log, round_no: int, jitter: int) -> None:
        if self.reboot:
            log.broadcast("REBOOT")
            self.wait(BOOT_S, f"round {round_no}, jitter {jitter}: rebooting")
        else:
            print(f"round {round_no}, jitter {jitter}", file=sys.stderr)
        log.broadcast(f"HBJIT {jitter}")
        log.broadcast("RESET")
        log.mark(f"arm,jitter={jitter},round={round_no}")

        end = time.monotonic() + self.seconds
        while True:
            left = end - time.monotonic()
            if left <= 0:
                break
            time.sleep(min(STIR_S, left))
            if end - time.monotonic() > 1.0:
                log.poll_status()

    # ------------------------------------------------------------- grading

    def grade(self, capture) -> list[Check] | None:
        marks = [e for e in capture.select("MARK") if e.first == "arm"]
        if not marks:
            print("no arms in this capture: not a heartbeat jitter run",
                  file=sys.stderr)
            return None

        # An arm ends where the next one is set up, or where the run closes.
        boundaries = sorted(
            [e.time for e in capture.select("CMD", source="pi")
             if e.fields == "REBOOT" or e.fields.startswith("HBJIT ")]
            + [e.time for e in capture.select("MARK") if e.fields == "end"]
        )

        arms = []
        for mark in marks:
            later = [t for t in boundaries if t > mark.time]
            end = later[0] if later else capture.events[-1].time
            arms.append(self.measure(capture, mark, end))

        print("\n--- each arm ---")
        print("  round  jitter  delivery  worst second  1 s gaps  sent/s  boards")
        for a in arms:
            print(f"  {a['round']:>5}  {a['jitter']:>4} ms  "
                  f"{100 * a['delivery']:7.1f}%  "
                  f"{a['worst']:>5} of {a['expect']:<4}  {a['gaps']:>8}  "
                  f"{a['rate']:6.2f}  {a['boards']:>6}")
            for line in a["gap_lines"]:
                print(f"         {line}")

        plain = [a for a in arms if a["jitter"] == 0]
        jittered = [a for a in arms if a["jitter"] != 0]

        print("\n--- together ---")
        for label, group in (("without jitter", plain), ("with jitter", jittered)):
            if not group:
                print(f"  {label}: no arms")
                continue
            deliveries = [a["delivery"] for a in group]
            print(f"  {label}: {len(group)} arm(s), delivery mean "
                  f"{100 * statistics.mean(deliveries):.1f}%, worst arm "
                  f"{100 * min(deliveries):.1f}%, one-second gaps "
                  f"{sum(a['gaps'] for a in group)}, sent/s "
                  f"{statistics.mean(a['rate'] for a in group):.2f}")

        complete = all(a["boards"] == len(self.swarm) for a in arms)
        obeyed = all(a["obeyed"] for a in arms)
        same_rate = (bool(plain) and bool(jittered) and abs(
            statistics.mean(a["rate"] for a in jittered)
            / statistics.mean(a["rate"] for a in plain) - 1) <= RATE_TOLERANCE)
        reproduced = any(a["gaps"] > 0 or a["delivery"] < GOOD_DELIVERY
                         for a in plain)
        no_gaps = bool(jittered) and all(a["gaps"] == 0 for a in jittered)
        delivered = bool(jittered) and all(a["delivery"] >= GOOD_DELIVERY
                                           for a in jittered)

        print("\n--- what it shows ---")
        if not (complete and obeyed):
            print("  nothing: the arms did not run as intended. If no board "
                  "reported hbjit=, the firmware on them has no HBJIT command.")
        elif not reproduced:
            print("  nothing either way: the arms without jitter lost nothing "
                  "this time. The loss comes and goes, so run it again.")
        elif no_gaps and delivered:
            print("  the theory holds up: the loss appeared without jitter "
                  "and not with it.")
        else:
            print("  the theory does not hold up: the loss appeared with "
                  "jitter too, so something else is losing heartbeats.")

        return [
            Check(f"every arm recorded on all {len(self.swarm)} boards", complete),
            Check("every board ran the jitter it was told to", obeyed),
            Check("jitter leaves the heartbeat rate unchanged (within "
                  f"{100 * RATE_TOLERANCE:.0f}%)", same_rate),
            Check("without jitter: the loss showed up", reproduced),
            Check("with jitter: no one-second gap between healthy boards",
                  no_gaps),
            Check(f"with jitter: at least {100 * GOOD_DELIVERY:.0f}% delivered "
                  "in every arm", delivered),
        ]

    def measure(self, capture, mark, end) -> dict:
        """One arm: what was sent, what arrived, and who lost sight of whom."""
        jitter = mark.number("jitter")
        start = mark.time + SKIP_S

        # First and last COUNTERS line of each board in the arm. The counters
        # only count heartbeats here, because no protocol is running.
        spans = {}
        worst = None
        seconds = []
        for node in self.swarm:
            rows = capture.select("COUNTERS", node=node, start=start, before=end)
            if len(rows) < 2:
                continue
            spans[node] = (rows[-1].number("en_tx") - rows[0].number("en_tx"),
                           rows[-1].number("en_rx") - rows[0].number("en_rx"),
                           rows[-1].time - rows[0].time)
            per_second = [b.number("en_rx") - a.number("en_rx")
                          for a, b in zip(rows, rows[1:])]
            low = min(per_second)
            worst = low if worst is None else min(worst, low)
            seconds.append(rows[-1].time - rows[0].time)

        sent = sum(tx for tx, _, _ in spans.values())
        heard = sum(rx for _, rx, _ in spans.values())
        # Each heartbeat should reach every other board.
        expected = sum(sent - tx for tx, _, _ in spans.values())
        rate = (statistics.mean(tx / t for tx, _, t in spans.values() if t > 0)
                if spans else 0.0)

        # Nothing is killed or jammed in this scenario, so every NEIGHBOR_LOST
        # is one healthy board going a second without hearing another.
        gaps = capture.select("NEIGHBOR_LOST", start=start, before=end)
        pairs = {}
        for e in gaps:
            pairs.setdefault(e.get("node"), []).append(e.node)
        gap_lines = [f"node {lost} not heard for 1 s by {sorted(set(by))}"
                     for lost, by in sorted(pairs.items())]

        # What the boards themselves said they were running.
        reported = {}
        for e in capture.select("RAW", start=mark.time, before=end):
            if e.fields.startswith("STATUS "):
                # Read here rather than with ports.parse_status, which needs
                # pyserial, and grading a capture must not.
                fields = dict(token.split("=", 1)
                              for token in e.fields.split()[1:] if "=" in token)
                reported[e.node] = fields.get("hbjit")
        obeyed = (len(reported) >= len(spans) > 0
                  and all(v == str(jitter) for v in reported.values()))

        return {
            "round": mark.number("round"),
            "jitter": jitter,
            "delivery": heard / expected if expected else 0.0,
            "worst": worst if worst is not None else 0,
            "expect": 5 * (len(spans) - 1) if spans else 0,
            "gaps": len(gaps),
            "gap_lines": gap_lines,
            "rate": rate,
            "boards": len(spans),
            "obeyed": obeyed,
        }


if __name__ == "__main__":
    sys.exit(HeartbeatJitter.main())
