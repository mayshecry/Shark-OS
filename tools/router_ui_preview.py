#!/usr/bin/env python3
"""Serve docs/router_ui.html as a standalone preview.

The page is completely self-contained. API paths intentionally return 404 so
the dashboard falls back to its built-in demo data; no router process, token
or network access is needed. Run from the repo root:

    python3 tools/router_ui_preview.py [port]
"""
import http.server
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "docs" / "router_ui.html"


class Handler(http.server.BaseHTTPRequestHandler):
    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self) -> None:  # noqa: N802
        self.do_GET()

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html", "/router_ui.html"):
            self._send(200, PAGE.read_bytes(), "text/html; charset=utf-8")
        elif path.startswith("/api/"):
            self._send(404, b'{"ok":false,"error":"preview only"}',
                       "application/json")
        else:
            self._send(404, b"Not found", "text/plain")

    def do_POST(self) -> None:  # noqa: N802
        self._send(404, b'{"ok":false,"error":"preview only"}',
                   "application/json")

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main() -> None:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    os.chdir(ROOT)
    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"Router dashboard preview on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
