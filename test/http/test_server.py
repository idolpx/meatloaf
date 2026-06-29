#!/usr/bin/env python3
"""
Test HTTP server for Meatloaf Full HTTP Client Mode verification.

This server provides:
1. /echo — universal echo endpoint: method, headers, query params, and body echoed back
2. /echo/status/<code> — returns arbitrary HTTP status code for error testing
3. /echo/headers — only returns response headers (no body) for header-mode testing
4. /static/<path> — predefined static content for basic GET tests
5. POST/PUT storage (original) — stores body and returns it on GET
6. /debug/* — debug endpoints

Every request gets a unique request ID for tracing through the
C64 → Meatloaf → Server chain. All I/O is logged with hex dumps
for troubleshooting.
"""

import http.server
import socketserver
import json
import logging
import time
import os
import sys
import uuid
import socket
from urllib.parse import urlparse
from datetime import datetime

# ── Configuration ────────────────────────────────────────────────────────────
LOG_LEVEL = os.environ.get("MEATLOAF_TEST_LOG_LEVEL", "INFO").upper()
LISTEN_ADDR = os.environ.get("MEATLOAF_LISTEN_ADDR", "")
LISTEN_PORT = int(os.environ.get("MEATLOAF_LISTEN_PORT", "8080"))

# ── Logging ──────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=getattr(logging, LOG_LEVEL),
    format="%(asctime)s.%(msecs)03d [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("meatloaf-test-server")

# ── In-memory storage ────────────────────────────────────────────────────────
stored_data: dict[str, dict] = {}
request_log: list[dict] = []


