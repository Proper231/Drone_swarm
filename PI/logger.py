#!/usr/bin/env python3
"""Record every board's serial output for a fixed time (step 2.6).

    python3 logger.py --scenario adhoc --protocol B --seconds 60
    python3 logger.py --scenario adhoc --cmd "PROTO B" --cmd RESET

Writes Data/<scenario>/<protocol>_run<N>.csv, N being the next number not
already on disk. For a run that sends commands at particular moments and then
grades itself, write a scenario instead: see scenarios/ and swarmlab/scenario.py.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from swarmlab.recorder import SwarmLogger, next_run


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--scenario", default="adhoc")
    ap.add_argument("--protocol", default="X")
    ap.add_argument(
        "--run",
        type=int,
        default=None,
        help="run number; defaults to the next one not already on disk",
    )
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument(
        "--cmd",
        action="append",
        default=[],
        help="broadcast this command at the start; repeatable",
    )
    args = ap.parse_args()

    run = args.run
    if run is None:
        run = next_run(args.scenario, args.protocol)

    try:
        with SwarmLogger(args.scenario, args.protocol, run) as log:
            for command in args.cmd:
                log.broadcast(command)
                time.sleep(0.2)
            log.mark("capture_start")
            try:
                time.sleep(args.seconds)
            except KeyboardInterrupt:
                print("\ninterrupted", file=sys.stderr)
            log.mark("capture_end")
            path = log.path
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 1

    size = os.path.getsize(path)
    with open(path, encoding="utf-8") as fh:
        rows = sum(1 for _ in fh) - 1
    print(f"wrote {rows} rows ({size} bytes) to {path}", file=sys.stderr)
    return 0 if rows > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
