#!/usr/bin/env python3
"""
C64 Meatloaf Full HTTP Client Mode Test Client

Simulates what C64 BASIC does in Full HTTP Client Mode and verifies
the server responds correctly. This tests the server side — run against
test_server.py.

For real end-to-end testing, run these flows ON the C64 with
http_full_client_test.bas.

Meatloaf Full Mode protocol:
  OPEN ...  → PRINT#1,"m <method>" → PRINT#1,"h <name>: <value>" → ...
  → PRINT#1,"b <body>" → PRINT#1,"s" (send) → PRINT#1,"status" (read status)
  → PRINT#1,"r-h" (read headers) → GET#1 (per header line)
  → PRINT#1,"r-b" (read body) → GET#1 (per byte or block)
  → PRINT#1,"c" (clear for next request)

Each server response includes X-Request-Id header for tracing through
the C64 → Meatloaf → Server pipeline.
"""

import requests
import time
import sys
import uuid
from datetime import datetime

SERVER = "http://localhost:8080"
TIMEOUT = 10

passed = 0
failed = 0


def log(msg: str = ""):
    """Print a log line with timestamp."""
    print(f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {msg}")


def section(title: str):
    """Print a section header."""
    print()
    print("=" * 70)
    print(f"  {title}")
    print("=" * 70)


def test(name: str, condition: bool, detail: str = ""):
    """Record a test result."""
    global passed, failed
    if condition:
        print(f"  ✓ PASS: {name}")
        passed += 1
    else:
        print(f"  ✗ FAIL: {name}" + (f" — {detail}" if detail else ""))
        failed += 1


def check_response_headers(resp: requests.Response, expected_headers: dict):
    """Check that expected headers are present in response."""
    for h, expected_val in expected_headers.items():
        h_normalized = h.lower()
        val = resp.headers.get(h_normalized)
        if val is None:
            # Check case-insensitive
            val = resp.headers.get(h, "")
        test(f"Response header {h}", val is not None and expected_val in str(val),
             f"expected '{expected_val}' in '{val}'")


def request_id() -> str:
    return uuid.uuid4().hex[:8]


# ══════════════════════════════════════════════════════════════════════════
# SERVER HEALTH CHECK
# ══════════════════════════════════════════════════════════════════════════

def check_server_status():
    """Check if test server is running."""
    try:
        resp = requests.get(f"{SERVER}/debug/stored", timeout=2)
        log(f"Server status: ONLINE (returned {resp.status_code})")
        return True
    except requests.exceptions.ConnectionError:
        log("Server status: OFFLINE")
        log(f"  Start the server with: python3 test_server.py")
        return False


# ══════════════════════════════════════════════════════════════════════════
# TEST: C64 WRITE -> READ simulation (original, pre-full-mode)
# ══════════════════════════════════════════════════════════════════════════

def test_c64_write_then_read():
    """Simulate C64 WRITE then READ pattern (simple mode)."""
    section("C64 WRITE -> READ Simulation (Simple Mode)")

    path = f"/test_c64_write_{request_id()}"
    write_data = b"HELLO FROM C64"

    # Step 1-3: Simulate C64 WRITE (PRINT#)
    log("[WRITE PHASE] Sending POST request like C64 PRINT#...")
    post_resp = requests.post(f"{SERVER}{path}", data=write_data, timeout=TIMEOUT)
    test("POST response status 200", post_resp.status_code == 200,
         f"got {post_resp.status_code}")
    test("POST has X-Request-Id", "x-request-id" in post_resp.headers)

    # Step 4-6: Simulate C64 READ (GET#)
    log("[READ PHASE] Sending GET request like C64 GET#...")
    get_resp = requests.get(f"{SERVER}{path}", timeout=TIMEOUT)
    test("GET response status 200", get_resp.status_code == 200)
    test("GET returns POSTed data", get_resp.content == write_data,
         f"expected {write_data!r}, got {get_resp.content!r}")

    log("  POST body (hex): " + post_resp.content.hex())
    log("  GET  body (hex): " + get_resp.content.hex())


# ══════════════════════════════════════════════════════════════════════════
# TEST: Basic GET to /echo
# ══════════════════════════════════════════════════════════════════════════

def test_echo_get():
    """Simulate C64: m GET → s → r-h → r-b on /echo."""
    section("Test 1: GET /echo (Basic GET)")

    rid = request_id()
    resp = requests.get(f"{SERVER}/echo?rid={rid}", timeout=TIMEOUT)

    test("Status 200", resp.status_code == 200)
    test("X-Request-Id present", "x-request-id" in resp.headers)
    test("X-Echo-Method is GET",
         resp.headers.get("x-echo-method", "").upper() == "GET")
    check_response_headers(resp, {
        "X-Echo-Path": "/echo",
        "X-Echo-Body-Size": "0",
    })

    body = resp.text
    test("Body mentions METHOD: GET", "METHOD: GET" in body)
    test("Body mentions PATH", "PATH: /echo" in body)
    test("Body has --- END --- marker", "--- END ---" in body)

    log(f"  Response body ({len(body)} bytes):")
    for line in body.split("\r\n")[:10]:
        log(f"    {line}")


# ══════════════════════════════════════════════════════════════════════════
# TEST: POST with JSON body
# ══════════════════════════════════════════════════════════════════════════

def test_echo_post_with_body():
    """Simulate C64: m POST → h Content-Type → b {json} → s → r-h → r-b."""
    section("Test 2: POST /echo with JSON Body")

    payload = '{"key":"value","number":42}'
    resp = requests.post(
        f"{SERVER}/echo",
        data=payload.encode("utf-8"),
        headers={"Content-Type": "application/json"},
        timeout=TIMEOUT,
    )

    test("Status 200", resp.status_code == 200)
    test("X-Echo-Method is POST",
         resp.headers.get("x-echo-method", "").upper() == "POST")

    body = resp.text
    test("Body echoes back payload", payload in body)
    test("Body shows METHOD: POST", "METHOD: POST" in body)
    test("Body shows Content-Type header", "Content-Type" in body)
    test("Body shows BODY size", f"({len(payload)} bytes)" in body)

    log(f"  POST body echoed correctly ({len(body)} bytes)")


# ══════════════════════════════════════════════════════════════════════════
# TEST: PUT method
# ══════════════════════════════════════════════════════════════════════════

def test_echo_put():
    """Simulate C64: m PUT → b data → s."""
    section("Test 3: PUT /echo")

    payload = "This is a PUT request"
    resp = requests.put(
        f"{SERVER}/echo",
        data=payload.encode("utf-8"),
        headers={"Content-Type": "text/plain"},
        timeout=TIMEOUT,
    )

    test("Status 200", resp.status_code == 200)
    test("X-Echo-Method is PUT",
         resp.headers.get("x-echo-method", "").upper() == "PUT")
    test("Body shows METHOD: PUT", "METHOD: PUT" in resp.text)
    test("Body shows PUT body", payload in resp.text)


# ══════════════════════════════════════════════════════════════════════════
# TEST: Custom headers + multi-value headers
# ══════════════════════════════════════════════════════════════════════════

def test_custom_headers():
    """Simulate C64: m GET → h Authorization → h+ X-Custom → s."""
    section("Test 4: Custom Headers + Multi-Value Headers")

    resp = requests.get(
        f"{SERVER}/echo",
        headers={
            "Authorization": "Bearer test123",
            "X-Custom": "first",
        },
        timeout=TIMEOUT,
    )

    test("Status 200", resp.status_code == 200)

    body = resp.text
    # The echo endpoint echoes request headers in the body
    test("Body echoes Authorization header",
         "Authorization: Bearer test123" in body or
         "authorization: Bearer test123" in body)
    test("Body echoes X-Custom header",
         "X-Custom" in body and "first" in body)

    # Server also returns X-Multi-Test x2 (multi-value response headers)
    # Check how the requests library handles multi-value headers
    multi_test = resp.raw.headers.getlist("X-Multi-Test") if hasattr(resp.raw.headers, "getlist") else None
    test("Server sends X-Multi-Test header",
         "x-multi-test" in resp.headers)

    log(f"  Response body: {body[:300]}...")


# ══════════════════════════════════════════════════════════════════════════
# TEST: Response headers (X-echo-*)
# ══════════════════════════════════════════════════════════════════════════

def test_response_headers():
    """Verify response headers contain echo metadata."""
    section("Test 5: Response Headers Verification")

    resp = requests.get(f"{SERVER}/echo/headers", timeout=TIMEOUT)

    test("Status 200", resp.status_code == 200)
    # Response headers
    expected_resp_headers = ["x-request-id", "content-type", "content-length"]
    for h in expected_resp_headers:
        test(f"Response contains {h}", h in resp.headers,
             f"headers: {list(resp.headers.keys())}")

    # Specific echo headers
    test("X-Echo method", "x-echo-method" in resp.headers)
    test("X-Echo path", "x-echo-path" in resp.headers)

    log(f"  Response headers: {dict(resp.headers)}")


# ══════════════════════════════════════════════════════════════════════════
# TEST: Status codes via /echo/status/NNN
# ══════════════════════════════════════════════════════════════════════════

def test_status_codes():
    """Test various HTTP status codes via /echo/status/<code>."""
    section("Test 6: Status Codes via /echo/status/NNN")

    for code in [200, 201, 301, 400, 404, 418, 500, 503]:
        try:
            resp = requests.get(f"{SERVER}/echo/status/{code}", timeout=TIMEOUT)
            test(f"Status {code} → response is {resp.status_code}",
                 resp.status_code == code,
                 f"got {resp.status_code}")
            test(f"Status {code} has X-Request-Id", "x-request-id" in resp.headers)
            # Body should mention the code
            body_has_code = str(code) in resp.text
            test(f"Status {code} body mentions it", body_has_code,
                 f"body: {resp.text[:100]!r}")
        except Exception as e:
            test(f"Status {code} request succeeded", False, str(e))

    # 404 specifically — body should say "404" or "Not Found"
    resp404 = requests.get(f"{SERVER}/echo/status/404", timeout=TIMEOUT)
    test("404 response body mentions 404", "404" in resp404.text)
    test("404 response has X-Request-Id", "x-request-id" in resp404.headers)


# ══════════════════════════════════════════════════════════════════════════
# TEST: HEAD request
# ══════════════════════════════════════════════════════════════════════════

def test_head_request():
    """Simulate C64: m HEAD → s."""
    section("Test 7: HEAD Request")

    resp = requests.head(f"{SERVER}/echo", timeout=TIMEOUT,
                         headers={"X-Test": "head-request"})
    test("HEAD response status 200", resp.status_code == 200)
    test("HEAD has X-Request-Id", "x-request-id" in resp.headers)
    test("HEAD has X-Echo-Method",
         resp.headers.get("x-echo-method", "").upper() == "HEAD")
    test("HEAD has no body", len(resp.content) == 0)

    log(f"  HEAD headers: {dict(resp.headers)}")


# ══════════════════════════════════════════════════════════════════════════
# TEST: Multi-request cycle (send → clear → send again)
# ══════════════════════════════════════════════════════════════════════════

def test_multi_request_cycle():
    """
    Simulate C64 multi-request cycle:
      open → m POST → b first → s → r-b → c → m PUT → b second → s → r-b
    """
    section("Test 8: Multi-Request Cycle Simulation")

    rid1 = request_id()
    rid2 = request_id()

    # Request 1: POST
    log("Request 1: POST")
    resp1 = requests.post(
        f"{SERVER}/echo?rid={rid1}",
        data="first request".encode("utf-8"),
        headers={"Content-Type": "text/plain"},
        timeout=TIMEOUT,
    )
    test("Req1 status 200", resp1.status_code == 200)
    test("Req1 echo method is POST",
         resp1.headers.get("x-echo-method", "").upper() == "POST")
    test("Req1 body contains 'first request'", "first request" in resp1.text)

    # Request 2: PUT (simulates c → m put → b second)
    log("Request 2: PUT (after clear)")
    resp2 = requests.put(
        f"{SERVER}/echo?rid={rid2}",
        data="second request".encode("utf-8"),
        headers={"Content-Type": "text/plain"},
        timeout=TIMEOUT,
    )
    test("Req2 status 200", resp2.status_code == 200)
    test("Req2 echo method is PUT",
         resp2.headers.get("x-echo-method", "").upper() == "PUT")
    test("Req2 body contains 'second request'", "second request" in resp2.text)


# ══════════════════════════════════════════════════════════════════════════
# TEST: B+ append body simulation
# ══════════════════════════════════════════════════════════════════════════

def test_body_append():
    """
    Simulate C64: b HELLO → b+ WORLD → b+ !!! → s
    On meatloaf, b+ appends to body. On the server, the body is
    received as a single POST body.
    """
    section("Test 9: Body Append (B+ simulation)")

    # The C64 would do: PRINT#1,"b HELLO" + PRINT#1,"b+ WORLD" + PRINT#1,"b+ !!!"
    # Meatloaf concatenates: "HELLOWORLD!!!"
    assembled_body = "HELLOWORLD!!!"
    resp = requests.post(
        f"{SERVER}/echo",
        data=assembled_body.encode("utf-8"),
        headers={"Content-Type": "text/plain"},
        timeout=TIMEOUT,
    )
    test("Status 200", resp.status_code == 200)
    test("Body echoes assembled text", assembled_body in resp.text)
    test("Body size matches", f"({len(assembled_body)} bytes)" in resp.text)

    log(f"  Sent: {assembled_body!r}")
    log(f"  Echo body preview: {resp.text[:200]}")


# ══════════════════════════════════════════════════════════════════════════
# TEST: Large body
# ══════════════════════════════════════════════════════════════════════════

def test_large_body():
    """Test sending/receiving a larger body (multi-line)."""
    section("Test 10: Large Body (multi-line)")

    lines = [f"Line {i}: The quick brown fox jumps over the lazy dog." for i in range(1, 21)]
    large_body = "\n".join(lines)

    resp = requests.post(
        f"{SERVER}/echo",
        data=large_body.encode("utf-8"),
        headers={"Content-Type": "text/plain"},
        timeout=TIMEOUT,
    )
    test("Status 200", resp.status_code == 200)
    for l in lines[:3]:
        test(f"Body contains '{l[:30]}...'", l in resp.text)

    test("Body length correct", str(len(large_body)) in resp.text or
         resp.headers.get("x-echo-body-size") is not None)
    log(f"  Sent {len(large_body)} bytes, received {len(resp.text)} bytes")


# ══════════════════════════════════════════════════════════════════════════
# TEST: Debug endpoints
# ══════════════════════════════════════════════════════════════════════════

def test_debug_endpoints():
    """Verify debug endpoints work."""
    section("Test 11: Debug Endpoints")

    # Check stored data
    resp = requests.get(f"{SERVER}/debug/stored", timeout=TIMEOUT)
    test("/debug/stored status 200", resp.status_code == 200)
    test("/debug/stored returns JSON",
         "content-type" in resp.headers and "json" in resp.headers["content-type"])
    test("/debug/stored has valid body",
         "count" in resp.text and "paths" in resp.text)

    # Check request log (should have data from previous tests)
    resp_log = requests.get(f"{SERVER}/debug/log", timeout=TIMEOUT)
    test("/debug/log status 200", resp_log.status_code == 200)
    test("/debug/log returns JSON", "json" in resp_log.headers.get("content-type", ""))
    test("/debug/log has entries", len(resp_log.json()) > 0)

    log(f"  Stored: {resp.json()}")
    log(f"  Log entries: {len(resp_log.json())}")


# ══════════════════════════════════════════════════════════════════════════
# RUNNER
# ══════════════════════════════════════════════════════════════════════════

def run_all_tests():
    """Run all test scenarios."""
    global passed, failed
    passed = 0
    failed = 0

    if not check_server_status():
        sys.exit(1)

    time.sleep(0.2)

    print()
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║   Full HTTP Client Mode — Python Test Suite                ║")
    print("╚══════════════════════════════════════════════════════════════╝")

    test_c64_write_then_read()
    test_echo_get()
    test_echo_post_with_body()
    test_echo_put()
    test_custom_headers()
    test_response_headers()
    test_status_codes()
    test_head_request()
    test_multi_request_cycle()
    test_body_append()
    test_large_body()
    test_debug_endpoints()

    # Summary
    total = passed + failed
    print()
    print("=" * 70)
    print(f"  RESULTS: {passed}/{total} passed, {failed} failed")
    if failed == 0:
        print("  ✓ ALL TESTS PASSED — server is ready for full HTTP client mode")
    else:
        print("  ✗ SOME TESTS FAILED — check server and output above")
    print("=" * 70)

    return failed == 0


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        sys.exit(0 if check_server_status() else 1)

    success = run_all_tests()
    sys.exit(0 if success else 1)
