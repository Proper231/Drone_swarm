"""The shape every scenario script shares.

A scenario does two things: it drives the boards through a sequence of
commands while everything is recorded, and afterwards it grades the capture.
The two halves only ever meet through the CSV file, which is what makes a run
reproducible evidence: the grade can be recomputed from the file alone, on any
machine, long after the boards have moved on.

A subclass supplies the two halves:

    class LeaderKill(Scenario):
        name = "s1_leader_kill"     # Data/s1_leader_kill/
        protocol = "B"              # B_run<N>.csv

        def drive(self, log):       # commands, with waits between them
            ...
        def grade(self, capture):   # print the detail, return the checks
            return [Check("exactly one new leader", ok), ...]

    if __name__ == "__main__":
        sys.exit(LeaderKill.main())

and gets the rest: checking the boards are there, never overwriting a capture,
the PASS/FAIL summary, and `--grade <csv>` to re-grade an old run with no
boards connected.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import NamedTuple

from .capture import Capture

# D1..D8. ATK (99) and SPARE (98) are not swarm members.
SWARM = [str(n) for n in range(1, 9)]

# How long the others take to notice a board has gone, by the firmware's own
# constants. A node is declared lost NEIGHBOR_WINDOW_MS (1000 ms) after the
# last message heard from it, and presence is scanned every 100 ms. When the
# KILL lands, that last heartbeat is already up to one interval (200 ms) old,
# so detection arrives 800-1000 ms later, plus up to 100 ms of scan
# granularity. Anything outside this band means the timing is not behaving as
# designed.
KILL_NOTICED_MIN_S = 0.7
KILL_NOTICED_MAX_S = 1.3


class Check(NamedTuple):
    """One line of the result: what was required, and whether it held."""
    label: str
    ok: bool


class Scenario:
    name = ""            # folder under Data/
    protocol = "none"    # capture file prefix
    swarm = SWARM        # the boards this scenario needs

    # ---- what a subclass supplies ----

    def drive(self, log) -> None:
        """Send the commands. `log` is the SwarmLogger recording the run."""
        raise NotImplementedError

    def grade(self, capture: Capture) -> list[Check] | None:
        """Print the detail and return the checks. Return None if the capture
        does not contain a run of this scenario at all."""
        raise NotImplementedError

    def missing(self, nodes: list[str]) -> str:
        """Why the boards present cannot run this scenario, or "" if they can."""
        absent = [n for n in self.swarm if n not in nodes]
        if absent:
            return (f"missing swarm boards {absent}, found {nodes}. Connect "
                    "every swarm board and SETID any that report 0.")
        return ""

    @classmethod
    def add_arguments(cls, parser: argparse.ArgumentParser) -> None:
        """Extra command-line options, for the scenarios that have any."""

    def configure(self, args: argparse.Namespace) -> None:
        """Take the values of those options."""

    # ---- what every scenario gets ----

    def wait(self, seconds: float, why: str) -> None:
        print(f"{why} ({seconds:.0f} s)...", file=sys.stderr)
        time.sleep(seconds)

    def fresh_start(self, log, protocol: str) -> None:
        """Every board silent, then every board reset: no old term survives.

        RESET to eight boards is not simultaneous. A board reset first can
        hear a heartbeat from one not yet reset, adopt its old term, and carry
        that into the "fresh" run. Silencing every board before resetting any
        closes that, so every run of every protocol starts from the same state.
        """
        log.broadcast("KILL")
        # Long enough for the last heartbeats already in the air to arrive and
        # be dropped by boards that are now deaf.
        time.sleep(0.5)
        log.broadcast(f"PROTO {protocol}")
        log.broadcast("RESET")  # also clears the KILL

    def find_leader(self, log, tries: int = 5) -> str | None:
        """The leader every live board agrees on, or None.

        Asks the boards, because with Raft the leader is not known in advance.
        Boards that are killed or jammed are left out: they are cut off, so
        what they believe is not the swarm's view. There is a leader when
        every other board names the same one and that board names itself.
        """
        for attempt in range(tries):
            if attempt:
                time.sleep(1.0)
            status = log.poll_status()
            silent = [n for n in log.nodes if n not in status]
            if silent:
                print(f"no STATUS reply from {silent}", file=sys.stderr)
                continue

            live = {node: fields for node, fields in status.items()
                    if fields.get("killed") != "1" and fields.get("jammed") != "1"}
            named = {fields.get("leader", "0") for fields in live.values()}
            if len(named) != 1:
                continue
            leader = named.pop()
            if leader == "0" or leader not in live:
                continue

            print(f"leader is node {leader}, term {live[leader].get('term')}",
                  file=sys.stderr)
            # Answering STATUS costs each board a long serial line and a
            # screen refresh, on all of them at the same moment. A command
            # sent straight after would be timed against boards whose loops
            # had just stalled, so the wait is here where no scenario can
            # forget it.
            time.sleep(2.0)
            return leader

        print("the live boards do not agree on a leader", file=sys.stderr)
        return None

    def run(self) -> int:
        """Drive the boards, record everything, then grade what was recorded."""
        # Imported here so that grading an old capture needs no pyserial.
        from .recorder import SwarmLogger, next_run

        run = next_run(self.name, self.protocol)
        with SwarmLogger(self.name, self.protocol, run) as log:
            problem = self.missing(log.nodes)
            if problem:
                print(problem, file=sys.stderr)
                return 1
            self.drive(log)
            log.mark("end")
            path = log.path

        print(f"\ncapture: {path}")
        return self.grade_file(path)

    def grade_file(self, path: str) -> int:
        """Grade a capture on disk. Exit status 0 only if every check passed."""
        capture = Capture(path)
        if not capture:
            print("CSV is empty", file=sys.stderr)
            return 1

        checks = self.grade(capture)
        if checks is None:
            return 1

        print("\n--- result ---")
        for check in checks:
            print(f"  {'PASS' if check.ok else 'FAIL'}  {check.label}")
        return 0 if all(check.ok for check in checks) else 1

    @classmethod
    def main(cls, argv: list[str] | None = None) -> int:
        parser = argparse.ArgumentParser(
            description=sys.modules[cls.__module__].__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        parser.add_argument(
            "--grade",
            metavar="CSV",
            help="re-grade an existing capture instead of running the boards",
        )
        cls.add_arguments(parser)
        args = parser.parse_args(argv)

        scenario = cls()
        scenario.configure(args)
        if args.grade:
            return scenario.grade_file(args.grade)
        return scenario.run()
