#!/usr/bin/env python3
"""Full-swarm acceptance test for Phase 2 (its "Done when").

The two-board smoke test can only show that heartbeats work at all. This is the
real check:

  1. all eight swarm boards are present and identify themselves
  2. every board hears **all seven** others, not just "some"
  3. KILL makes all seven others stop hearing it within about a second
  4. REVIVE brings it back for all seven

Run with every swarm board connected:

    python3 scenarios/swarm_bringup.py
    python3 scenarios/swarm_bringup.py --grade ../Data/bringup/none_run1.csv

It is also the regression test for any firmware change that is not supposed
to alter behaviour. The per-board detection times it prints belong in
Docs/lab_notes.md.
"""

from __future__ import annotations

import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swarmlab.scenario import (KILL_NOTICED_MAX_S, KILL_NOTICED_MIN_S, Check,
                               Scenario)

VICTIM = "1"

SETTLE_S = 12.0
DEAD_S = 6.0
RECOVER_S = 6.0


class SwarmBringup(Scenario):
    name = "bringup"
    protocol = "none"

    def drive(self, log) -> None:
        log.broadcast("RESET")
        log.mark("settle")
        self.wait(SETTLE_S, f"settling with {len(log.nodes)} boards")

        log.mark("kill")
        log.send(VICTIM, "KILL")
        self.wait(DEAD_S, f"node {VICTIM} killed")

        log.mark("revive")
        log.send(VICTIM, "REVIVE")
        self.wait(RECOVER_S, f"node {VICTIM} revived")

    def grade(self, capture) -> list[Check]:
        swarm = self.swarm
        others = [n for n in swarm if n != VICTIM]
        t_kill = capture.command_time(VICTIM, "KILL")
        t_revive = capture.command_time(VICTIM, "REVIVE")
        checks = []

        # --- 1. every swarm board logged something ---------------------------
        seen = capture.boards()
        absent = [n for n in swarm if n not in seen]
        print(f"\n--- boards present ---\n  logged: {seen}")
        if absent:
            print(f"  MISSING: {absent}")
        checks.append(Check("all swarm boards logging", not absent))

        # --- 2. full mesh: each board heard all the others --------------------
        # Counted before the kill, so the victim going silent cannot mask a
        # board that never discovered its neighbours in the first place.
        print(f"\n--- neighbour discovery (expect {len(swarm)-1} each) ---")
        full_mesh = True
        for node in swarm:
            counters = capture.select("COUNTERS", node=node, before=t_kill)
            if not counters:
                print(f"  node {node}: no COUNTERS before the kill")
                full_mesh = False
                continue
            best = max(c.number("neighbors") for c in counters)
            tx = counters[-1].get("en_tx")
            rx = counters[-1].get("en_rx")
            flag = "" if best == len(swarm) - 1 else f"  <-- only {best}"
            print(f"  node {node}: neighbours={best}  en_tx={tx} en_rx={rx}{flag}")
            if best != len(swarm) - 1:
                full_mesh = False
        checks.append(Check(f"every board hears the other {len(swarm)-1}", full_mesh))

        # --- 3. KILL noticed by everyone, in time ----------------------------
        print("\n--- KILL detection ---")
        ok_kill = False
        if t_kill is None:
            print("  no KILL command recorded")
        else:
            # Observers only: the victim also logs losses because KILL makes
            # it deaf as well as silent, which is correct but not graded here.
            noticed = {}
            for e in capture.select("NEIGHBOR_LOST", start=t_kill):
                if e.get("node") == VICTIM and e.node != VICTIM:
                    noticed.setdefault(e.node, e.time - t_kill)

            for node in others:
                if node not in noticed:
                    print(f"  node {node}: never noticed  <-- FAIL")
                else:
                    dt = noticed[node]
                    ok = KILL_NOTICED_MIN_S <= dt <= KILL_NOTICED_MAX_S
                    print(f"  node {node}: {dt:.3f} s  [{'ok' if ok else 'OUT OF RANGE'}]")
            times = list(noticed.values())
            if times:
                print(f"  -> {len(times)}/{len(others)} noticed; "
                      f"mean {statistics.mean(times):.3f} s, "
                      f"min {min(times):.3f}, max {max(times):.3f}")
            ok_kill = (len(noticed) == len(others)
                       and all(KILL_NOTICED_MIN_S <= d <= KILL_NOTICED_MAX_S
                               for d in times))
        checks.append(Check("all others noticed KILL in time", ok_kill))

        # --- 4. REVIVE seen again by everyone --------------------------------
        print("\n--- REVIVE recovery ---")
        ok_revive = False
        if t_revive is None:
            print("  no REVIVE command recorded")
        else:
            back = {}
            for e in capture.select("NEIGHBOR_UP", start=t_revive):
                if e.get("node") == VICTIM and e.node != VICTIM:
                    back.setdefault(e.node, e.time - t_revive)
            for node in others:
                if node not in back:
                    print(f"  node {node}: never saw it return  <-- FAIL")
            if back:
                v = list(back.values())
                print(f"  -> {len(back)}/{len(others)} saw it return; "
                      f"mean {statistics.mean(v):.3f} s, max {max(v):.3f} s")
            ok_revive = len(back) == len(others)
        checks.append(Check("all others saw REVIVE", ok_revive))

        return checks


if __name__ == "__main__":
    sys.exit(SwarmBringup.main())
