#!/usr/bin/env python3
"""Phase 2 acceptance test, runnable with only two boards.

Checks the three things Phase 2 says must work:

  1. both boards send heartbeats and each hears the other
  2. KILL makes the other stop hearing it within about a second
  3. one CSV is written with rows from every board

    python3 scenarios/smoke_two_boards.py
    python3 scenarios/smoke_two_boards.py --grade ../Data/smoke/none_run1.csv

With the whole swarm connected, swarm_bringup.py is the stronger test.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swarmlab.scenario import (KILL_NOTICED_MAX_S, KILL_NOTICED_MIN_S, Check,
                               Scenario)

SETTLE_S = 10.0   # let heartbeats reach steady state
DEAD_S = 5.0      # how long the killed board stays silent
RECOVER_S = 5.0   # time to come back after REVIVE


class SmokeTwoBoards(Scenario):
    name = "smoke"
    protocol = "none"
    victim = "1"

    @classmethod
    def add_arguments(cls, parser) -> None:
        parser.add_argument("--victim", default="1",
                            help="node ID of the board to kill (default 1)")

    def configure(self, args) -> None:
        self.victim = args.victim

    def missing(self, nodes: list[str]) -> str:
        if len(nodes) < 2:
            return (f"need two provisioned boards, found {nodes}. "
                    "SETID them first.")
        if self.victim not in nodes:
            return f"node {self.victim} is not connected ({nodes})"
        return ""

    def drive(self, log) -> None:
        log.broadcast("RESET")
        log.mark("settle")
        self.wait(SETTLE_S, "settling")

        log.mark("kill")
        log.send(self.victim, "KILL")
        self.wait(DEAD_S, f"node {self.victim} killed")

        log.mark("revive")
        log.send(self.victim, "REVIVE")
        self.wait(RECOVER_S, f"node {self.victim} revived")

    def grade(self, capture) -> list[Check]:
        victim = self.victim
        nodes = capture.boards()
        print(f"\nboards that logged anything : {nodes}")

        # 1. heartbeats flowing both ways
        print("\n--- heartbeats ---")
        ok_hb = True
        for node in nodes:
            counters = capture.select("COUNTERS", node=node)
            if not counters:
                print(f"  node {node}: no COUNTERS rows")
                ok_hb = False
                continue
            last = counters[-1]
            tx, rx = last.get("en_tx"), last.get("en_rx")
            print(f"  node {node}: en_tx={tx} en_rx={rx}")
            if int(tx or 0) == 0:
                print(f"    node {node} never transmitted")
                ok_hb = False
            if int(rx or 0) == 0:
                print(f"    node {node} never received: check the WiFi channel")
                ok_hb = False

        # 2. did each one see the other
        print("\n--- neighbour discovery ---")
        ups = capture.select("NEIGHBOR_UP")
        for e in ups:
            print(f"  node {e.node} saw node {e.get('node')}")
        ok_disc = len(ups) >= len(nodes)

        # 3. KILL detection time, the figure Phase 2 is graded on
        print("\n--- KILL detection ---")
        t_kill = capture.command_time(victim, "KILL")
        losses = [e for e in capture.select("NEIGHBOR_LOST")
                  if e.get("node") == victim]
        ok_kill = False
        if t_kill is None:
            print("  no KILL command was recorded")
        else:
            # Losses logged before the KILL are startup noise, not failures:
            # opening a board's port resets it, and the boards still running
            # notice it drop out. Report them, do not grade them.
            before = [e for e in losses if e.time < t_kill]
            after = [e for e in losses if e.time >= t_kill]
            if before:
                print(
                    f"  ({len(before)} before the kill, from boards resetting as "
                    "their ports opened - ignored)"
                )

            if not after:
                print(f"  nobody logged NEIGHBOR_LOST for node {victim}")
            else:
                times = [e.time - t_kill for e in after]
                for e, dt in zip(after, times):
                    ok = KILL_NOTICED_MIN_S <= dt <= KILL_NOTICED_MAX_S
                    print(
                        f"  node {e.node} noticed after {dt:.3f} s  "
                        f"[{'ok' if ok else 'OUT OF RANGE'}]"
                    )
                ok_kill = all(
                    KILL_NOTICED_MIN_S <= dt <= KILL_NOTICED_MAX_S for dt in times
                )

        return [
            Check("heartbeats both ways", ok_hb),
            Check("neighbour discovery", ok_disc),
            Check("KILL noticed in time", ok_kill),
        ]


if __name__ == "__main__":
    sys.exit(SmokeTwoBoards.main())
