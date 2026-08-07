#!/usr/bin/env python3
"""
Robust serial capture for Meatloaf ESP32 UART debug output.

Designed to survive:
  - ESP32 reboot / flash cycle (port disappears temporarily)
  - USB disconnect/reconnect
  - Port already in use (waits for it)
  - Any transient serial errors, including self-kill by pkill

Usage:
  # Start capturing (background via subprocess)
  serial_capture.py --port /dev/ttyUSB0 --baud 2000000

  # Health check (exit code 0 = running)
  serial_capture.py --check /tmp/serial_capture.pid

The daemon:
  - Opens serial port and reads continuously
  - Strips ANSI escape codes and control characters
  - Writes clean output to log file and stdout
  - Writes heartbeat every time data arrives
  - Auto-reconnects with exponential backoff on port loss
  - Sends ENTER on initial connect to activate the console
"""

import sys
import os
import time
import re
import signal
import argparse
import select
import errno

# ── Config ──────────────────────────────────────────────────────────────────

PORT_DEFAULT = os.environ.get("MEATLOAF_UPLOAD_PORT", "/dev/ttyUSB0")
BAUD_DEFAULT = int(os.environ.get("MEATLOAF_MONITOR_SPEED", "2000000"))
OUT_DEFAULT = "/tmp/meatloaf_serial.log"
PID_DEFAULT = "/tmp/serial_capture.pid"
HEARTBEAT_DEFAULT = "/tmp/serial_capture_heartbeat"
CMD_FIFO_DEFAULT = "/tmp/serial_capture_cmd"  # FIFO for sending commands

# ── Globals ─────────────────────────────────────────────────────────────────

_running = True


def _signal_handler(signum, frame):
    global _running
    _running = False


def check_health(pid_path: str) -> bool:
    """Check if capture is running via PID + heartbeat file."""
    if not os.path.exists(pid_path) or not os.path.exists(HEARTBEAT_DEFAULT):
        return False
    try:
        with open(pid_path) as f:
            pid = int(f.read().strip())
        os.kill(pid, 0)  # check if alive
        # Check heartbeat is fresh (< 30s old)
        hb_age = time.time() - os.path.getmtime(HEARTBEAT_DEFAULT)
        if hb_age > 30:
            return False
        return True
    except (ProcessLookupError, ValueError, OSError, FileNotFoundError):
        return False


def ensure_stale_stopped(pid_path: str):
    """Kill any existing capture process by PID only (avoids self-kill)."""
    my_pid = str(os.getpid())

    if not os.path.exists(pid_path):
        return

    try:
        with open(pid_path) as f:
            old_pid = f.read().strip()
        if not old_pid or old_pid == my_pid:
            return  # it's us or empty
        pid = int(old_pid)
        try:
            os.kill(pid, signal.SIGTERM)
            for _ in range(10):
                time.sleep(0.1)
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    break
            else:
                # Still alive — force kill
                try:
                    os.kill(pid, signal.SIGKILL)
                    time.sleep(0.2)
                except ProcessLookupError:
                    pass
        except (ProcessLookupError, OSError):
            pass
    except (ValueError, OSError):
        pass
    try:
        os.unlink(pid_path)
    except OSError:
        pass
    time.sleep(0.3)


def write_to_log(fd, text: str):
    """Write text to the log file descriptor."""
    try:
        os.write(fd, text.encode("utf-8"))
    except OSError:
        pass


