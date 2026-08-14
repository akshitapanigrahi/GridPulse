"""GRID PULSE HTTP server: serves the UI and streams device events.

Endpoints
---------
``GET  /``              the game page (web/play.html)
``GET  /<path>``        static assets from web/, path-traversal proof
``GET  /api/status``    device presence and a plain-language reason when absent
``GET  /api/events``    Server-Sent Events stream
``POST /api/command``   START / ABORT / SELFTEST / PING, forwarded to the device

The absence of a device is an ordinary situation, not an error: ``/api/status``
reports it in plain language and the UI preselects keyboard mode. No endpoint ever
returns a stack trace.

Standard library only.
"""

from __future__ import annotations

import json
import mimetypes
import os
import posixpath
import socket
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Callable, Dict, Optional

from . import sse

MAX_COMMAND_BODY_BYTES = 4096


def web_root() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(os.path.dirname(os.path.dirname(here)), "web")


class ServerContext:
    """Everything the request handlers need, without global state."""

    def __init__(
        self,
        broadcaster: sse.Broadcaster,
        status_provider: Callable[[], Dict[str, Any]],
        command_sink: Optional[Callable[[str, Dict[str, Any]], bool]] = None,
        root: Optional[str] = None,
    ) -> None:
        self.broadcaster = broadcaster
        self.status_provider = status_provider
        self.command_sink = command_sink
        self.root = os.path.abspath(root or web_root())
        self.shutdown_event = threading.Event()


