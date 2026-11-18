"""One board's serial port."""

from __future__ import annotations

import sys
import threading
import time
from typing import Callable

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial is missing. Run: pip install -r requirements.txt")

from .ports import BAUD, REPLY_TIMEOUT_S, RESET_WAIT_S, parse_status

# Called for every line a board prints: (link, pi_time, line).
LineHandler = Callable[["BoardLink", float, str], None]


class BoardLink:
    """Owns one serial port: reads it, and is the only writer to it.

    Serial ports are exclusive, so while a BoardLink holds a port nothing else
    can, including a serial monitor.
    """

    def __init__(self, port: str, baud: int = BAUD) -> None:
        self.port = port
        self.baud = baud
        self.node = "?"   # node ID, once identify() has asked
        self.error = ""   # set if the port dies while being read
        self._ser: serial.Serial | None = None
        self._write_lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def open(self) -> None:
        """Opens the port, which resets the board. settle() must follow."""
        self._ser = serial.Serial(self.port, self.baud, timeout=0.3)

    def settle(self) -> None:
        """Discards the boot banner once the board has finished resetting."""
        if self._ser is not None:
            self._ser.reset_input_buffer()

    def status(self, timeout: float = REPLY_TIMEOUT_S) -> dict[str, str]:
        """Asks STATUS and returns its fields, or {} if the board said nothing.

        Reads the port directly, so call it before start_reading().
        """
        if self._ser is None:
            return {}
        self.write("STATUS")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self._ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if line.startswith("STATUS "):
                return parse_status(line)
        return {}

    def identify(self) -> str:
        """Learns which node is on this port, so output can be attributed."""
        self.node = self.status().get("id", "?")
        return self.node

    def write(self, command: str) -> None:
        if self._ser is None:
            raise RuntimeError(f"{self.port} is not open")
        with self._write_lock:
            self._ser.write((command + "\n").encode())
            self._ser.flush()

    def start_reading(self, on_line: LineHandler) -> None:
        """Hands every line the board prints to on_line, on its own thread."""
        self._thread = threading.Thread(
            target=self._read_loop, args=(on_line,), daemon=True
        )
        self._thread.start()

    def stop_reading(self) -> None:
        self._stop.set()

    def close(self) -> None:
        if self._ser is not None:
            self._ser.close()
            self._ser = None

    def _read_loop(self, on_line: LineHandler) -> None:
        if self._ser is None:
            return
        try:
            while not self._stop.is_set():
                raw = self._ser.readline()
                if not raw:
                    continue
                # Stamp on arrival, before any parsing, to keep the clock tight.
                now = time.time()
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    on_line(self, now, line)
        except serial.SerialException as exc:
            self.error = str(exc)


def open_together(
    ports: list[str], baud: int = BAUD
) -> tuple[list[BoardLink], dict[str, str]]:
    """Opens every port back to back, then waits once for the boards to boot.

    Opening a port resets that board. Opening and waiting one at a time would
    stagger the resets by RESET_WAIT_S each, twenty seconds for ten boards,
    during which the boards already running watch each other drop out and log
    spurious NEIGHBOR_LOST events. Opening together keeps the disturbance to
    one short window before anything is recorded.

    Returns the links that opened, in port order, and {port: error} for the
    ones that did not.
    """
    links: list[BoardLink] = []
    failed: dict[str, str] = {}
    for port in ports:
        link = BoardLink(port, baud)
        try:
            link.open()
        except serial.SerialException as exc:
            failed[port] = str(exc)
            continue
        links.append(link)

    if links:
        time.sleep(RESET_WAIT_S)
    for link in links:
        link.settle()
    return links, failed
