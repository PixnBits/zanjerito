#!/usr/bin/env python3
"""Serve one fixture scenario over HTTP/1.1 (stdlib only)."""
from __future__ import annotations

import argparse
import os
import signal
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

GET_MAP = {
    "/api/kiosk": "kiosk.json",
    "/api/schedules": "schedules.json",
}


def _nap(seconds: float) -> None:
    if seconds <= 0:
        return
    end = time.monotonic() + seconds
    while True:
        left = end - time.monotonic()
        if left <= 0:
            return
        time.sleep(0.05 if left > 0.05 else left)


class FixtureHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        return

    def _record(self) -> None:
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        line = "%s %s %s\n" % (
            self.command,
            self.path,
            body.decode("utf-8", "replace"),
        )
        ts_line = "%.6f %s %s\n" % (time.monotonic(), self.command, self.path)
        with self.server.log_lock:
            with open(self.server.log_path, "a", encoding="utf-8") as f:
                f.write(line)
            with open(self.server.log_path + ".ts", "a", encoding="utf-8") as f:
                f.write(ts_line)

    def _send(self, data: bytes) -> None:
        try:
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, OSError):
            return

    def do_GET(self) -> None:
        _nap(self.server.get_delay)
        if getattr(self.server, "kiosk_404", False) and self.path.split("?", 1)[0] == "/api/kiosk":
            try:
                self.send_error(404)
            except (BrokenPipeError, ConnectionResetError, OSError):
                return
            return
        name = GET_MAP.get(self.path)
        if not name:
            self.send_error(404)
            return
        path = os.path.join(self.server.fixture_dir, name)
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            self.send_error(404)
            return
        self._send(data)

    def do_POST(self) -> None:
        self._record()
        _nap(self.server.post_delay)
        self._send(b"{}")

    def do_PUT(self) -> None:
        self.do_POST()

    def do_DELETE(self) -> None:
        self.do_POST()


class FixtureServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--dir", required=True, help="scenario fixture directory")
    p.add_argument("--log", required=True, help="append POST/PUT/DELETE lines here")
    p.add_argument("--port", type=int, default=0)
    p.add_argument("--get-delay", type=float, default=0, help="seconds to hold each GET")
    p.add_argument("--post-delay", type=float, default=0, help="seconds to hold each POST after logging it")
    p.add_argument("--kiosk-404", action="store_true", help="GET /api/kiosk returns 404")
    args = p.parse_args()

    httpd = FixtureServer(("127.0.0.1", args.port), FixtureHandler)
    httpd.fixture_dir = os.path.abspath(args.dir)
    httpd.log_path = args.log
    httpd.log_lock = threading.Lock()
    httpd.get_delay = args.get_delay if args.get_delay > 0 else 0.0
    httpd.post_delay = args.post_delay if args.post_delay > 0 else 0.0
    httpd.kiosk_404 = bool(args.kiosk_404)
    log_dir = os.path.dirname(os.path.abspath(args.log))
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
    open(args.log, "a", encoding="utf-8").close()
    open(args.log + ".ts", "a", encoding="utf-8").close()

    def _die(signum, frame):
        os._exit(0)

    signal.signal(signal.SIGTERM, _die)
    signal.signal(signal.SIGINT, _die)

    print("PORT %d" % httpd.server_address[1], flush=True)
    httpd.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
