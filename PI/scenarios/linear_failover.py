#!/usr/bin/env python3
"""Protocol B acceptance test (Phase 3, step 3.3).

Runs the three cases the step guide asks for, in one capture:

  1. kill the leader          -> node 2 takes over, every live node agrees
  2. revive the old leader    -> it rejoins as a FOLLOWER (non-pre-emptive)
  3. kill nodes 1 and 2       -> node 3 takes over, after a measurably
                                 longer wait than case 1

and grades the Phase 3 "Done when": killing the leader always produces exactly
one new leader, and every live node agrees.

    python3 scenarios/linear_failover.py
    python3 scenarios/linear_failover.py --grade ../Data/linear/B_run1.csv

The failover time it reports is the figure scenario 1 of Phase 7 is built on:
from the KILL to the LAST live node agreeing on the new leader, not the first.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swarmlab.scenario import Check, Scenario

SETTLE_S = 10.0
AFTER_KILL_S = 6.0
AFTER_REVIVE_S = 6.0

# Node 2 is first in line behind node 1, so it waits LINEAR_T_BASE_MS (1000 ms)
# from the leader's last heartbeat, which is already up to 200 ms old when the
# KILL lands. Node 3 sits one place further back and waits one LINEAR_T_STEP_MS
# (500 ms) more. Both bands allow 200 ms for the news to reach every node.
SINGLE_MIN, SINGLE_MAX = 0.7, 1.4
DOUBLE_MIN, DOUBLE_MAX = 1.2, 1.9


class LinearFailover(Scenario):
    name = "linear"
    protocol = "B"

    def drive(self, log) -> None:
        log.broadcast("PROTO B")
        log.broadcast("RESET")
        log.mark("settle")
        self.wait(SETTLE_S, "settling")

        log.mark("kill1")
        log.send("1", "KILL")
        self.wait(AFTER_KILL_S, "node 1, the leader, killed")

        log.mark("revive1")
        log.send("1", "REVIVE")
        self.wait(AFTER_REVIVE_S, "node 1 revived")

        # Back to a clean slate so case 3 starts from node 1 leading again.
        log.mark("reset2")
        log.broadcast("RESET")
        self.wait(SETTLE_S, "settling")

        log.mark("kill12")
        log.send("1", "KILL")
        log.send("2", "KILL")
        self.wait(AFTER_KILL_S + 2, "nodes 1 and 2 killed")

        log.broadcast("RESET")

    def grade(self, capture) -> list[Check] | None:
        checks = []

        t_kill1 = capture.command_time("1", "KILL")
        t_revive = capture.command_time("1", "REVIVE")
        t_reset2 = capture.mark_time("reset2")
        t_kill2 = capture.command_time("2", "KILL", after=t_reset2 or 0)

        if t_kill1 is None:
            print("no KILL recorded for node 1", file=sys.stderr)
            return None

        # --- case 1: kill the leader -----------------------------------------
        found, single = self.grade_failover(
            capture, t_kill1, t_revive or t_kill1 + AFTER_KILL_S,
            dead={"1"}, expect_leader="2",
            lo=SINGLE_MIN, hi=SINGLE_MAX, label="kill leader 1")
        checks += found

        # --- case 2: revive, and confirm it does NOT take the lead back -------
        print("\n--- revive node 1 (must stay a follower) ---")
        if t_revive is None:
            print("  no REVIVE recorded")
            checks.append(Check("revive: node 1 rejoins as follower", False))
        else:
            window_end = t_reset2 or (t_revive + AFTER_REVIVE_S)
            reclaimed = capture.select("TAKEOVER", node="1",
                                       start=t_revive, end=window_end)
            moved = [
                e for e in capture.select("LEADER", start=t_revive, end=window_end)
                if e.first == "1" and e.node != "1"
            ]
            adopted = self.belief_at(capture, "1", window_end)
            print(f"  node 1 now believes the leader is {adopted}")
            if reclaimed:
                print("  node 1 declared itself leader again  <-- PRE-EMPTED")
            if moved:
                print(f"  {len(moved)} other node(s) switched back to node 1"
                      "  <-- PRE-EMPTED")
            ok = not reclaimed and not moved and adopted == "2"
            checks.append(Check("revive: node 1 rejoins as follower", ok))

        # --- case 3: kill 1 and 2, node 3 should take over, later -------------
        if t_kill2 is not None:
            found, double = self.grade_failover(
                capture, t_kill2, t_kill2 + AFTER_KILL_S,
                dead={"1", "2"}, expect_leader="3",
                lo=DOUBLE_MIN, hi=DOUBLE_MAX, label="kill leaders 1 and 2")
            checks += found
            if single is not None and double is not None:
                longer = double > single
                print(f"\n  node 3 took {double - single:+.3f} s longer than "
                      f"node 2 did  [{'ok' if longer else 'NOT LONGER'}]")
                checks.append(Check("second-in-line waits longer", longer))
        else:
            print("\n--- kill leaders 1 and 2 ---\n  not run")
            checks.append(Check("kill leaders 1 and 2 ran", False))

        return checks

    def grade_failover(self, capture, t0, t_end, dead, expect_leader, lo, hi, label):
        """One kill: who took over, did everyone agree, and how long did it
        take. Returns the checks and the failover time (None if nobody agreed).
        """
        print(f"\n--- {label} ---")
        live = [n for n in self.swarm if n not in dead]

        takeovers = {e.node: e.time
                     for e in capture.select("TAKEOVER", start=t0, end=t_end)}
        print(f"  declared themselves leader: {sorted(takeovers) or 'nobody'}")

        # When each live node first logged the expected leader.
        agree = {}
        for node in live:
            for e in capture.select("LEADER", node=node, start=t0, end=t_end):
                if e.first == expect_leader:
                    agree[node] = e.time - t0
                    break

        for node in live:
            if node in agree:
                print(f"  node {node}: agreed on leader {expect_leader} "
                      f"after {agree[node]:.3f} s")
            else:
                believes = self.belief_at(capture, node, t_end)
                print(f"  node {node}: did NOT agree (believes {believes})")

        exactly_one = len(takeovers) == 1 and list(takeovers) == [expect_leader]
        everyone = all(node in agree for node in live)
        # To the LAST node, not the first: the swarm has failed over only once
        # every live node agrees.
        failover = max(agree.values()) if agree else None
        in_band = failover is not None and lo <= failover <= hi

        if failover is not None:
            print(f"  -> failover {failover:.3f} s to the last node "
                  f"(first {min(agree.values()):.3f} s)  "
                  f"[{'ok' if in_band else f'OUT OF BAND {lo}-{hi}'}]")

        return [
            Check(f"{label}: exactly one new leader", exactly_one),
            Check(f"{label}: every live node agrees", everyone),
            Check(f"{label}: failover within {lo}-{hi} s", in_band),
        ], failover

    @staticmethod
    def belief_at(capture, node, when):
        """Who `node` believed the leader was, as of `when`."""
        lines = capture.select("LEADER", node=node, end=when)
        return lines[-1].first if lines else None


if __name__ == "__main__":
    sys.exit(LinearFailover.main())
