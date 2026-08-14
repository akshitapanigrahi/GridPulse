"""GRID PULSE session replay.

Plays a recorded JSON Lines log back through the broadcaster at its original timing,
so a run can be reviewed, demonstrated, or debugged without the hardware present.

Timing comes from the device timestamps in the log (``t_us``), not from wall-clock
gaps between writes, so the playback reproduces what the device did rather than what
the logger happened to do.

Standard library only.
"""

from __future__ import annotations

import json
import threading
import time
from typing import Any, Callable, Dict, Iterator, List, Optional, Tuple

# The longest gap between events that a replay reproduces exactly. Anything longer is
# collapsed to this.
#
# A log starts when the HOST started, not when anyone played, so a recording routinely
# opens with a minute of nothing and carries minutes more between runs. Reproducing
# that faithfully is technically honest and useless to watch: the default replay sat on
# a blank grid for 63 seconds.
#
# The cut is clean because of how the device behaves, not because of a guess. During a
# run it ticks four times a second, so a run is never quiet for longer than 250 ms. The
# one long gap that carries meaning is the 3-second countdown, which must survive
# intact or the 3-2-1 is wrong. Every other gap in a real log is at least 5 seconds and
# is the device sitting idle. 4 seconds is comfortably above the countdown and
# comfortably below the shortest idle stretch.
#
# So the timing WITHIN a run - target intervals, reaction times, the countdown - is
# reproduced exactly. Only dead air is compressed.
MAX_REPLAYED_GAP_S = 4.0


class ReplayError(Exception):
    """Raised for a log that cannot be read or contains no replayable events."""


def load_events(path: str) -> List[Dict[str, Any]]:
    """Reads a JSON Lines log, skipping records that are not device events.

    A truncated final line - the usual result of a log written up to the moment
    someone unplugged the board - is skipped rather than treated as fatal.
    """
    events: List[Dict[str, Any]] = []
    try:
        with open(path, "r", encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                except ValueError:
                    # Almost always the last line of a log cut short. Anything else is
                    # a corrupt file, and skipping is still the useful behaviour.
                    continue
                if not isinstance(record, dict):
                    continue
                if record.get("type") in ("SESSION", "RAW", "REPORT"):
                    continue
                if "t_us" in record:
                    events.append(record)
                _ = number
    except OSError as exc:
        raise ReplayError("cannot read %s: %s" % (path, exc.strerror)) from exc

    if not events:
        raise ReplayError("%s contains no replayable device events" % path)
    return events


def gap_seconds(previous_us: int, current_us: int) -> float:
    """The wall time to wait between two events, with dead air collapsed.

    Clamped at both ends. Negative gaps are not a corrupt log: END carries the run's
    elapsed time rather than device uptime, so it legitimately runs backwards against
    its neighbours, and treating that as a wait would stall the replay.
    """
    gap = (current_us - previous_us) / 1e6
    if gap < 0.0:
        return 0.0
    return min(gap, MAX_REPLAYED_GAP_S)


def replay_span(events: List[Dict[str, Any]]) -> Tuple[float, float]:
    """(recorded seconds, seconds this will actually take) at speed 1."""
    recorded = 0.0
    played = 0.0
    for index in range(len(events) - 1):
        a = int(events[index].get("t_us", 0))
        b = int(events[index + 1].get("t_us", 0))
        recorded += max(0.0, (b - a) / 1e6)
        played += gap_seconds(a, b)
    return recorded, played


def iter_with_original_timing(
    events: List[Dict[str, Any]], speed: float = 1.0
) -> Iterator[Dict[str, Any]]:
    """Yields events, sleeping to reproduce the original inter-event gaps.

    Gaps longer than MAX_REPLAYED_GAP_S are collapsed: see the note there. Everything
    shorter - which is everything inside a run - is reproduced exactly.
    """
    if speed <= 0:
        raise ReplayError("replay speed must be positive, got %r" % speed)

    started_wall = time.monotonic()
    elapsed_s = 0.0
    previous_us = int(events[0].get("t_us", 0))

    for event in events:
        current_us = int(event.get("t_us", previous_us))
        elapsed_s += gap_seconds(previous_us, current_us) / speed
        previous_us = current_us

        delay = started_wall + elapsed_s - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        yield event


class ReplaySource:
    """Feeds a recorded log into the same pipeline a live device would."""

    def __init__(self, path: str, speed: float = 1.0, loop: bool = False) -> None:
        self.path = path
        self.speed = speed
        self.loop = loop
        self.events = load_events(path)
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    def describe(self) -> str:
        recorded, played = replay_span(self.events)
        played /= self.speed
        if played < recorded - 1.0:
            return ("%d events, %.0f s recorded, about %.0f s to watch "
                    "(idle stretches skipped)" % (len(self.events), recorded, played))
        return "%d events spanning %.0f s" % (len(self.events), recorded)

    def start(self, on_event: Callable[[Dict[str, Any]], None]) -> None:
        def run() -> None:
            while not self._stop.is_set():
                for event in iter_with_original_timing(self.events, self.speed):
                    if self._stop.is_set():
                        return
                    on_event(event)
                if not self.loop:
                    return

        self._thread = threading.Thread(target=run, name="gridpulse-replay", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
