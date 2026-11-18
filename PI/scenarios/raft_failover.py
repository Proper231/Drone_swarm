#!/usr/bin/env python3
"""Protocol A acceptance test (Phase 4, step 4.4).

Raft's leader is whoever won the election, so nothing here assumes a node ID
or a term number: the script asks the boards who leads before each case, and
the grader reads who was killed or jammed back out of the capture.

One capture, in this order:

  1. startup            -> all 8 boards elect one leader
  2. kill the leader    -> exactly one new leader, on a majority, all 7 agree
  3. revive it          -> it rejoins as a follower, no election
  4. jam the leader     -> as case 2, for the 7 that can still talk
  5. unjam it           -> it sees the higher term, steps down and follows
  6. kill 4, leader     -> the 4 left can never reach 5 votes: elections,
     among them            rising terms, and no leader
  7. revive those 4     -> all 8 agree again, on a higher term

    python3 scenarios/raft_failover.py
    python3 scenarios/raft_failover.py --grade ../Data/raft/A_run1.csv

Case 4 exists because REVIVE resets the protocol, so a revived leader is
already a follower. Only a leader that was cut off while keeping its role and
its term shows what happens when an old leader really comes back.

The failover time is measured as for Protocol B: from the command to the LAST
live node logging the new leader. The elections each failover took, and the
votes each candidate got, are printed for the report and not graded.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swarmlab.scenario import Check, Scenario

SETTLE_S = 10.0
AFTER_KILL_S = 8.0
AFTER_REVIVE_S = 6.0
AFTER_KILL4_S = 12.0
AFTER_REVIVE4_S = 8.0

# RAFT_MAJORITY in config.h: of the full swarm, not of the boards alive.
MAJORITY = 5

# A follower re-arms on each leader heartbeat with a fresh draw from 1000 to
# 2000 ms, and that heartbeat is up to 200 ms old when the leader is cut off,
# so the first candidate stands 0.8 to 2.0 s later. The vote exchange adds
# tens of milliseconds. A second round, after a lost reply or a split vote,
# adds up to 2 s more.
FAILOVER_MIN, FAILOVER_MAX = 0.7, 4.5

# With 4 boards left no election can succeed, so they keep coming.
KILL4_MIN_ELECTIONS = 3
KILL4_MIN_TERM_RISE = 3

# Every line RaftProtocol writes that carries a term.
RAFT_EVENTS = ("ELECTION_START", "ELECTION_TIMEOUT", "VOTE_GRANTED",
               "BECAME_LEADER", "STEP_DOWN", "LEADER")


class RaftFailover(Scenario):
    name = "raft"
    protocol = "A"

    # ------------------------------------------------------------- driving

    def drive(self, log) -> None:
        self.cases(log)
        log.broadcast("RESET")

    def cases(self, log) -> None:
        log.mark("settle")
        self.fresh_start(log, "A")
        self.wait(SETTLE_S, "settling, first election")
        leader = self.leader_or_stop(log)
        if leader is None:
            return

        log.mark("kill_leader")
        log.send(leader, "KILL")
        self.wait(AFTER_KILL_S, f"node {leader}, the leader, killed")

        log.mark("revive")
        log.send(leader, "REVIVE")
        self.wait(AFTER_REVIVE_S, f"node {leader} revived")
        leader = self.leader_or_stop(log)
        if leader is None:
            return

        log.mark("jam_leader")
        log.send(leader, "JAM ON")
        self.wait(AFTER_KILL_S, f"node {leader}, the leader, jammed")

        log.mark("unjam")
        log.send(leader, "JAM OFF")
        self.wait(AFTER_REVIVE_S, f"node {leader} unjammed")
        leader = self.leader_or_stop(log)
        if leader is None:
            return

        # The leader has to be one of the four. A Raft leader that loses its
        # majority does not notice, so if it survived, the other three would
        # go on following it and no election would ever start.
        victims = [leader] + [n for n in self.swarm if n != leader][:3]
        log.mark("kill4")
        for node in victims:
            log.send(node, "KILL")
        self.wait(AFTER_KILL4_S, f"nodes {victims} killed, 4 left")

        log.mark("revive4")
        for node in victims:
            log.send(node, "REVIVE")
        self.wait(AFTER_REVIVE4_S, f"nodes {victims} revived")

    def leader_or_stop(self, log) -> str | None:
        leader = self.find_leader(log)
        if leader is None:
            print("no agreed leader: stopping here. The cases not run will "
                  "fail the grade.", file=sys.stderr)
        return leader

    # ------------------------------------------------------------- grading

    def grade(self, capture) -> list[Check] | None:
        if not any(e.fields == "PROTO A"
                   for e in capture.select("CMD", source="pi")):
            print("no PROTO A command in this capture: not a Raft run",
                  file=sys.stderr)
            return None

        checks = self.grade_startup(capture)
        checks += self.grade_failover(capture, "kill_leader", "KILL",
                                      "kill leader")
        checks += self.grade_revive(capture)
        checks += self.grade_failover(capture, "jam_leader", "JAM ON",
                                      "jam leader")
        checks += self.grade_unjam(capture)
        checks += self.grade_kill4(capture)
        checks += self.grade_revive4(capture)

        conflicts = capture.select("LEADER_CONFLICT")
        print("\n--- whole run ---")
        for e in conflicts:
            print(f"  node {e.node} saw a second leader, node {e.first}, in "
                  f"term {e.number('term')}  <-- LEADER_CONFLICT")
        if not conflicts:
            print("  no LEADER_CONFLICT")
        checks.append(Check("whole run: no LEADER_CONFLICT", not conflicts))

        self.print_health(capture)
        return checks

    def grade_startup(self, capture) -> list[Check]:
        print("\n--- startup election ---")
        window = self.window(capture, "settle")
        if window is None:
            return self.not_run("startup")
        start, end = window

        beliefs = {n: self.belief(capture, n, start, end) for n in self.swarm}
        self.print_beliefs(beliefs)
        agreed = self.common(beliefs.values())

        self.print_elections(capture, start, end)
        t_reset = self.first_command(capture, "RESET", start, end)
        if agreed is not None and t_reset is not None:
            times = self.agreement_times(capture, self.swarm, agreed, t_reset, end)
            if len(times) == len(self.swarm):
                print(f"  -> all {len(self.swarm)} agreed on node {agreed[0]}, "
                      f"term {agreed[1]}, {max(times.values()):.3f} s after "
                      "the RESET")

        return [Check(f"startup: all {len(self.swarm)} boards agree on one "
                      "leader and term", agreed is not None)]

    def grade_failover(self, capture, mark, command, label) -> list[Check]:
        """The leader is cut off: who took over, on how many votes, did every
        board still talking agree, and how long did it take."""
        print(f"\n--- {label} ---")
        window = self.window(capture, mark)
        if window is None:
            return self.not_run(label)
        start, end = window

        victims = self.targets(capture, command, start, end)
        if len(victims) != 1:
            print(f"  expected one {command}, found {victims}")
            return self.not_run(label)
        victim = victims[0]
        t0 = capture.command_time(victim, command, after=start)
        live = [n for n in self.swarm if n != victim]
        term_before = self.highest_term(capture, before=start)
        print(f"  node {victim} got {command}; highest term before it was "
              f"{term_before}")

        self.print_elections(capture, start, end)
        won = capture.select("BECAME_LEADER", start=start, before=end)
        labels = [
            f"{label}: exactly one new leader",
            f"{label}: its term is higher than before",
            f"{label}: won on a majority ({MAJORITY}+ votes, "
            f"{MAJORITY - 1}+ boards granted)",
            f"{label}: all {len(live)} live boards agree",
            f"{label}: failover within {FAILOVER_MIN}-{FAILOVER_MAX} s",
        ]
        if not won:
            print("  nobody became leader")
            return [Check(text, False) for text in labels]

        winner = won[-1].node
        term = won[-1].number("term")
        votes = won[-1].number("votes")
        granted = sorted({
            e.node
            for e in capture.select("VOTE_GRANTED", start=start, before=end)
            if e.first == winner and e.number("term") == term
            and e.node != winner
        })
        print(f"  node {winner} became leader in term {term} with {votes} "
              f"votes; VOTE_GRANTED logged by {granted}")

        times = self.agreement_times(capture, live, (winner, term), t0, end)
        beliefs = {n: self.belief(capture, n, start, end) for n in live}
        for node in live:
            if node in times:
                print(f"  node {node}: agreed on leader {winner} after "
                      f"{times[node]:.3f} s")
            else:
                print(f"  node {node}: did NOT agree "
                      f"(believes {self.describe(beliefs[node])})")
        everyone = all(beliefs[n] == (winner, term) for n in live)

        # To the LAST node, not the first: the swarm has failed over only once
        # every board still talking agrees.
        failover = max(times.values()) if len(times) == len(live) else None
        in_band = (failover is not None
                   and FAILOVER_MIN <= failover <= FAILOVER_MAX)
        if failover is not None:
            flag = "ok" if in_band else f"OUT OF BAND {FAILOVER_MIN}-{FAILOVER_MAX}"
            print(f"  -> failover {failover:.3f} s to the last node "
                  f"(first {min(times.values()):.3f} s)  [{flag}]")

        results = [
            len(won) == 1,
            term > term_before,
            votes >= MAJORITY and len(granted) >= MAJORITY - 1,
            everyone,
            in_band,
        ]
        return [Check(text, ok) for text, ok in zip(labels, results)]

    def grade_revive(self, capture) -> list[Check]:
        print("\n--- revive the old leader (must rejoin as a follower) ---")
        window = self.window(capture, "revive")
        if window is None:
            return self.not_run("revive")
        start, end = window

        victims = self.targets(capture, "REVIVE", start, end)
        if len(victims) != 1:
            print(f"  expected one REVIVE, found {victims}")
            return self.not_run("revive")
        victim = victims[0]
        current = self.current_leader(capture, victim, start)
        adopted = self.belief(capture, victim, start, end)
        print(f"  the others follow {self.describe(current)}; node {victim} "
              f"now follows {self.describe(adopted)}")

        became = capture.select("BECAME_LEADER", start=start, before=end)
        elections = capture.select("ELECTION_START", start=start, before=end)
        for e in elections:
            print(f"  node {e.node} started an election, term "
                  f"{e.number('term')}  <-- should not happen")

        follows = (current is not None and adopted is not None
                   and adopted[0] == current[0])
        return [
            Check("revive: old leader follows the current one", follows),
            Check("revive: no election", not became and not elections),
        ]

    def grade_unjam(self, capture) -> list[Check]:
        print("\n--- unjam the old leader (must step down) ---")
        window = self.window(capture, "unjam")
        if window is None:
            return self.not_run("unjam")
        start, end = window

        victims = self.targets(capture, "JAM OFF", start, end)
        if len(victims) != 1:
            print(f"  expected one JAM OFF, found {victims}")
            return self.not_run("unjam")
        victim = victims[0]
        current = self.current_leader(capture, victim, start)

        steps = [
            e for e in capture.select("STEP_DOWN", node=victim,
                                      start=start, before=end)
            if e.get("was") == "LEADER" and e.get("why") == "higher_term"
        ]
        adopted = None
        if steps:
            t_unjam = capture.command_time(victim, "JAM OFF", after=start)
            print(f"  node {victim} stepped down {steps[0].time - t_unjam:.3f} s"
                  f" after JAM OFF, into term {steps[0].number('term')}")
            adopted = self.belief(capture, victim, steps[0].time, end)
        else:
            print(f"  node {victim} never logged STEP_DOWN was=LEADER,"
                  "why=higher_term")
        print(f"  the others follow {self.describe(current)}; node {victim} "
              f"now follows {self.describe(adopted)}")

        became = capture.select("BECAME_LEADER", start=start, before=end)
        for e in became:
            print(f"  node {e.node} became leader, term {e.number('term')}"
                  "  <-- the returning leader disturbed the swarm")

        follows = (current is not None and adopted is not None
                   and adopted[0] == current[0])
        return [
            Check("unjam: old leader steps down and follows the current one",
                  follows),
            Check("unjam: no new leader", not became),
        ]

    def grade_kill4(self, capture) -> list[Check]:
        print("\n--- kill 4, the leader among them (no majority left) ---")
        window = self.window(capture, "kill4")
        if window is None:
            return self.not_run("kill 4")
        start, end = window

        victims = self.targets(capture, "KILL", start, end)
        if len(victims) != 4:
            print(f"  expected four KILLs, found {victims}")
            return self.not_run("kill 4")
        live = [n for n in self.swarm if n not in victims]
        current = self.current_leader(capture, None, start)
        among = current is not None and current[0] in victims
        print(f"  killed {victims}, leaving {live}; the leader was "
              f"{self.describe(current)}"
              f"{'' if among else '  <-- NOT among the killed'}")

        became = capture.select("BECAME_LEADER", start=start, before=end)
        for e in became:
            print(f"  node {e.node} became leader, term {e.number('term')}, "
                  f"votes {e.number('votes')}  <-- without a majority alive")
        leader_lines = [
            e for e in capture.select("LEADER", start=start, before=end)
            if e.node in live
        ]
        for e in leader_lines:
            print(f"  node {e.node} logged LEADER,{e.fields}  <-- should not")

        elections = self.print_elections(capture, start, end)
        term_before = self.highest_term(capture, before=start)
        term_after = self.highest_term(capture, start=start, before=end)
        rise = term_after - term_before
        print(f"  highest term went {term_before} -> {term_after} (+{rise})")

        # Matched to the elections started in the window by term, not by
        # time: the last election's timeout can land just after the revive
        # mark (raft/A_run8.csv), and it still has to be checked.
        terms = {(e.node, e.number("term"))
                 for e in capture.select("ELECTION_START", start=start,
                                         before=end)}
        timeouts = [e for e in capture.select("ELECTION_TIMEOUT", start=start)
                    if (e.node, e.number("term")) in terms]
        most = max((e.number("votes") for e in timeouts), default=0)
        print(f"  {len(timeouts)} election(s) timed out, the best with "
              f"{most} vote(s)")

        return [
            Check("kill 4: no leader elected", not became),
            Check(f"kill 4: no LEADER line from the {len(live)} live boards",
                  not leader_lines),
            Check(f"kill 4: at least {KILL4_MIN_ELECTIONS} elections started",
                  elections >= KILL4_MIN_ELECTIONS),
            Check(f"kill 4: highest term rose by at least {KILL4_MIN_TERM_RISE}",
                  rise >= KILL4_MIN_TERM_RISE),
            Check(f"kill 4: no election got more than {MAJORITY - 1} votes",
                  most <= MAJORITY - 1),
        ]

    def grade_revive4(self, capture) -> list[Check]:
        print("\n--- revive the 4 (the swarm must recover) ---")
        window = self.window(capture, "revive4")
        if window is None:
            return self.not_run("revive 4")
        start, end = window
        # The run ends with a RESET, which is not part of the recovery.
        t_reset = self.first_command(capture, "RESET", start, end)
        if t_reset is not None:
            end = t_reset

        term_before = self.highest_term(capture, before=start)
        beliefs = {n: self.belief(capture, n, start, end) for n in self.swarm}
        self.print_beliefs(beliefs)
        agreed = self.common(beliefs.values())
        self.print_elections(capture, start, end)

        t0 = self.first_command(capture, "REVIVE", start, end)
        if agreed is not None and t0 is not None:
            times = self.agreement_times(capture, self.swarm, agreed, t0, end)
            if len(times) == len(self.swarm):
                print(f"  -> all {len(self.swarm)} agreed on node {agreed[0]}, "
                      f"term {agreed[1]}, {max(times.values()):.3f} s after "
                      "the first REVIVE")
        print(f"  highest term before the revive was {term_before}")

        return [
            Check(f"revive 4: all {len(self.swarm)} boards end on the same "
                  "leader", agreed is not None),
            Check("revive 4: at a term above every earlier one",
                  agreed is not None and agreed[1] > term_before),
        ]

    # ---------------------------------------------- reading the capture

    @staticmethod
    def not_run(label) -> list[Check]:
        print("  not run")
        return [Check(f"{label}: ran", False)]

    @staticmethod
    def window(capture, label):
        """(start, end) of one case: from its mark to the next mark."""
        start = capture.mark_time(label)
        if start is None:
            return None
        later = [e.time for e in capture.select("MARK") if e.time > start]
        return start, (min(later) if later else float("inf"))

    @staticmethod
    def targets(capture, command, start, end) -> list[str]:
        """Which boards the Pi sent `command` to in the window."""
        return sorted({
            e.node
            for e in capture.select("CMD", source="pi", start=start, before=end)
            if e.fields == command
        }, key=lambda node: node.zfill(3))

    @staticmethod
    def first_command(capture, command, start, end):
        for e in capture.select("CMD", source="pi", start=start, before=end):
            if e.fields == command:
                return e.time
        return None

    @staticmethod
    def belief(capture, node, start, end):
        """(leader, term) from the last LEADER line `node` logged in the
        window, or None if it logged none."""
        lines = capture.select("LEADER", node=node, start=start, before=end)
        return (lines[-1].first, lines[-1].number("term")) if lines else None

    @staticmethod
    def common(beliefs):
        """The belief every board shares, or None if they differ or any board
        has none."""
        found = set(beliefs)
        if len(found) == 1 and None not in found:
            return found.pop()
        return None

    def current_leader(self, capture, apart_from, when):
        """Who the swarm followed as of `when`, leaving one board out."""
        return self.common(
            self.belief(capture, n, None, when)
            for n in self.swarm if n != apart_from
        )

    @staticmethod
    def agreement_times(capture, nodes, leader, t0, end) -> dict[str, float]:
        """How long after t0 each node first logged this (leader, term)."""
        times = {}
        for node in nodes:
            for e in capture.select("LEADER", node=node, start=t0, before=end):
                if (e.first, e.number("term")) == leader:
                    times[node] = e.time - t0
                    break
        return times

    @staticmethod
    def highest_term(capture, start=None, before=None) -> int:
        terms = [
            e.number("term")
            for e in capture.select(source="board", start=start, before=before)
            if e.name in RAFT_EVENTS
        ]
        return max(terms, default=0)

    @staticmethod
    def describe(belief) -> str:
        if belief is None:
            return "nobody"
        return f"node {belief[0]} (term {belief[1]})"

    def print_beliefs(self, beliefs) -> None:
        for node, belief in beliefs.items():
            print(f"  node {node}: ends on {self.describe(belief)}")

    def print_elections(self, capture, start, end) -> int:
        """Lists the elections started in the window and how each ended.
        Results for the report, not graded here. Returns how many."""
        elections = capture.select("ELECTION_START", start=start, before=end)
        for e in elections:
            print(f"  election: node {e.node}, term {e.number('term')}, after "
                  f"waiting {e.number('waited_ms')} ms: "
                  f"{self.election_outcome(capture, e)}")
        print(f"  {len(elections)} election(s) in this window")
        return len(elections)

    @staticmethod
    def election_outcome(capture, started) -> str:
        """What the candidate logged next about its own election."""
        # By position, not by time: a candidate whose election ran out logs
        # ELECTION_TIMEOUT for the old term and ELECTION_START for the new one
        # in the same millisecond.
        at = next(i for i, e in enumerate(capture.events) if e is started)
        for e in capture.events[at + 1:]:
            if e.node != started.node or e.source != "board":
                continue
            if e.name == "BECAME_LEADER":
                return f"won with {e.number('votes')} votes"
            if e.name == "ELECTION_TIMEOUT":
                return f"timed out with {e.number('votes')} vote(s)"
            if e.name == "STEP_DOWN":
                return f"stood down ({e.get('why')})"
            if e.name in ("ELECTION_START", "KILL", "REVIVE"):
                break
        return "no outcome logged"

    def print_health(self, capture) -> None:
        """What to watch on hardware (design section 6). Not graded."""
        print("\n--- health, not graded ---")
        counters = capture.select("COUNTERS")
        print(f"  receive queue drops (d_queue), worst board: "
              f"{max((c.number('d_queue') for c in counters), default=0)}")
        print(f"  send failures (en_txfail), worst board: "
              f"{max((c.number('en_txfail') for c in counters), default=0)}")

        # A healthy swarm must not hold elections. One here means a follower
        # went a whole timeout without hearing a leader that was up, which is
        # the "false leader change" Phase 7 counts. Looked for in every window
        # where all 8 boards are up: from the first leader onwards where the
        # window starts without one, from the start where it starts with one.
        false_starts = []
        for label, starts_led in (("settle", False), ("revive", True),
                                  ("unjam", True), ("revive4", False)):
            window = self.window(capture, label)
            if window is None:
                continue
            start, end = window
            t_reset = (self.first_command(capture, "RESET", start, end)
                       if label == "revive4" else None)
            if t_reset is not None:
                end = t_reset
            if not starts_led:
                first = capture.select("BECAME_LEADER", start=start, before=end)
                if not first:
                    continue
                start = first[0].time
            for e in capture.select("ELECTION_START", start=start, before=end):
                if e.time > start:
                    false_starts.append((label, e))
        for label, e in false_starts:
            print(f"  {label}: node {e.node} stood against a leader that was "
                  f"up, term {e.number('term')}, after waiting "
                  f"{e.number('waited_ms')} ms  <-- missed its heartbeats")
        print(f"  elections against a healthy leader: {len(false_starts)}")


if __name__ == "__main__":
    sys.exit(RaftFailover.main())