class MeatloafTestHandler(http.server.BaseHTTPRequestHandler):
    """HTTP handler with full echo and debug capabilities."""

    # Silence default per-request logging (we do our own)
    def log_message(self, format, *args):
        pass

    # ── Helpers ──────────────────────────────────────────────────────────

    def _request_id(self) -> str:
        """Return a short unique ID for this request (for tracing)."""
        return uuid.uuid4().hex[:8]

    def _log_request(self, rid: str):
        """Log full request details with hex dump of body."""
        ct = self.headers.get("Content-Type", "(none)")
        cl = self.headers.get("Content-Length", "0")
        logger.info(
            f"[{rid}] {self.command} {self.path} "
            f"(Content-Type: {ct}, Content-Length: {cl})"
        )
        for h, v in self.headers.items():
            logger.debug(f"  [{rid}] Header: {h}: {v}")

    def _log_body(self, rid: str, body: bytes, label: str = "Body"):
        """Log body as hex + ASCII preview."""
        if not body:
            logger.info(f"  [{rid}] {label}: (empty)")
            return
        logger.info(f"  [{rid}] {label}: {len(body)} bytes")
        logger.info(f"  [{rid}] {label} (hex): {body[:256].hex()}")
        try:
            text = body.decode("utf-8", errors="replace")
            logger.info(f"  [{rid}] {label} (text): {text[:512]}")
        except Exception:
            logger.info(f"  [{rid}] {label} (text): <not utf-8>")
        if len(body) > 256:
            logger.info(f"  [{rid}]   ... ({len(body) - 256} more bytes)")

    def _read_body(self) -> bytes:
        """Read request body respecting Content-Length or Transfer-Encoding."""
        length = int(self.headers.get("Content-Length", 0))
        if length > 0:
            return self.rfile.read(length)
        # Chunked transfer encoding
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            chunks = []
            while True:
                line = self.rfile.readline().strip()
                if not line:
                    break
                chunk_size = int(line, 16)
                if chunk_size == 0:
                    break
                chunks.append(self.rfile.read(chunk_size))
                self.rfile.readline()  # trailing CRLF
            return b"".join(chunks)
        return b""

    def _send_json(self, status: int, data, rid: str = ""):
        """Send a JSON response."""
        body = json.dumps(data, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Request-Id", rid)
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, status: int, text: str, rid: str = "", extra_headers: list = None):
        """Send a plain text response."""
        body = text.encode("utf-8") if isinstance(text, str) else text
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Request-Id", rid)
        if extra_headers:
            for h, v in extra_headers:
                self.send_header(h, v)
        self.end_headers()
        self.wfile.write(body)

    # ── Echo handler (all methods) ───────────────────────────────────────

    def _handle_echo(self, method: str):
        """
        Universal echo endpoint.
        Echoes back: method, path, query, headers (as X-Echo-Header-*),
        and body. This is the primary endpoint for Full HTTP Client Mode testing.
        """
        rid = self._request_id()
        t0 = time.time()
        self._log_request(rid)

        body = self._read_body()
        self._log_body(rid, body)

        # Build echo response
        parsed = urlparse(self.path)
        echo_headers = {}
        for h, v in self.headers.items():
            # Forward client headers as X-Echo-Header-* response headers
            echo_headers[f"X-Echo-Header-{h}"] = v

        echo_body_lines = [
            f"REQUEST ID: {rid}",
            f"METHOD: {method}",
            f"PATH: {self.path}",
            f"QUERY: {parsed.query}",
            f"--- HEADERS ---",
        ]
        for h, v in self.headers.items():
            echo_body_lines.append(f"  {h}: {v}")
        echo_body_lines.append(f"--- BODY ({len(body)} bytes) ---")
        if body:
            echo_body_lines.append(body.decode("utf-8", errors="replace"))
        echo_body_lines.append(f"--- END ---")
        echo_body = "\r\n".join(echo_body_lines) + "\r\n"

        elapsed = (time.time() - t0) * 1000

        response_headers = [
            ("X-Request-Id", rid),
            ("X-Echo-Method", method),
            ("X-Echo-Path", self.path),
            ("X-Echo-Body-Size", str(len(body))),
            ("X-Echo-Time-Ms", f"{elapsed:.1f}"),
        ] + list(echo_headers.items())

        # Add some multi-value headers for testing
        response_headers.append(("X-Multi-Test", "value1"))
        response_headers.append(("X-Multi-Test", "value2"))

        logger.info(f"[{rid}] Echo response: {len(echo_body)} bytes in {elapsed:.1f}ms")
        self._send_text(200, echo_body, rid, response_headers)

    # ── HTTP method handlers ─────────────────────────────────────────────

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/debug/stored":
            self._do_debug_stored()
        elif parsed.path == "/debug/log":
            self._do_debug_log()
        elif self.path.startswith("/echo/status/"):
            self._do_echo_status("GET")
        elif self.path.startswith("/echo"):
            self._handle_echo("GET")
        elif self.path.startswith("/static/"):
            self._do_static()
        elif self.path in stored_data:
            self._do_return_stored()
        else:
            self._send_text(200, "OK: " + self.path, self._request_id())

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path.startswith("/echo"):
            self._handle_echo("POST")
        else:
            self._do_store_and_reply("POST")

    def do_PUT(self):
        parsed = urlparse(self.path)
        if parsed.path.startswith("/echo"):
            self._handle_echo("PUT")
        else:
            self._do_store_and_reply("PUT")

    def do_DELETE(self):
        rid = self._request_id()
        self._log_request(rid)
        body = self._read_body()
        self._log_body(rid, body, "Body (DELETE)")
        # Clear stored data at this path
        path = urlparse(self.path).path
        if path in stored_data:
            del stored_data[path]
            logger.info(f"[{rid}] Deleted stored data for {path}")
        # Echo the delete
        self._send_text(200, f"DELETE OK: {self.path}", rid)

    def do_HEAD(self):
        parsed = urlparse(self.path)
        rid = self._request_id()
        self._log_request(rid)

        if parsed.path.startswith("/echo"):
            self.send_response(200)
            self.send_header("X-Echo-Method", "HEAD")
            self.send_header("X-Request-Id", rid)
            self.end_headers()
        elif self.path.startswith("/static/"):
            content = b"HELLO FROM SERVER!"
            self.send_response(200)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("X-Static", "yes")
            self.end_headers()
        elif self.path in stored_data:
            data = stored_data[self.path]
            self.send_response(200)
            self.send_header("Content-Length", str(len(data.get("body", b""))))
            self.send_header("X-Exists", "yes")
            self.end_headers()
        else:
            self.send_response(404)
            self.send_header("X-Exists", "no")
            self.end_headers()

    # ── Specialized handlers ─────────────────────────────────────────────

    def _do_echo_status(self, method: str):
        """
        /echo/status/<code> — return the given status code with an explanatory body.
        Used for testing error handling (4xx, 5xx).
        """
        rid = self._request_id()
        self._log_request(rid)
        body = self._read_body()
        self._log_body(rid, body)

        path_parts = urlparse(self.path).path.split("/")
        try:
            status_code = int(path_parts[-1])
        except (ValueError, IndexError):
            status_code = 400

        status_text = {
            200: "OK", 201: "Created", 204: "No Content",
            301: "Moved Permanently", 302: "Found",
            400: "Bad Request", 401: "Unauthorized", 403: "Forbidden",
            404: "Not Found", 405: "Method Not Allowed",
            418: "I'm a Teapot",
            500: "Internal Server Error", 502: "Bad Gateway",
            503: "Service Unavailable",
        }.get(status_code, f"Status {status_code}")

        response_body = (
            f"REQUEST ID: {rid}\r\n"
            f"STATUS CODE: {status_code}\r\n"
            f"STATUS TEXT: {status_text}\r\n"
            f"--- END ---\r\n"
        )

        logger.info(f"[{rid}] Returning status {status_code} ({status_text})")
        extra = [("X-Request-Id", rid)]
        if body:
            extra.append(("X-Echo-Body-Size", str(len(body))))
        self._send_text(status_code, response_body, rid, extra)

    def _do_static(self):
        """Return static content for basic GET tests."""
        rid = self._request_id()
        self._log_request(rid)
        content = (
            "HELLO FROM SERVER! This is static content for C64 to read.\r\n"
            "Line 2 of static content.\r\n"
            "Line 3 - with trailing data.\r\n"
        ).encode("utf-8")
        logger.info(f"[{rid}] Returning static content ({len(content)} bytes)")
        self._send_text(
            200, content, rid,
            [("Content-Type", "text/plain"), ("X-Static", "yes")]
        )

    def _do_return_stored(self):
        """Return previously POSTed/PUT data."""
        rid = self._request_id()
        self._log_request(rid)
        data = stored_data[self.path]
        body = data.get("body", b"")
        logger.info(f"[{rid}] Returning stored data ({len(body)} bytes)")
        self._send_text(
            200, body, rid,
            [
                ("Content-Type", "application/octet-stream"),
                ("X-Stored-Path", self.path),
                ("X-Stored-Time", data.get("timestamp", "unknown")),
            ],
        )

    def _do_store_and_reply(self, method: str):
        """Store POST/PUT body, log it, return confirmation."""
        rid = self._request_id()
        t0 = time.time()
        self._log_request(rid)
        body = self._read_body()
        self._log_body(rid, body)

        timestamp = datetime.now().isoformat()
        stored_data[self.path] = {
            "body": body,
            "timestamp": timestamp,
            "headers": dict(self.headers),
            "method": method,
        }

        request_log.append({
            "time": timestamp,
            "rid": rid,
            "method": method,
            "path": self.path,
            "size": len(body),
            "body_preview": body[:64].hex() if body else None,
        })

        elapsed = (time.time() - t0) * 1000
        reply = (
            f"{method} received: {len(body)} bytes stored at {self.path}\r\n"
            f"Time: {elapsed:.1f}ms\r\n"
        ).encode("utf-8")

        logger.info(f"[{rid}] {method} stored, reply: {len(reply)} bytes in {elapsed:.1f}ms")
        self._send_text(
            200, reply, rid,
            [
                (f"X-{method}-Stored", "yes"),
                (f"X-{method}-Size", str(len(body))),
            ],
        )

    # ── Debug endpoints ──────────────────────────────────────────────────

    def _do_debug_stored(self):
        """List all stored paths."""
        paths = list(stored_data.keys())
        response = json.dumps({
            "count": len(paths),
            "paths": paths,
            "details": {
                p: {
                    "size": len(d.get("body", b"")),
                    "method": d.get("method", "?"),
                    "time": d.get("timestamp", "?"),
                }
                for p, d in stored_data.items()
            },
        }, indent=2)
        self._send_text(200, response, self._request_id(),
                        [("Content-Type", "application/json")])

    def _do_debug_log(self):
        """Show request history."""
        response = json.dumps(request_log[-100:] if len(request_log) > 100 else request_log, indent=2)
        self._send_text(200, response, self._request_id(),
                        [("Content-Type", "application/json")])


