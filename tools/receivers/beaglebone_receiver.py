#!/usr/bin/env python3
"""Minimal JPEG upload receiver for BeagleBone.

Accepts HTTP POST /upload with raw image/jpeg payload and saves to disk.
"""

from __future__ import annotations

from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

HOST = "0.0.0.0"
PORT = 8080
OUTPUT_DIR = Path("captures_beaglebone")


class UploadHandler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        if self.path != "/upload":
            self.send_error(404, "Use POST /upload")
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        content_type = self.headers.get("Content-Type", "")

        if content_length <= 0:
            self.send_error(400, "Empty payload")
            return

        if content_type != "image/jpeg":
            self.send_error(415, "Expected Content-Type: image/jpeg")
            return

        payload = self.rfile.read(content_length)
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        filename = datetime.now().strftime("capture_%Y%m%d_%H%M%S_%f.jpg")
        out_path = OUTPUT_DIR / filename
        out_path.write_bytes(payload)

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")
        print(f"saved: {out_path} ({len(payload)} bytes)")

    def log_message(self, fmt: str, *args) -> None:
        print(f"[beaglebone] {self.address_string()} - {fmt % args}")


if __name__ == "__main__":
    print(f"BeagleBone receiver listening on http://{HOST}:{PORT}/upload")
    HTTPServer((HOST, PORT), UploadHandler).serve_forever()