def main():
    global _running

    parser = argparse.ArgumentParser(
        description="Robust serial capture for Meatloaf debug output"
    )
    parser.add_argument("--port", default=PORT_DEFAULT)
    parser.add_argument("--baud", type=int, default=BAUD_DEFAULT)
    parser.add_argument("--out", default=OUT_DEFAULT)
    parser.add_argument("--pid", default=PID_DEFAULT)
    parser.add_argument("--check", nargs="?", const=PID_DEFAULT, default=None,
                        help="Check if capture is running (exit 0=yes)")
    args = parser.parse_args()

    # Health check mode
    if args.check is not None:
        sys.exit(0 if check_health(args.check) else 1)

    # Ensure pyserial is available
    try:
        import serial as pyser
    except ImportError:
        print("Error: pyserial not installed. Install with: pip install pyserial")
        sys.exit(1)

    # Kill any stale capture using our pid file (by PID only)
    ensure_stale_stopped(args.pid)

    # Write PID (we are the daemon now)
    with open(args.pid, "w") as f:
        f.write(str(os.getpid()))

    # Signal handling for graceful shutdown
    signal.signal(signal.SIGTERM, _signal_handler)
    signal.signal(signal.SIGINT, _signal_handler)

    # Open output file (append mode)
    out_fd = os.open(args.out, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)

    # Compile strip patterns once
    ansi_re = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    ctrl_re = re.compile(r'[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]')

    # Status tracking
    status = "starting"
    consecutive_failures = 0
    max_backoff = 30  # max seconds between reconnect attempts
    ser = None  # pyserial Serial object

    # Write startup marker
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    write_to_log(out_fd, f"\n=== CAPTURE STARTED {ts} ===\n")

    # ── Main loop ────────────────────────────────────────────────────
    last_hb = 0.0

    # Create command FIFO so other tools can send commands through the daemon
    # (prevents serial port contention)
    cmd_fifo_path = os.environ.get("SERIAL_CAPTURE_CMD_FIFO", CMD_FIFO_DEFAULT)
    cmd_fifo_fd = None
    try:
        if os.path.exists(cmd_fifo_path):
            os.unlink(cmd_fifo_path)
        os.mkfifo(cmd_fifo_path, 0o600)
        # Open non-blocking so we don't block waiting for a writer
        cmd_fifo_fd = os.open(cmd_fifo_path, os.O_RDONLY | os.O_NONBLOCK)
        ts2 = time.strftime("%H:%M:%S")
        write_to_log(out_fd,
            f"[{ts2}] serial: Command FIFO at {cmd_fifo_path}\n")
    except OSError as e:
        ts2 = time.strftime("%H:%M:%S")
        write_to_log(out_fd,
            f"[{ts2}] serial: Could not create FIFO {cmd_fifo_path}: {e}\n")
        cmd_fifo_fd = None

    while _running:
        # Periodic heartbeat (every 5s, even without data — Meatloaf
        # only outputs when sent a command or during activity)
        now = time.time()
        if now - last_hb >= 5:
            try:
                with open(HEARTBEAT_DEFAULT, "w") as hb:
                    hb.write(str(now))
            except Exception:
                pass
            last_hb = now

        try:
            # --- Connect phase ---
            if ser is None or not ser.is_open:
                if status != "listening":
                    ts2 = time.strftime("%H:%M:%S")
                    write_to_log(out_fd,
                        f"[{ts2}] serial: Connecting to {args.port} at {args.baud} baud...\n")
                    status = "connecting"

                # Open with a timeout so we can check _running periodically
                ser = pyser.Serial(
                    port=args.port,
                    baudrate=args.baud,
                    timeout=0.1,
                    write_timeout=0.1,
                    exclusive=False,
                    rtscts=False,
                    dsrdtr=False,
                )
                consecutive_failures = 0

                ts2 = time.strftime("%Y-%m-%d %H:%M:%S")
                write_to_log(out_fd,
                    f"[{ts2}] serial: Connected to {args.port} at {args.baud} baud\n")
                status = "listening"

                # Activate the console with a newline
                ser.write(b"\n")
                time.sleep(0.2)
                ser.reset_input_buffer()

            # --- Read phase ---
            try:
                data = ser.read(4096)
            except pyser.SerialException as e:
                ts2 = time.strftime("%H:%M:%S")
                write_to_log(out_fd,
                    f"[{ts2}] serial: Port error: {e}. Reconnecting...\n")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                consecutive_failures += 1
                backoff = min(2 ** consecutive_failures, max_backoff)
                status = "disconnected"
                for _ in range(int(backoff * 10)):
                    if not _running:
                        break
                    time.sleep(0.1)
                continue

            if data:
                # Decode, strip, write
                try:
                    text = data.decode("utf-8", errors="replace")
                except Exception:
                    text = data.decode("latin-1", errors="replace")

                cleaned = ansi_re.sub("", text)
                cleaned = ctrl_re.sub("", cleaned)

                if cleaned:
                    write_to_log(out_fd, cleaned)
                    sys.stdout.write(cleaned)
                    sys.stdout.flush()

            # --- Check for commands from FIFO ---
            if cmd_fifo_fd is not None:
                try:
                    cmd_data = os.read(cmd_fifo_fd, 4096)
                    if cmd_data:
                        cmd_text = cmd_data.decode("utf-8", errors="replace").strip()
                        if cmd_text and ser and ser.is_open:
                            # Echo to log
                            ts2 = time.strftime("%H:%M:%S")
                            write_to_log(out_fd,
                                f"[{ts2}] > {cmd_text}\n")
                            # Send to Meatloaf
                            ser.write((cmd_text + "\n").encode("utf-8"))
                except OSError as e:
                    if e.errno != errno.EAGAIN:
                        pass  # expected when no data

        except pyser.SerialException as e:
            # Outer catch for open failures
            ts2 = time.strftime("%H:%M:%S")
            write_to_log(out_fd,
                f"[{ts2}] serial: Open error: {e}. Retrying...\n")
            if ser:
                try:
                    ser.close()
                except Exception:
                    pass
            ser = None
            consecutive_failures += 1
            backoff = min(2 ** consecutive_failures, max_backoff)
            status = "disconnected"
            for _ in range(int(backoff * 10)):
                if not _running:
                    break
                time.sleep(0.1)

        except Exception as e:
            ts2 = time.strftime("%H:%M:%S")
            write_to_log(out_fd,
                f"[{ts2}] serial: Unexpected error: {e}. Retrying...\n")
            if ser:
                try:
                    ser.close()
                except Exception:
                    pass
            ser = None
            consecutive_failures += 1
            backoff = min(2 ** consecutive_failures, max_backoff)
            status = "disconnected"
            time.sleep(backoff)

    # ── Clean shutdown ──────────────────────────────────────────────────
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    write_to_log(out_fd, f"=== CAPTURE STOPPED {ts} ===\n")

    if ser and ser.is_open:
        try:
            ser.close()
        except Exception:
            pass

    os.close(out_fd)

    # Clean up heartbeat and FIFO
    try:
        os.unlink(HEARTBEAT_DEFAULT)
    except OSError:
        pass
    try:
        if cmd_fifo_fd is not None:
            os.close(cmd_fifo_fd)
        os.unlink(cmd_fifo_path)
    except OSError:
        pass

    sys.exit(0)


if __name__ == "__main__":
    main()
