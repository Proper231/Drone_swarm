#!/usr/bin/env python3
"""Does a short random wait before a vote reply stop the replies being lost?
An experiment, not an acceptance test.

What was seen (lab notes, 2026-11-16 and 17): over 21 Raft elections with the
whole swarm up, 36% of VOTE_REPLY frames never reached the candidate, and 4
elections failed their first round for want of 4 replies. Each failure costs
a failover about a second.

The theory: every voter hears a VOTE_REQ at the same instant, so every voter
answers at the same instant, and replies sent together do not all arrive.

The test: the same boards, turn and turn about, with replies sent at once
(`VOTEJIT 0`) and with each reply held for a fresh random 0 to J ms
(`VOTEJIT J`), for one or more values of J. One trial is one election of the
whole swarm: every board is reset, the first to time out stands, and the
other seven answer it. The grader counts how many of those answers the
candidate received.

Every block runs each setting once, and the order rotates from block to
block, so no setting always runs first or on boards that have been up
longest. With two delays that is a Latin square over three blocks.

    python3 scenarios/vote_reply_delay.py                    0, 20 and 50 ms
    python3 scenarios/vote_reply_delay.py --delay 20         0 and 20 ms
    python3 scenarios/vote_reply_delay.py --blocks 3 --trials 10 --delay 20,50
    python3 scenarios/vote_reply_delay.py --grade ../Data/votejitter/A_run1.csv

The theory is supported if replies are lost without the wait and not with it.
If none are lost without it either, the run says nothing: run it again.
"""

from __future__ import annotations

import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swarmlab.scenario import Check, Scenario

# One trial: time for the first candidate to stand (1 to 2 s after the reset)
# and, if its round fails, for a second one (1 to 2 s more).
TRIAL_S = 5.0

# What "the loss" and "fixed" mean. Without the wait about 64% of replies have
# arrived; a candidate needs 4 of 7.
LOSS_SHOWED_BELOW = 0.90
FIXED_AT_LEAST = 0.95


def parse_delays(text: str) -> list[int]:
    """'20,50' -> [20, 50]: the delays to test, each 1 to 100 ms."""
    import argparse
    try:
        delays = [int(part) for part in text.split(",") if part.strip()]
    except ValueError:
        raise argparse.ArgumentTypeError(f"not a list of numbers: {text!r}")
    if not delays or any(d < 1 or d > 100 for d in delays):
        raise argparse.ArgumentTypeError("each delay must be 1 to 100 ms")
    if len(set(delays)) != len(delays):
        raise argparse.ArgumentTypeError("a delay is listed twice")
    return delays


