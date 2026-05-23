#!/usr/bin/env python3
"""
Test HTTP server for Meatloaf HTTP write/READ verification.

This server:
1. Accepts POST requests and stores the body
2. Returns the stored POST data on GET requests
3. Logs all requests/responses for debugging
4. Supports chunked transfer encoding
"""

import http.server
import socketserver
import json
import logging
import time
from urllib.parse import urlparse, parse_qs
from datetime import datetime

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(__name__)

# In-memory storage for posted data, keyed by path
stored_data: dict[str, dict] = {}

# Track all requests for debugging
request_log = []

class MeatloafTestHandler(http.server.BaseHTTPRequestHandler):
    """HTTP handler that stores POST data and returns it on GET."""

    def log_message(self, format, *args):
        """Override to use our logger."""
        logger.info(f"{self.client_address[0]}:{self.client_address[1]} - {format % args}")

    def log_request_details(self):
        """Log detailed info about the request."""
        parsed = urlparse(self.path)
        logger.info(f"  Path: {self.path}")
        logger.info(f"  Method: {self.command}")
        logger.info(f"  Headers:")
        for header, value in self.headers.items():
            logger.info(f"    {header}: {value}")

    def do_GET(self):
        """Handle GET requests - return stored POST data, static content, or default message."""
        self.log_request_details()

        parsed = urlparse(self.path)

        # Static content endpoint - always returns predefined content
        if self.path.startswith('/static/'):
            static_content = b"HELLO FROM SERVER! This is static content for C64 to read."
            logger.info(f"  Returning static content ({len(static_content)} bytes)")

            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', str(len(static_content)))
            self.send_header('X-Static', 'yes')
            self.end_headers()
            self.wfile.write(static_content)
            return

        # Check if there's stored data for this path
        if self.path in stored_data:
            data = stored_data[self.path]
            body = data.get('body', b'')
            logger.info(f"  Returning stored data ({len(body)} bytes)")

            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('X-Stored-Path', self.path)
            self.send_header('X-Stored-Time', data.get('timestamp', 'unknown'))
            self.end_headers()
            self.wfile.write(body)
        else:
            # No stored data - return default response
            response = b"No POST data stored for this path yet."
            logger.info(f"  No stored data, returning default response")

            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', str(len(response)))
            self.end_headers()
            self.wfile.write(response)

    def do_POST(self):
        """Handle POST requests - store the body."""
        self.log_request_details()

        # Read Content-Length
        content_length = int(self.headers.get('Content-Length', 0))
        logger.info(f"  Content-Length: {content_length}")

        # Read the body
        body = self.rfile.read(content_length) if content_length > 0 else b''
        logger.info(f"  Body received ({len(body)} bytes): {body[:100]}...")
        if len(body) > 100:
            logger.info(f"  Body preview (hex): {body[:50].hex()}")

        # Store the data
        timestamp = datetime.now().isoformat()
        stored_data[self.path] = {
            'body': body,
            'timestamp': timestamp,
            'headers': dict(self.headers)
        }

        # Log to request history
        request_log.append({
            'time': timestamp,
            'method': 'POST',
            'path': self.path,
            'size': len(body),
            'body_preview': body[:50].hex() if body else None
        })

        # Send response
        response = f"POST received: {len(body)} bytes stored at {self.path}".encode()
        logger.info(f"  Sending response: {response.decode()}")

        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(response)))
        self.send_header('X-Post-Stored', 'yes')
        self.send_header('X-Post-Size', str(len(body)))
        self.end_headers()
        self.wfile.write(response)

    def do_HEAD(self):
        """Handle HEAD requests."""
        self.log_request_details()

        # Handle HEAD for static content
        if self.path.startswith('/static/'):
            static_content = b"HELLO FROM SERVER! This is static content for C64 to read."
            self.send_response(200)
            self.send_header('Content-Length', str(len(static_content)))
            self.send_header('X-Static', 'yes')
            self.end_headers()
            return

        # Check if data exists
        if self.path in stored_data:
            data = stored_data[self.path]
            self.send_response(200)
            self.send_header('Content-Length', str(len(data.get('body', b''))))
            self.send_header('X-Exists', 'yes')
        else:
            self.send_response(404)
            self.send_header('X-Exists', 'no')

        self.end_headers()

    def do_PUT(self):
        """Handle PUT requests - store the body and echo it back in the response."""
        self.log_request_details()

        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else b''
        logger.info(f"  PUT Body ({len(body)} bytes): {body[:100]}...")

        # Store PUT data under a different key to distinguish from POST
        put_key = self.path + "?method=PUT"
        stored_data[put_key] = {
            'body': body,
            'timestamp': datetime.now().isoformat(),
            'method': 'PUT'
        }
        # Also store at the regular path (last write wins)
        stored_data[self.path] = {
            'body': body,
            'timestamp': datetime.now().isoformat(),
            'method': 'PUT'
        }

        # Echo back what was PUT
        echo_response = f"PUT OK: {len(body)} bytes stored at {self.path}".encode()
        logger.info(f"  PUT stored, echoing response")

        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(echo_response)))
        self.send_header('X-Put-Stored', 'yes')
        self.send_header('X-Put-Size', str(len(body)))
        self.end_headers()
        self.wfile.write(echo_response)


class VerboseTCPServer(socketserver.TCPServer):
    """TCP server with timeout and address reuse."""
    allow_reuse_address = True
    timeout = 30


def run_server(port=8080):
    """Run the test server."""
    print(f"""
╔════════════════════════════════════════════════════════════╗
║         Meatloaf HTTP Test Server                          ║
╠════════════════════════════════════════════════════════════╣
║  Server running at http://localhost:{port}                    ║
║                                                            ║
║  Endpoints:                                                ║
║    GET  /static/...  - Returns static test content        ║
║    POST /path        - Store data, returns confirmation    ║
║    GET  /path        - Return stored data (if any)        ║
║    PUT  /path        - Store data, echo confirmation      ║
║                                                            ║
║  Debug endpoints:                                          ║
║    GET /debug/stored   - List all stored paths            ║
║    GET /debug/log      - Show request history              ║
║                                                            ║
║  Press Ctrl+C to stop                                      ║
╚════════════════════════════════════════════════════════════╝
    """)

    # Add debug endpoints to handler
    def do_GET_debug_stored(self):
        """List all stored paths."""
        paths = list(stored_data.keys())
        response = json.dumps({
            'count': len(paths),
            'paths': paths
        }, indent=2)
        response_bytes = response.encode()

        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(response_bytes)))
        self.end_headers()
        self.wfile.write(response_bytes)

    def do_GET_debug_log(self):
        """Show request history."""
        response = json.dumps(request_log, indent=2)
        response_bytes = response.encode()

        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(response_bytes)))
        self.end_headers()
        self.wfile.write(response_bytes)

    # Patch handler for debug endpoints
    MeatloafTestHandler.do_GET_debug_stored = do_GET_debug_stored
    MeatloafTestHandler.do_GET_debug_log = do_GET_debug_log

    # Override do_GET to handle debug paths
    original_do_GET = MeatloafTestHandler.do_GET

    def patched_do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == '/debug/stored':
            do_GET_debug_stored(self)
        elif parsed.path == '/debug/log':
            do_GET_debug_log(self)
        else:
            original_do_GET(self)

    MeatloafTestHandler.do_GET = patched_do_GET

    with VerboseTCPServer(("", port), MeatloafTestHandler) as httpd:
        print(f"Server listening on port {port}...")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server...")


if __name__ == '__main__':
    import sys
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    run_server(port)