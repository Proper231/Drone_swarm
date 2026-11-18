"""Which serial ports exist, and what a board says about itself."""

from __future__ import annotations

import glob
import platform
import sys

try:
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    sys.exit("pyserial is missing. Run: pip install -r requirements.txt")

BAUD = 115200
BY_PATH_GLOB = "/dev/serial/by-path/*"

# The ESP32 resets when the serial port is opened (DTR is wired to the reset
# circuit), so nothing it prints is readable until the boot is finished.
RESET_WAIT_S = 2.0
REPLY_TIMEOUT_S = 3.0


def find_ports() -> list[str]:
    """Serial ports, preferring the stable by-path names on Linux.

    by-path names are tied to the physical hub port, so they survive reboots
    and board swaps. /dev/ttyUSB* does not, and by-id cannot tell these boards
    apart because their USB bridges share one serial number.
    """
    if platform.system() == "Linux":
        ports = sorted(glob.glob(BY_PATH_GLOB))
        if ports:
            return ports
        print(
            f"No entries under {BY_PATH_GLOB}. Is the hub powered, and are the "
            "cables data cables rather than charge-only?",
            file=sys.stderr,
        )
    # Windows and macOS, or Linux without by-path entries.
    return [p.device for p in sorted(list_ports.comports(), key=lambda p: p.device)]


def parse_status(line: str) -> dict[str, str]:
    """'STATUS id=1 label=D1 mac=AA:..' -> {'id': '1', 'label': 'D1', ...}"""
    out: dict[str, str] = {}
    for token in line.split()[1:]:
        if "=" in token:
            key, _, value = token.partition("=")
            out[key] = value
    return out
