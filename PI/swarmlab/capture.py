"""A recorded run, loaded for analysis."""

from __future__ import annotations

import csv

from .event import Event


class Capture:
    """Every event of one run, in the order the Pi recorded them.

    All the questions a scenario asks of a run ("when was node 1 killed?",
    "which LEADER lines did node 3 log after that?") go through here, so the
    scenarios hold the reasoning and none of the CSV handling.
    """

    def __init__(self, path: str) -> None:
        self.path = path
        with open(path, encoding="utf-8") as fh:
            self.events = [Event.from_csv_row(row) for row in csv.DictReader(fh)]

    def __len__(self) -> int:
        return len(self.events)

    def __iter__(self):
        return iter(self.events)

    def select(
        self,
        name: str | None = None,
        *,
        node: str | None = None,
        source: str | None = None,
        start: float | None = None,   # at or after this time
        end: float | None = None,     # at or before this time
        before: float | None = None,  # strictly before this time
    ) -> list[Event]:
        """The events matching every condition given, in recorded order."""
        out = []
        for e in self.events:
            if name is not None and e.name != name:
                continue
            if node is not None and e.node != node:
                continue
            if source is not None and e.source != source:
                continue
            if start is not None and e.time < start:
                continue
            if end is not None and e.time > end:
                continue
            if before is not None and e.time >= before:
                continue
            out.append(e)
        return out

    def command_time(self, node: str, text: str, after: float = 0.0) -> float | None:
        """When the Pi first sent `text` to `node`, at or after `after`."""
        for e in self.select("CMD", node=node, source="pi", start=after):
            if e.fields == text:
                return e.time
        return None

    def mark_time(self, label: str) -> float | None:
        for e in self.select("MARK"):
            if e.fields == label:
                return e.time
        return None

    def boards(self) -> list[str]:
        """Node IDs of every board that logged anything."""
        return sorted({e.node for e in self.events if e.source == "board" and e.node})
