"""Every board's serial output into one CSV per run (step 2.6).

One thread per board. Every line is stamped with the Pi's own clock the moment
it is read, and that stamp is what the analysis uses. Commands sent from here
go into the same file on the same clock, so "time from KILL to the last node
agreeing on a new leader" is a subtraction between two rows of one file.

    run = next_run("s1_leader_kill", "A")   # never overwrites a capture
    with SwarmLogger("s1_leader_kill", "A", run) as log:
        log.broadcast("RESET")
        log.broadcast("PROTO B")
        time.sleep(10)
        log.send(1, "KILL")
        time.sleep(20)
"""

from __future__ import annotations

import csv
import os
import queue
import sys
import threading
import time

from .board import BoardLink, open_together
from .event import CSV_HEADER, Event
from .ports import find_ports, parse_status

# <repo>/Data, wherever the repo happens to live.
DATA_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Data")
)


def capture_path(scenario: str, protocol: str, run: int, data_dir: str = DATA_DIR) -> str:
    return os.path.join(data_dir, scenario, f"{protocol}_run{run}.csv")


def next_run(scenario: str, protocol: str, data_dir: str = DATA_DIR) -> int:
    """First run number with no capture on disk yet.

    A capture is evidence. Re-running a scenario must add a file, never replace
    one, so the scripts ask for the next free number instead of assuming 1.
    """
    n = 1
    while os.path.exists(capture_path(scenario, protocol, n, data_dir)):
        n += 1
    return n


class SwarmLogger:
    def __init__(
        self,
        scenario: str,
        protocol: str,
        run: int,
        data_dir: str = DATA_DIR,
        ports: list[str] | None = None,
    ) -> None:
        self.scenario = scenario
        self.protocol = protocol
        self.run = run
        self.path = capture_path(scenario, protocol, run, data_dir)
        self._ports = ports
        self._queue: queue.Queue = queue.Queue()
        self._links: list[BoardLink] = []
        self._by_node: dict[str, BoardLink] = {}
        # Each board's latest STATUS reply and when it arrived, for
        # poll_status(). Written by the reader threads, hence the lock.
        self._status: dict[str, tuple[float, dict[str, str]]] = {}
        self._status_lock = threading.Lock()
        self._writer: threading.Thread | None = None
        self._stop_writer = threading.Event()
        self._fh = None

    # ---- lifecycle ----

    def __enter__(self) -> "SwarmLogger":
        self.start()
        return self

    def __exit__(self, *exc) -> None:
        self.stop()

    def start(self) -> None:
        ports = self._ports if self._ports is not None else find_ports()
        if not ports:
            raise RuntimeError("no serial ports found")

        # Backstop for next_run(): never write over an existing capture.
        if os.path.exists(self.path):
            raise RuntimeError(
                f"{self.path} already exists. Captures are never overwritten; "
                "use next_run() or pick another run number."
            )

        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        self._fh = open(self.path, "x", newline="", encoding="utf-8")
        self._csv = csv.writer(self._fh)
        self._csv.writerow(CSV_HEADER)

        links, failed = open_together(ports)
        for port, error in failed.items():
            print(f"{port}: {error}", file=sys.stderr)

        for link in links:
            node = link.identify()
            if node in ("?", "0"):
                print(
                    f"{link.port}: no node ID (SETID it first), rows will be "
                    f"attributed to '{node}'",
                    file=sys.stderr,
                )
            self._links.append(link)
            self._by_node[node] = link
            link.start_reading(self._on_line)

        if not self._links:
            raise RuntimeError("no board could be opened")

        self._writer = threading.Thread(target=self._drain, daemon=True)
        self._writer.start()

        print(
            f"logging {len(self._links)} board(s) "
            f"{sorted(self._by_node)} -> {self.path}",
            file=sys.stderr,
        )

    def stop(self) -> None:
        for link in self._links:
            link.stop_reading()
        time.sleep(0.5)
        self._stop_writer.set()
        if self._writer is not None:
            self._writer.join(timeout=3.0)
        for link in self._links:
            link.close()
            if link.error:
                print(f"{link.port}: {link.error}", file=sys.stderr)
        if self._fh is not None:
            self._fh.close()
            self._fh = None

    # ---- commands ----

    def send(self, node: int | str, command: str) -> bool:
        """Sends one command to one board and records it on the same clock."""
        key = str(node)
        link = self._by_node.get(key)
        if link is None:
            print(f"no board with node id {key}", file=sys.stderr)
            return False
        now = time.time()
        link.write(command)
        self._queue.put(Event.command(now, key, command))
        return True

    def broadcast(self, command: str) -> None:
        now = time.time()
        for node, link in self._by_node.items():
            link.write(command)
            self._queue.put(Event.command(now, node, command))

    def mark(self, label: str) -> None:
        """Drops a labelled row in, to mark a phase boundary in the run."""
        self._queue.put(Event.mark(time.time(), label))

    def poll_status(self, timeout: float = 1.5) -> dict[str, dict[str, str]]:
        """Asks every board for STATUS and returns {node: fields}.

        Only replies that arrived after this request count, so a board that
        does not answer in time is simply absent from the result. The request
        and the replies go into the capture like any other line, which is
        wanted: with a protocol whose leader is not fixed in advance, they are
        the record of who led.
        """
        asked = time.time()
        self.broadcast("STATUS")
        deadline = time.monotonic() + timeout
        while True:
            with self._status_lock:
                fresh = {node: fields
                         for node, (when, fields) in self._status.items()
                         if when >= asked}
            if len(fresh) == len(self._by_node) or time.monotonic() >= deadline:
                return fresh
            time.sleep(0.05)

    @property
    def nodes(self) -> list[str]:
        return sorted(self._by_node)

    # ---- internals ----

    def _on_line(self, link: BoardLink, pi_time: float, line: str) -> None:
        if line.startswith("STATUS "):
            with self._status_lock:
                self._status[link.node] = (pi_time, parse_status(line))
        self._queue.put(Event.from_board_line(pi_time, line, link.node))

    def _drain(self) -> None:
        while not self._stop_writer.is_set() or not self._queue.empty():
            try:
                event = self._queue.get(timeout=0.2)
            except queue.Empty:
                continue
            self._csv.writerow(event.as_csv_row())
            self._fh.flush()
