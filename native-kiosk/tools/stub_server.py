#!/usr/bin/env python3
"""Serve one fixture scenario over HTTP/1.1 (stdlib only)."""
from __future__ import annotations

import argparse
import os
import signal
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

GET_MAP = {
    "/api/status": "status.json",
    "/api/stations": "stations.json",
    "/api/schedules": "schedules.json",
    "/api/soil": "soil.json",
}


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
        with open(self.server.log_path, "a", encoding="utf-8") as f:
            f.write(line)

    def do_GET(self) -> None:
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
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self) -> None:
        self._record()
        body = b"{}"
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_PUT(self) -> None:
        self.do_POST()

    def do_DELETE(self) -> None:
        self.do_POST()


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--dir", required=True, help="scenario fixture directory")
    p.add_argument("--log", required=True, help="append POST/PUT/DELETE lines here")
    p.add_argument("--port", type=int, default=0)
    args = p.parse_args()

    httpd = HTTPServer(("127.0.0.1", args.port), FixtureHandler)
    httpd.fixture_dir = os.path.abspath(args.dir)
    httpd.log_path = args.log
    log_dir = os.path.dirname(os.path.abspath(args.log))
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
    open(args.log, "a", encoding="utf-8").close()

    def _die(signum, frame):
        os._exit(0)

    signal.signal(signal.SIGTERM, _die)
    signal.signal(signal.SIGINT, _die)

    print("PORT %d" % httpd.server_address[1], flush=True)
    httpd.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
