"""Event-stream delivery tests, over a real socket against a real server.

WHY A REAL SOCKET
-----------------
The bug these were written for is an ordering property between two threads and a
network client, and it is invisible to anything that calls ``Broadcaster`` directly.

``_serve_events`` used to write the response headers first and subscribe afterwards.
The browser treats the headers as "the stream is live" - that is exactly when
EventSource fires ``open`` - and the UI sent START as soon as it did. In the window
between the two, the client believed it was listening and the broadcaster did not know
it existed, so anything published landed in an empty subscriber list. There is no
backlog and no replay, so it was gone for good.

What that looked like from the outside: the first run of a session had no 3-2-1
countdown. MODE COUNTDOWN was published into that window and lost; the screen sat
blank for three seconds until MODE RUNNING arrived. Later runs usually won the same
race on a warm host, which disguised a race as a first-run quirk.

The gated test below pins the ordering directly rather than trying to lose a race on
purpose, which would be flaky in both directions.
"""

from __future__ import annotations

import os
import socket
import sys
import threading
import time
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from gridpulse import server, sse  # noqa: E402

HEADER_TERMINATOR = b"\r\n\r\n"


class GatedBroadcaster(sse.Broadcaster):
    """A broadcaster whose subscribe() can be held open by the test.

    This turns "did the handler subscribe before it sent headers?" from a race into an
    assertion: hold subscribe(), then check the client cannot yet see headers.
    """

    def __init__(self) -> None:
        super().__init__()
        self.entered = threading.Event()
        self.gate = threading.Event()
        self.gate.set()

    def subscribe(self) -> sse.Subscriber:
        self.entered.set()
        self.gate.wait(timeout=5.0)
        return super().subscribe()


class ServerFixture:
    """A real GridPulseServer on a real localhost port."""

    def __init__(self, broadcaster: sse.Broadcaster) -> None:
        self.broadcaster = broadcaster
        context = server.ServerContext(
            broadcaster=broadcaster,
            status_provider=lambda: {"device": {"connected": False}},
        )
        self.server = server.GridPulseServer(("127.0.0.1", 0), context)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self) -> None:
        self.server.context.shutdown_event.set()
        self.broadcaster.close_all()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5.0)

    def open_stream(self) -> socket.socket:
        """Opens /api/events and returns the socket, without reading the response."""
        client = socket.create_connection(("127.0.0.1", self.port), timeout=5.0)
        client.sendall(
            b"GET /api/events HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept: text/event-stream\r\n\r\n"
        )
        return client


def read_headers(client: socket.socket) -> bytes:
    buffer = b""
    while HEADER_TERMINATOR not in buffer:
        chunk = client.recv(4096)
        if not chunk:
            raise AssertionError("stream closed before headers arrived")
        buffer += chunk
    return buffer


class TestEventStreamSubscribesBeforeAnnouncingItself(unittest.TestCase):
    def test_no_headers_reach_the_client_until_the_subscription_exists(self):
        """The ordering guarantee, asserted directly.

        With subscribe() held, the client must see nothing at all. If headers were
        written first, they would already be readable here and this fails - which is
        precisely the window that swallowed MODE COUNTDOWN.
        """
        broadcaster = GatedBroadcaster()
        broadcaster.gate.clear()
        fixture = ServerFixture(broadcaster)
        self.addCleanup(fixture.close)

        client = fixture.open_stream()
        self.addCleanup(client.close)

        self.assertTrue(
            broadcaster.entered.wait(timeout=5.0),
            "the handler never attempted to subscribe",
        )

        client.settimeout(0.5)
        with self.assertRaises(socket.timeout):
            client.recv(4096)

        # Releasing the gate completes the subscription, and only then does the client
        # learn the stream is live.
        broadcaster.gate.set()
        client.settimeout(5.0)
        headers = read_headers(client)
        self.assertIn(b"text/event-stream", headers)

    def test_an_event_published_the_instant_headers_arrive_is_delivered(self):
        """The behaviour the ordering exists to produce.

        This is the START-right-after-open sequence the UI performs, at the tightest
        timing a real client can manage.
        """
        broadcaster = sse.Broadcaster()
        fixture = ServerFixture(broadcaster)
        self.addCleanup(fixture.close)

        client = fixture.open_stream()
        self.addCleanup(client.close)

        read_headers(client)
        broadcaster.publish(
            {"seq": 7, "type": "MODE", "mode": "EVAL", "state": "COUNTDOWN", "n": 25}
        )

        client.settimeout(5.0)
        payload = client.recv(4096)
        self.assertIn(b'"type":"MODE"', payload)
        self.assertIn(b'"state":"COUNTDOWN"', payload)

    def test_a_subscriber_is_released_when_the_client_disconnects(self):
        """The fix moved subscribe() outside the try block's original scope; the
        unsubscribe in `finally` must still cover it, or every reconnect would leak a
        subscriber and every event would be fanned out to a growing list of dead
        queues."""
        broadcaster = sse.Broadcaster()
        fixture = ServerFixture(broadcaster)
        self.addCleanup(fixture.close)

        client = fixture.open_stream()
        read_headers(client)
        self.assertEqual(broadcaster.subscriber_count(), 1)

        # Shut down cleanly with nothing unread, so the server sees a FIN rather than
        # an RST. A reset would leave the handler thread raising out of its keep-alive
        # read and printing a traceback through the suite, which is noise this test has
        # no business generating.
        client.shutdown(socket.SHUT_RDWR)
        client.close()

        # The handler only notices on its next write, so nudge it.
        for _ in range(50):
            if broadcaster.subscriber_count() == 0:
                break
            broadcaster.publish({"seq": 1, "type": "TICK"})
            time.sleep(0.05)
        self.assertEqual(broadcaster.subscriber_count(), 0)


if __name__ == "__main__":
    unittest.main()
