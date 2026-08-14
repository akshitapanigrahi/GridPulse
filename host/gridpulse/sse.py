"""GRID PULSE Server-Sent Events broadcasting.

WHY SSE AND NOT WEBSOCKETS
--------------------------
The browser is a display. Events flow device -> host -> browser and never the other
way; commands go over ordinary POSTs. SSE is exactly that shape, is a few dozen lines
over the standard library's HTTP server, and needs no dependency. WebSockets would
mean either a third-party package or hand-rolling a framing and masking layer for a
capability the design does not use.

BACKPRESSURE POLICY
-------------------
Each subscriber has a bounded queue. If a browser stalls - a background tab, a
throttled renderer - its queue fills. When that happens the broadcaster drops
PRESENTATION events (ticks) and never drops SCORING events (targets, hits, misses,
mode changes, the final tally). A dropped tick makes the display briefly stale; a
dropped hit would make it wrong.

If even the scoring events cannot be delivered the subscriber is disconnected, which
the UI surfaces as a link warning. The reported score is unaffected either way: it is
computed on the device.

Standard library only.
"""

from __future__ import annotations

import json
import queue
import threading
import time
from typing import Any, Dict, List, Optional

# Per-subscriber queue depth. A browser that falls this far behind is not going to
# catch up by being given more buffer.
SUBSCRIBER_QUEUE_DEPTH = 256

# Keepalive interval. Proxies and some browsers close an idle event stream; an SSE
# comment costs three bytes and keeps it open.
KEEPALIVE_SECONDS = 15.0

# Event types that may be dropped when a subscriber is behind.
DROPPABLE_TYPES = frozenset({"TICK", "LOG", "STATUS"})


class Subscriber:
    """One connected browser."""

    def __init__(self) -> None:
        self.queue: "queue.Queue[Optional[str]]" = queue.Queue(SUBSCRIBER_QUEUE_DEPTH)
        self.dropped = 0
        self.closed = False

    def offer(self, payload: str, droppable: bool) -> bool:
        """Enqueues a payload.

        Returns False only when a non-droppable event could not be delivered, which
        tells the broadcaster to disconnect this subscriber rather than let it show
        a silently incomplete run.
        """
        if self.closed:
            return False
        try:
            self.queue.put_nowait(payload)
            return True
        except queue.Full:
            self.dropped += 1
            if droppable:
                return True
            # Make room by discarding the oldest droppable payload rather than
            # dropping the scoring event now in hand.
            try:
                self.queue.get_nowait()
                self.queue.put_nowait(payload)
                return True
            except (queue.Empty, queue.Full):
                return False

    def close(self) -> None:
        self.closed = True
        try:
            self.queue.put_nowait(None)
        except queue.Full:
            pass


class Broadcaster:
    """Fans events out to every connected browser."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._subscribers: List[Subscriber] = []
        self._last_snapshot: Optional[Dict[str, Any]] = None
        self.total_dropped = 0

    def subscribe(self) -> Subscriber:
        subscriber = Subscriber()
        with self._lock:
            self._subscribers.append(subscriber)
            snapshot = self._last_snapshot
        # A browser that connects mid-run needs the current state immediately rather
        # than waiting for the next event.
        if snapshot is not None:
            subscriber.offer(_encode(snapshot), droppable=True)
        return subscriber

    def unsubscribe(self, subscriber: Subscriber) -> None:
        subscriber.close()
        with self._lock:
            if subscriber in self._subscribers:
                self._subscribers.remove(subscriber)

    def publish(self, event: Dict[str, Any]) -> None:
        """Sends one event to every subscriber."""
        payload = _encode(event)
        droppable = event.get("type") in DROPPABLE_TYPES

        if event.get("type") == "STATUS":
            with self._lock:
                self._last_snapshot = event

        with self._lock:
            subscribers = list(self._subscribers)

        for subscriber in subscribers:
            if not subscriber.offer(payload, droppable):
                self.total_dropped += subscriber.dropped
                self.unsubscribe(subscriber)

    def subscriber_count(self) -> int:
        with self._lock:
            return len(self._subscribers)

    def close_all(self) -> None:
        with self._lock:
            subscribers = list(self._subscribers)
            self._subscribers.clear()
        for subscriber in subscribers:
            subscriber.close()


def _encode(event: Dict[str, Any]) -> str:
    """Formats one SSE message.

    The blank line terminates the message; without it the browser buffers forever.
    """
    return "data: %s\n\n" % json.dumps(event, separators=(",", ":"))


def keepalive_comment() -> str:
    """An SSE comment. Ignored by the client, keeps the connection from idling out."""
    return ": keepalive %d\n\n" % int(time.time())