class VoteReplyDelay(Scenario):
    name = "votejitter"
    protocol = "A"
    delays = [20, 50]
    blocks = 3
    trials = 10

    @classmethod
    def add_arguments(cls, parser) -> None:
        default = ",".join(str(d) for d in cls.delays)
        parser.add_argument("--delay", type=parse_delays, default=cls.delays,
                            help="longest wait before a reply, ms, for each "
                                 "delayed setting, comma-separated (default "
                                 f"{default}; each at most 100). Replies at "
                                 "once are always run as well")
        parser.add_argument("--blocks", type=int, default=cls.blocks,
                            help="blocks to run, each one block per setting "
                                 f"(default {cls.blocks})")
        parser.add_argument("--trials", type=int, default=cls.trials,
                            help="elections in each block "
                                 f"(default {cls.trials})")

    def configure(self, args) -> None:
        self.delays = args.delay
        self.blocks = args.blocks
        self.trials = args.trials

    # ------------------------------------------------------------- driving

    def drive(self, log) -> None:
        settings = [0] + self.delays
        for block in range(1, self.blocks + 1):
            # The order rotates by one each block, so every setting takes its
            # turn first and none always runs on boards that have been up
            # longer. With one delay this is the old swap, 0 first then not.
            k = (block - 1) % len(settings)
            order = settings[k:] + settings[:k]
            for delay in order:
                print(f"block {block}, reply delay {delay} ms: "
                      f"{self.trials} elections", file=sys.stderr)
                log.broadcast(f"VOTEJIT {delay}")
                # The boards' own word for what they are running goes into
                # the capture, so the grade does not rest on the command.
                log.mark(f"vblock,delay={delay},block={block}")
                log.poll_status()
                for n in range(1, self.trials + 1):
                    log.mark(f"vtrial,delay={delay},block={block},n={n}")
                    self.fresh_start(log, "A")
                    time.sleep(TRIAL_S)
        # Leave the boards as a fresh boot would, on the firmware's own
        # default, not on whatever the last block set.
        log.broadcast("REBOOT")

    # ------------------------------------------------------------- grading

    def grade(self, capture) -> list[Check] | None:
        marks = capture.select("MARK")
        trial_marks = [e for e in marks if e.first == "vtrial"]
        if not trial_marks:
            print("no trials in this capture: not a vote reply delay run",
                  file=sys.stderr)
            return None

        trials = []
        for mark in trial_marks:
            later = [e.time for e in marks if e.time > mark.time]
            end = later[0] if later else capture.events[-1].time
            trials.append(self.measure(capture, mark, end))

        obeyed = self.boards_obeyed(capture, marks)

        print("\n--- each block ---")
        print("  block  delay  elections  replies arrived  first rounds failed"
              "  settled")
        keys = []
        for t in trials:  # in the order they were run
            if (t["block"], t["delay"]) not in keys:
                keys.append((t["block"], t["delay"]))
        for block, delay in keys:
            group = [t for t in trials
                     if t["block"] == block and t["delay"] == delay]
            self.print_row(f"  {block:>5}  {delay:>3} ms", group)

        plain = [t for t in trials if t["delay"] == 0]
        delays = sorted({t["delay"] for t in trials if t["delay"] != 0})
        by_delay = {d: [t for t in trials if t["delay"] == d] for d in delays}

        # With one delay the labels are the ones the first capture was graded
        # with, so re-grading it prints exactly what it printed then.
        def name(d: int) -> str:
            return ("replies delayed" if len(delays) == 1
                    else f"replies delayed 0-{d} ms")

        print("\n--- together ---")
        groups = [("replies at once", plain)]
        groups += [(name(d), by_delay[d]) for d in delays]
        if not delays:
            groups.append(("replies delayed", []))
        for label, group in groups:
            if not group:
                print(f"  {label}: no trials")
                continue
            self.print_summary(label, group)

        counted = [t for t in trials if t["candidate"] is not None]
        complete = (len(counted) == len(trials)
                    and all(t["boards"] == len(self.swarm) for t in trials))
        rate_plain = self.arrival(plain)
        reproduced = rate_plain is not None and rate_plain < LOSS_SHOWED_BELOW

        outcome = {}
        for d in delays or [None]:
            group = by_delay.get(d, [])
            rate = self.arrival(group)
            outcome[d] = {
                "rate": rate,
                "arrived": rate is not None and rate >= FIXED_AT_LEAST,
                "no_failed": bool(group) and not any(t["failed"] for t in group),
                "settled": bool(group) and all(t["settled"] is not None
                                               for t in group),
            }

        print("\n--- what it shows ---")
        if not (complete and obeyed):
            print("  nothing: the trials did not run as intended. If no board "
                  "reported votejit=, the firmware on them has no VOTEJIT "
                  "command.")
        elif not reproduced:
            print("  nothing either way: the replies sent at once nearly all "
                  "arrived this time. Run it again.")
        else:
            for d in delays:
                head = "" if len(delays) == 1 else f"0 to {d} ms: "
                print(f"  {head}" + self.verdict(rate_plain, outcome[d]))
            if len(delays) > 1:
                self.compare(trials, delays)

        checks = [
            Check(f"every trial held an election on all {len(self.swarm)} "
                  "boards", complete),
            Check("every board ran the delay it was told to", obeyed),
            Check("replies at once: the loss showed up (under "
                  f"{100 * LOSS_SHOWED_BELOW:.0f}% arrived)", reproduced),
        ]
        for d in delays or [None]:
            label = name(d) if d is not None else "replies delayed"
            o = outcome[d]
            checks += [
                Check(f"{label}: at least {100 * FIXED_AT_LEAST:.0f}% arrived",
                      o["arrived"]),
                Check(f"{label}: no first round failed", o["no_failed"]),
                Check(f"{label}: every trial ended with a leader all "
                      f"{len(self.swarm)} agree on", o["settled"]),
            ]
        return checks

    @staticmethod
    def verdict(rate_plain, o) -> str:
        """One delayed setting against replies at once, in a sentence."""
        if o["arrived"] and o["no_failed"]:
            return ("the theory holds up: replies were lost when sent at once "
                    "and not when delayed.")
        if (o["no_failed"] and o["rate"] is not None
                and (1 - o["rate"]) <= (1 - rate_plain) / 2):
            # Added after the first run on boards, whose result was this case.
            # The checks are unchanged; only this summary sentence is.
            return ("the delay does most of it, not all: replies lost fell "
                    f"from {100 * (1 - rate_plain):.0f}% to "
                    f"{100 * (1 - o['rate']):.0f}% and no delayed first round "
                    "failed, but some are still lost. Try a wider delay.")
        return ("the theory does not hold up: replies were lost with the "
                "delay too, so something else is losing them.")

    def compare(self, trials, delays) -> None:
        """Between delays: is the wider one better, block for block? Printed,
        not graded. The blocks rotate, so a setting that beats another in
        every block beats it whichever ran first."""
        def block_rates(d):
            rates = {}
            for t in trials:
                if t["delay"] == d:
                    sent, got = rates.get(t["block"], (0, 0))
                    rates[t["block"]] = (sent + t["sent"], got + t["arrived"])
            return [got / sent for sent, got in rates.values() if sent]

        print("  between the delays, replies arrived per block:")
        for d in delays:
            rates = block_rates(d)
            print(f"    0 to {d:>3} ms: "
                  + ", ".join(f"{100 * r:.1f}%" for r in rates))
        for lo, hi in zip(delays, delays[1:]):
            a, b = block_rates(lo), block_rates(hi)
            if a and b and min(b) > max(a):
                print(f"    0 to {hi} ms beat 0 to {lo} ms in every block: "
                      "the wider wait helps.")
            elif a and b and min(a) > max(b):
                print(f"    0 to {lo} ms beat 0 to {hi} ms in every block: "
                      "the wider wait is worse.")
            else:
                print(f"    0 to {lo} and 0 to {hi} ms overlap block for "
                      "block: no difference this run can show.")

    def measure(self, capture, mark, end) -> dict:
        """One trial: the first election after the reset, and how it went."""
        result = {
            "delay": mark.number("delay"), "block": mark.number("block"),
            "candidate": None, "sent": 0, "arrived": 0,
            "failed": False, "split": False, "elections": 0, "settled": None,
            "won_ms": None, "boards": 0,
        }
        t_reset = None
        for e in capture.select("CMD", source="pi", start=mark.time, before=end):
            if e.fields == "RESET":
                t_reset = e.time
                break
        if t_reset is None:
            return result

        window = [e for e in capture.events if t_reset <= e.time < end]
        result["boards"] = len({e.node for e in window
                                if e.source == "board" and e.node in self.swarm})
        starts = [e for e in window if e.name == "ELECTION_START"]
        result["elections"] = len(starts)
        if not starts:
            return result

        first = starts[0]
        candidate, term = first.node, first.number("term")
        result["candidate"] = candidate

        # Who heard the request, and so owed a reply; and whose reply the
        # candidate then received. Both from the RX lines the boards log.
        owed = {e.node for e in window
                if e.name == "RX" and e.get("type") == "VOTE_REQ"
                and e.get("from") == candidate and e.number("term") == term}
        got = {e.get("from") for e in window
               if e.node == candidate and e.name == "RX"
               and e.get("type") == "VOTE_REPLY" and e.number("term") == term}
        result["sent"] = len(owed)
        result["arrived"] = len(got & owed)

        won = [e for e in window if e.name == "BECAME_LEADER"
               and e.node == candidate and e.number("term") == term]
        # A split vote: a second board stood for the same term before it
        # heard the first one ask, so each denied the other. Ordinary Raft,
        # and nothing to do with replies being lost, so it is counted apart
        # and does not count as a failed round.
        result["split"] = any(e.name == "ELECTION_START" and e.node != candidate
                              and e.number("term") == term for e in starts)
        result["failed"] = not won and not result["split"]
        if won:
            result["won_ms"] = (won[0].time - first.time) * 1000.0

        # Settled: every board's last LEADER line names one leader and term.
        beliefs = {}
        times = []
        for node in self.swarm:
            lines = [e for e in window if e.name == "LEADER" and e.node == node]
            if lines:
                beliefs[node] = (lines[-1].first, lines[-1].number("term"))
                times.append(lines[-1].time)
        if len(beliefs) == len(self.swarm) and len(set(beliefs.values())) == 1:
            result["settled"] = max(times) - t_reset
        return result

    def boards_obeyed(self, capture, marks) -> bool:
        """Every board's STATUS in each block reports the delay it was sent."""
        blocks = [e for e in marks if e.first == "vblock"]
        if not blocks:
            return False
        for mark in blocks:
            later = [e.time for e in marks
                     if e.time > mark.time and e.first == "vblock"]
            end = later[0] if later else capture.events[-1].time
            reported = {}
            for e in capture.select("RAW", start=mark.time, before=end):
                if e.fields.startswith("STATUS ") and e.node in self.swarm:
                    fields = dict(token.split("=", 1)
                                  for token in e.fields.split()[1:]
                                  if "=" in token)
                    reported.setdefault(e.node, fields.get("votejit"))
            if (len(reported) != len(self.swarm)
                    or any(v != str(mark.number("delay"))
                           for v in reported.values())):
                return False
        return True

    @staticmethod
    def arrival(group):
        sent = sum(t["sent"] for t in group)
        return sum(t["arrived"] for t in group) / sent if sent else None

    def print_row(self, head, group) -> None:
        sent = sum(t["sent"] for t in group)
        arrived = sum(t["arrived"] for t in group)
        failed = sum(1 for t in group if t["failed"])
        settled = sum(1 for t in group if t["settled"] is not None)
        share = f"{100 * arrived / sent:5.1f}%" if sent else "   - "
        print(f"{head}  {len(group):>9}  {arrived:>3} of {sent:<3} {share}  "
              f"{failed:>19}  {settled:>3} of {len(group)}")

    def print_summary(self, label, group) -> None:
        sent = sum(t["sent"] for t in group)
        arrived = sum(t["arrived"] for t in group)
        failed = sum(1 for t in group if t["failed"])
        won = [t["won_ms"] for t in group if t["won_ms"] is not None]
        settled = [t["settled"] for t in group if t["settled"] is not None]
        fewest = min((t["arrived"] for t in group if t["sent"]), default=0)
        splits = sum(1 for t in group if t["split"])
        print(f"  {label}: {len(group)} elections, {arrived} of {sent} replies "
              f"arrived ({100 * arrived / sent if sent else 0:.1f}%), fewest "
              f"in one election {fewest}; first rounds failed {failed}"
              + (f"; two candidates in one term {splits}" if splits else ""))
        if won:
            print(f"      a won first round took {statistics.mean(won):.0f} ms "
                  f"from standing to leading (longest {max(won):.0f} ms)")
        if settled:
            print(f"      all 8 agreed {statistics.mean(settled):.3f} s after "
                  f"the reset on average, at worst {max(settled):.3f} s")


if __name__ == "__main__":
    sys.exit(VoteReplyDelay.main())