# ── Server ------------------------------------------------------------------

class VerboseTCPServer(socketserver.TCPServer):
    allow_reuse_address = True
    timeout = 30


def run_server(port=8080):
    """Run the test server."""
    try:
        local_ip = socket.gethostbyname(socket.gethostname())
    except Exception:
        local_ip = "127.0.0.1"

    print(f"""
╔═══════════════════════════════════════════════════════════════╗
║       Meatloaf HTTP Test Server — Full HTTP Client Mode      ║
╠═══════════════════════════════════════════════════════════════╣
║  Listening on http://{LISTEN_ADDR or '0.0.0.0'}:{port}                      ║
║  Local network: http://{local_ip}:{port}                        ║
║                                                               ║
║  Endpoints:                                                   ║
║    GET/POST/PUT/DELETE  /echo              — Echo endpoint    ║
║                         /echo/headers      — Headers-only      ║
║                         /echo/status/###   — Custom status     ║
║    GET /static/<path>                     — Static content     ║
║    POST/PUT <path>                        — Store data         ║
║    GET <path>                             — Return stored data ║
║                                                               ║
║  Debug:                                                       ║
║    GET /debug/stored                      — List stored data   ║
║    GET /debug/log                         — Request history    ║
║                                                               ║
║  Every request gets a unique X-Request-Id header.             ║
║  DEBUG logging shows hex dumps of all bodies.                 ║
║                                                               ║
║  Set env: MEATLOAF_TEST_LOG_LEVEL=DEBUG  for verbose output   ║
║  Set env: MEATLOAF_LISTEN_PORT=<port>    for custom port      ║
║                                                               ║
║  Press Ctrl+C to stop                                         ║
╚═══════════════════════════════════════════════════════════════╝
    """)

    with VerboseTCPServer((LISTEN_ADDR, port), MeatloafTestHandler) as httpd:
        print(f"Server listening on port {port}...")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server...")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else LISTEN_PORT
    run_server(port)