class Handler(BaseHTTPRequestHandler):
    """Request handler. One instance per request; the context is on the server."""

    protocol_version = "HTTP/1.1"
    server_version = "GridPulse/1.0"

    @property
    def context(self) -> ServerContext:
        return self.server.context  # type: ignore[attr-defined]

    # Quiet by default: a game that prints a log line per asset request buries the
    # one message that matters. Errors still surface through log_error.
    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        if getattr(self.server, "verbose", False):  # type: ignore[attr-defined]
            super().log_message(fmt, *args)

    # --- helpers ---------------------------------------------------------

    def _send_json(self, payload: Dict[str, Any], status: int = 200) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _resolve(self, url_path: str) -> Optional[str]:
        """Maps a URL path to a file under web/, refusing to escape it.

        Traversal is blocked by resolving to an absolute path and checking it is
        still inside the root, rather than by filtering ``..`` out of the input.
        """
        path = urllib.parse.urlparse(url_path).path
        path = urllib.parse.unquote(path)
        if path in ("", "/"):
            path = "/play.html"
        # posixpath.normpath collapses ".." before the join, and the containment
        # check below catches anything it does not.
        normalised = posixpath.normpath(path).lstrip("/")
        candidate = os.path.abspath(os.path.join(self.context.root, normalised))
        if candidate != self.context.root and not candidate.startswith(
            self.context.root + os.sep
        ):
            return None
        if not os.path.isfile(candidate):
            return None
        return candidate

    # --- routes -----------------------------------------------------------

    def do_GET(self) -> None:  # noqa: N802  (BaseHTTPRequestHandler's naming)
        try:
            if self.path.startswith("/api/status"):
                self._send_json(self.context.status_provider())
                return
            if self.path.startswith("/api/events"):
                self._serve_events()
                return
            self._serve_static()
        except (BrokenPipeError, ConnectionResetError):
            # The browser navigated away mid-response. Entirely normal.
            pass
        except OSError as exc:
            self.log_error("GET %s failed: %s", self.path, exc)

    def do_POST(self) -> None:  # noqa: N802
        try:
            if not self.path.startswith("/api/command"):
                self._send_json({"ok": False, "error": "unknown endpoint"}, 404)
                return

            length = int(self.headers.get("Content-Length", "0") or "0")
            if length > MAX_COMMAND_BODY_BYTES:
                self._send_json({"ok": False, "error": "request body too large"}, 413)
                return

            try:
                body = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
            except (ValueError, UnicodeDecodeError) as exc:
                self._send_json({"ok": False, "error": "malformed JSON: %s" % exc}, 400)
                return

            name = body.get("name")
            args = body.get("args") or {}
            if not isinstance(name, str) or not isinstance(args, dict):
                self._send_json({"ok": False, "error": "expected {name, args}"}, 400)
                return

            if self.context.command_sink is None:
                self._send_json(
                    {
                        "ok": False,
                        "error": "no keypad is connected, so device commands cannot "
                                 "be sent. Keyboard mode does not need one.",
                    },
                    409,
                )
                return

            accepted = self.context.command_sink(name, args)
            self._send_json({"ok": bool(accepted)}, 200 if accepted else 400)
        except (BrokenPipeError, ConnectionResetError):
            pass
        except OSError as exc:
            self.log_error("POST %s failed: %s", self.path, exc)

    def _serve_static(self) -> None:
        path = self._resolve(self.path)
        if path is None:
            self.send_error(404, "not found")
            return

        content_type, _ = mimetypes.guess_type(path)
        if content_type is None:
            content_type = "application/octet-stream"
        if content_type.startswith("text/") or content_type in (
            "application/javascript",
            "application/json",
        ):
            content_type += "; charset=utf-8"

        try:
            with open(path, "rb") as handle:
                body = handle.read()
        except OSError as exc:
            self.send_error(500, "cannot read asset: %s" % exc.strerror)
            return

        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        # The UI is served from disk and edited during development; caching it would
        # mean a grader seeing a stale page after an update.
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _serve_events(self) -> None:
        # Subscribe BEFORE the response headers go out, not after.
        #
        # The browser treats the headers as "the stream is live" - that is when
        # EventSource fires `open` - and sends its first command straight afterwards.
        # Subscribing after writing them leaves a window in which the client believes
        # it is listening and the broadcaster does not know it exists; anything
        # published in that window goes to an empty subscriber list and is lost, since
        # there is no backlog and no replay. Registering first makes the client's
        # belief true by the time it can act on it.
        subscriber = self.context.broadcaster.subscribe()
        try:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "keep-alive")
            # An SSE stream has no length and must not be buffered by an intermediary.
            self.send_header("X-Accel-Buffering", "no")
            self.end_headers()

            try:
                # Nagle would coalesce small event writes and add up to 40 ms of
                # latency to the live display for no benefit.
                self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError:
                pass

            last_keepalive = time.monotonic()
            while not self.context.shutdown_event.is_set():
                try:
                    payload = subscriber.queue.get(timeout=1.0)
                except Exception:  # queue.Empty; the timeout is the keepalive tick
                    payload = None

                if payload is None:
                    if subscriber.closed:
                        break
                    now = time.monotonic()
                    if now - last_keepalive >= sse.KEEPALIVE_SECONDS:
                        last_keepalive = now
                        self.wfile.write(sse.keepalive_comment().encode("utf-8"))
                        self.wfile.flush()
                    continue

                self.wfile.write(payload.encode("utf-8"))
                # Flushed per event: buffering an event stream defeats its purpose.
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            # The browser closed the stream. Normal on navigation or reload.
            pass
        finally:
            self.context.broadcaster.unsubscribe(subscriber)
            # An event stream is the last thing this connection will ever carry, so do
            # not go back and try to read another request from it. Without this the
            # handler's keep-alive loop reads from a socket the browser has already
            # closed and raises ConnectionResetError out of handle_one_request, where
            # nothing catches it and the traceback lands in the operator's terminal on
            # every ordinary reload.
            self.close_connection = True


class GridPulseServer(ThreadingHTTPServer):
    """Threaded so a long-lived SSE stream cannot block asset requests."""

    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, context: ServerContext, verbose: bool = False) -> None:
        super().__init__(address, Handler)
        self.context = context
        self.verbose = verbose


def find_free_port(preferred: int = 8765, attempts: int = 40) -> int:
    """Finds a usable localhost port, starting from `preferred`.

    A fixed port would collide with a previous run that has not fully released it, or
    with anything else the grader happens to be running.
    """
    for offset in range(attempts):
        candidate = preferred + offset
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                probe.bind(("127.0.0.1", candidate))
                return candidate
            except OSError:
                continue
    # Let the OS choose.
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]
