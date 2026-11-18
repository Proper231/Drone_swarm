"""One line of a capture.

The recorder writes Events and the analysis reads them back, so the CSV layout
is defined here and nowhere else:

    pi_time, source, node, board_ms, event, fields

`pi_time` is the Pi's clock at the moment the line arrived, and it is the only
time the analysis uses. `board_ms` is the board's own millis(), kept for
reference: nothing synchronises the boards' clocks with each other.
"""

from __future__ import annotations

from dataclasses import dataclass

CSV_HEADER = ["pi_time", "source", "node", "board_ms", "event", "fields"]


@dataclass(frozen=True)
class Event:
    time: float     # Pi clock, seconds
    source: str     # "board", or "pi" for commands and marks
    node: str       # who said it, or who a command was sent to
    board_ms: str
    name: str       # KILL, LEADER, COUNTERS, CMD, MARK, RAW ...
    fields: str     # everything after the event name, exactly as logged

    # ---- making them ----

    @classmethod
    def from_board_line(cls, pi_time: float, line: str, fallback_node: str) -> "Event":
        """A line a board printed.

        LOG lines are split on their fixed prefix. Anything else (STATUS
        replies, OK/ERR, the boot banner) is kept verbatim as a RAW event
        rather than thrown away, because it is usually what explains a run
        that went wrong.
        """
        parts = line.split(",")
        if len(parts) >= 4 and parts[0] == "LOG":
            return cls(pi_time, "board", parts[1], parts[2], parts[3],
                       ",".join(parts[4:]))
        return cls(pi_time, "board", fallback_node, "", "RAW", line)

    @classmethod
    def command(cls, pi_time: float, node: str, text: str) -> "Event":
        """A command the Pi sent, recorded on the same clock as the replies."""
        return cls(pi_time, "pi", node, "", "CMD", text)

    @classmethod
    def mark(cls, pi_time: float, label: str) -> "Event":
        """A labelled boundary between the phases of a run."""
        return cls(pi_time, "pi", "", "", "MARK", label)

    @classmethod
    def from_csv_row(cls, row: dict) -> "Event":
        return cls(float(row["pi_time"]), row["source"], row["node"],
                   row["board_ms"], row["event"], row["fields"])

    def as_csv_row(self) -> list[str]:
        return [f"{self.time:.6f}", self.source, self.node, self.board_ms,
                self.name, self.fields]

    # ---- reading them ----

    def get(self, key: str, default: str = "") -> str:
        """The value of `key=value` in the fields, or default."""
        for token in self.fields.split(","):
            k, _, v = token.partition("=")
            if k == key:
                return v
        return default

    def number(self, key: str) -> int:
        return int(self.get(key) or 0)

    @property
    def first(self) -> str:
        """The leading field. LEADER and TAKEOVER put the node id there, and
        radio events put the radio ("LORA", "ESPNOW")."""
        return self.fields.split(",")[0] if self.fields else ""
