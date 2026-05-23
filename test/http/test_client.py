#!/usr/bin/env python3
"""
C64 Meatloaf HTTP Write/Read Test Client

This simulates what C64 BASIC does:
1. OPEN 1,8,1,"HTTP://test.server:8080/test"  (open for writing)
2. PRINT#1,"HELLO WORLD"                       (write data)
3. CLOSE 1                                      (close channel)
4. OPEN 1,8,0,"HTTP://test.server:8080/test"  (open for reading)
5. GET#1,A$                                    (read data)
6. CLOSE 1

Expected: Step 5 should read data from SERVER
Bug:      Step 5 reads what was written in step 2 (local echo)
"""

import requests
import time

SERVER = "http://localhost:8080"
TEST_PATH = "/test_c64_write"

def test_c64_write_then_read():
    """Simulate C64 WRITE then READ pattern."""
    print("=" * 60)
    print("C64 WRITE->READ Test")
    print("=" * 60)

    # Clean up any previous test data
    requests.delete(f"{SERVER}{TEST_PATH}")
    time.sleep(0.1)

    # Step 1-3: Simulate C64 WRITE
    print("\n[WRITE PHASE] Sending POST request like C64 PRINT#...")
    write_data = b"HELLO FROM C64"
    post_resp = requests.post(f"{SERVER}{TEST_PATH}", data=write_data)
    print(f"  POST response: {post_resp.status_code}")
    print(f"  POST response headers: {dict(post_resp.headers)}")
    print(f"  POST response body: {post_resp.text}")

    # Step 4-6: Simulate C64 READ
    print("\n[READ PHASE] Sending GET request like C64 INPUT#/GET#...")
    get_resp = requests.get(f"{SERVER}{TEST_PATH}")
    print(f"  GET response: {get_resp.status_code}")
    print(f"  GET response headers: {dict(get_resp.headers)}")
    print(f"  GET response body: {get_resp.text}")
    print(f"  GET response body (hex): {get_resp.content.hex()}")

    # Verify
    print("\n[VERIFICATION]")
    if get_resp.content == write_data:
        print("  ✓ PASS: Server returned the data that was POSTed")
        print("  This means Meatloaf is correctly forwarding data from C64 to server")
    else:
        print("  ✗ FAIL: Server returned different data")
        print(f"    Expected: {write_data}")
        print(f"    Got:      {get_resp.content}")

    return get_resp.content == write_data


def test_echo_detection():
    """
    Check if server returns local echo (bug indicator).

    A bug would cause GET to return what was written,
    even though server didn't send it.
    """
    print("\n" + "=" * 60)
    print("Echo Detection Test")
    print("=" * 60)

    # Clear previous data
    requests.delete(f"{SERVER}{TEST_PATH}")
    time.sleep(0.1)

    # Write something
    test_data = b"UNIQUE_TEST_DATA_12345"
    requests.post(f"{SERVER}{TEST_PATH}", data=test_data)

    # Get from server - should be the same
    resp = requests.get(f"{SERVER}{TEST_PATH}")

    if resp.content == test_data:
        print("  Server correctly returned posted data")
        print("  (This is GOOD - server is working as expected)")
        return True
    else:
        print("  Server returned different data")
        print(f"    Posted: {test_data}")
        print(f"    Got:    {resp.content}")
        return False


def check_server_status():
    """Check if test server is running."""
    try:
        resp = requests.get(f"{SERVER}/debug/stored", timeout=2)
        print(f"Server status: ONLINE (returned {resp.status_code})")
        return True
    except requests.exceptions.ConnectionError:
        print("Server status: OFFLINE")
        print(f"  Start the server with: python3 test_server.py")
        return False


if __name__ == '__main__':
    import sys

    if len(sys.argv) > 1 and sys.argv[1] == '--status':
        check_server_status()
    else:
        if not check_server_status():
            sys.exit(1)

        print("\nStarting tests...\n")

        result1 = test_c64_write_then_read()
        print()
        result2 = test_echo_detection()

        print("\n" + "=" * 60)
        print("SUMMARY")
        print("=" * 60)
        print(f"  Write->Read test: {'PASS' if result1 else 'FAIL'}")
        print(f"  Echo detection:   {'PASS' if result2 else 'FAIL'}")

        if result1 and result2:
            print("\n  ✓ All tests passed - server is working correctly")
            print("  If C64 still shows echo, the bug is in Meatloaf")
        else:
            print("\n  ✗ Tests failed - check server configuration")