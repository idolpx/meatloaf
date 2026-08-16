#!/usr/bin/env python3
"""
Meatloaf C64 Debug Automation — full unattended debug cycle.

Provides:
  - Ultimate 64 / II+ REST API control (BASIC injection, screen reading, keys)
  - Meatloaf firmware build + flash
  - Robust serial capture management (auto-reconnect, health checks)
  - Echo server management
  - Full "modify → deploy → run → read → analyze → fix" cycle
"""

import os
import sys
import time
import struct
import json
import re
import subprocess
import signal
from pathlib import Path
from typing import Optional

try:
    import requests
except ImportError:
    requests = None  # type: ignore

# ── Build settings (platformio.ini is the source of truth) ────────────

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pio_config  # noqa: E402

SKILL_DIR = Path(os.environ.get(
    "MEATLOAF_DEBUG_SKILL_DIR",
    Path(__file__).resolve().parents[1],
))

# Everything below comes from platformio.ini; MEATLOAF_* env vars override
# it for one-off runs (e.g. a second board on a different port).
PIO = pio_config.load()

MEATLOAF_DIR = Path(os.environ.get("MEATLOAF_DIR") or PIO["project_root"] or ".")
U64_IP_ADDRESS = (os.environ.get("U64_IP_ADDRESS")
                  or PIO["u64_ip_address"] or "192.168.1.176")
U64_PASSWORD = os.environ.get("U64_PASSWORD", "")
UPLOAD_PORT = os.environ.get("MEATLOAF_UPLOAD_PORT") or PIO["upload_port"]
SERIAL_PORT = (os.environ.get("MEATLOAF_UPLOAD_PORT")
               or PIO["monitor_port"] or PIO["upload_port"])
MONITOR_SPEED = os.environ.get("MEATLOAF_MONITOR_SPEED") or PIO["monitor_speed"]
BUILD_ENV = os.environ.get("MEATLOAF_BUILD_ENV") or PIO["environment"]

# ── C64 memory locations ─────────────────────────────────────────────

KB_BUF_ADDR = "0277"       # Keyboard buffer (10-byte FIFO)
KB_CNT_ADDR = "00C6"       # Keyboard buffer count
SCREEN_ADDR = "0400"       # Screen memory (40×25 = 1000 bytes)
SCREEN_SIZE = 1000
BASIC_ADDR = 0x0801        # BASIC program start
VAR_PTR_ADDR = "002D"      # Variable pointer (2 bytes, LE)

# ── Serial capture paths ──────────────────────────────────────────────

SERIAL_SCRIPT = SKILL_DIR / "scripts" / "serial_capture.py"
SERIAL_LOG = Path("/tmp/meatloaf_serial.log")
SERIAL_PID = Path("/tmp/serial_capture.pid")
SERIAL_HEARTBEAT = Path("/tmp/serial_capture_heartbeat")

# ── Echo server paths ─────────────────────────────────────────────────

ECHO_SERVER_DIR = MEATLOAF_DIR / "test" / "http"
ECHO_SERVER_SCRIPT = ECHO_SERVER_DIR / "test_server.py"
ECHO_SERVER_PID = Path("/tmp/meatloaf_echo_server.pid")


# ═══════════════════════════════════════════════════════════════════════
# bc64 Compiler — the ONLY way to compile BASIC
# ═══════════════════════════════════════════════════════════════════════

BC64_PATH = os.environ.get("BC64_PATH", "bc64")

def compile_bc64(source_text: str, *, aliases: bool = True,
                  temp_dir: str = "/tmp") -> bytes:
    """Compile a bc64 .bas source into tokenized binary (no PRG header).

    bc64 supports labels, @aliases, auto-numbering, case-insensitive
    keywords, #include, and more. This is the only supported way to
    compile BASIC for injection — the built-in V2 tokenizer is removed.

    Steps:
    1. Write source to a temp .bas file
    2. Run bc64 to produce a .prg (contains 2-byte load address header)
    3. Read the .prg, strip the 2-byte header, return raw tokenized bytes

    Args:
        source_text: bc64 BASIC source code
        aliases: Enable --aliases/-a flag for @var naming
        temp_dir: Directory for temp files

    Returns:
        Raw tokenized bytes ready to POST to $0801 (no load address header)

    Raises:
        RuntimeError: if bc64 compilation fails
    """
    import subprocess, tempfile

    # Write source to temp file
    bas_path = os.path.join(temp_dir, "___bc64_temp.bas")
    prg_path = os.path.join(temp_dir, "___bc64_temp.prg")
    try:
        with open(bas_path, "w") as f:
            f.write(source_text)

        # Build command
        cmd = [BC64_PATH]
        if aliases:
            cmd.append("-a")
        cmd.extend(["-o", prg_path, bas_path])

        # Run compiler
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            raise RuntimeError(
                f"bc64 compilation failed (exit {result.returncode}):\n"
                f"{result.stderr}\n{result.stdout}"
            )

        # Read PRG, strip 2-byte load address header
        with open(prg_path, "rb") as f:
            prg_data = f.read()

        if len(prg_data) < 2:
            raise RuntimeError(f"bc64 produced empty output ({len(prg_data)} bytes)")

        # PRG header is 2-byte little-endian load address (usually $0801)
        body = prg_data[2:]

        if len(body) == 0:
            raise RuntimeError("bc64 produced only a header with no body")

        return body

    finally:
        # Cleanup temp files (best effort)
        for path in (bas_path, prg_path):
            try:
                if os.path.exists(path):
                    os.unlink(path)
            except OSError:
                pass


# ═══════════════════════════════════════════════════════════════════════
# U64 Remote Control
# ═══════════════════════════════════════════════════════════════════════

class U64Remote:
    """Control a C64 via Ultimate 64 / II+ REST API.

    Handles: BASIC injection, screen reading, keystroke injection,
    memory read/write, machine reset.

    Uses PUT for small hex-encoded writes (keyboard buffer)
    and POST for large binary writes (BASIC program injection).
    """

    def __init__(self, base_url: str = "", password: str = ""):
        base_url = base_url or f"http://{U64_IP_ADDRESS}"
        self.base = base_url.rstrip("/")
        self.api = f"{self.base}/v1"
        self._headers = {}
        if password:
            self._headers["X-Password"] = password
        if not requests:
            raise RuntimeError("'requests' library required. Install: pip install requests")

    # ── Low-level API calls ──────────────────────────────────────────

    def _put(self, endpoint: str, params: dict) -> None:
        """PUT with retries on transient failures."""
        url = f"{self.api}/{endpoint}"
        last_error = None
        for attempt in range(3):
            try:
                r = requests.put(url, params=params, headers=self._headers, timeout=10)
                if r.status_code in (429, 503):
                    last_error = RuntimeError(f"HTTP {r.status_code}")
                    time.sleep(0.5 * (attempt + 1))
                    continue
                r.raise_for_status()
                return
            except (requests.ConnectionError, requests.Timeout) as e:
                last_error = e
                time.sleep(0.5 * (attempt + 1))
            except requests.HTTPError as e:
                if e.response.status_code in (429, 503):
                    last_error = e
                    time.sleep(0.5 * (attempt + 1))
                else:
                    raise
        raise last_error  # type: ignore[misc]

    def _post_mem(self, address_hex: str, data: bytes) -> None:
        """POST binary data to memory (for BASIC injection)."""
        url = f"{self.api}/machine:writemem"
        for attempt in range(3):
            try:
                r = requests.post(
                    url, params={"address": address_hex}, data=data,
                    headers=self._headers, timeout=10,
                )
                r.raise_for_status()
                return
            except (requests.ConnectionError, requests.Timeout) as e:
                if attempt < 2:
                    time.sleep(0.5 * (attempt + 1))
                else:
                    raise e

    def _get_mem(self, address_hex: str, length: int = 256) -> bytes:
        """GET binary data from memory."""
        url = f"{self.api}/machine:readmem"
        for attempt in range(3):
            try:
                r = requests.get(
                    url, params={"address": address_hex, "length": str(length)},
                    headers=self._headers, timeout=10,
                )
                r.raise_for_status()
                return r.content
            except (requests.ConnectionError, requests.Timeout) as e:
                if attempt < 2:
                    time.sleep(0.5 * (attempt + 1))
                else:
                    raise e

    # ── Keystroke injection ─────────────────────────────────────────

    def _read_kb_count(self) -> int:
        """Read keyboard buffer count from $00C6."""
        try:
            return self._get_mem(KB_CNT_ADDR, 1)[0]
        except Exception:
            return -1

    def _flush_keyboard_buffer(self) -> None:
        """Discard any stale keys in the keyboard buffer.

        The C64's 10-byte FIFO at $0277 can accumulate stale key codes
        if a previous program was interrupted (STOP) or ended with keys
        still buffered. Before typing new text, we zero the count to
        flush everything.
        """
        count = self._read_kb_count()
        if count and count > 0:
            self._put("machine:writemem",
                       {"address": KB_CNT_ADDR, "data": "00"})
            time.sleep(0.02)

    def type_text(self, text: str, *, press_enter: bool = True,
                  delay: float = 0.05, flush: bool = True) -> None:
        """Inject keystrokes into the C64 keyboard buffer ($0277).

        Strategy — robust by design:
        1. Flush stale keys from the buffer before starting
        2. Write keys in ≤8 byte chunks (within 10-byte FIFO margin)
        3. Write buffer contents first, THEN set count (prevents IRQ
           from seeing empty/uninitialised buffer slots)
        4. Poll $00C6 between chunks to let the C64 drain before
           writing the next batch
        5. On drain timeout, flush stale keys and retry once
        6. Final fallback: send in 1-byte chunks

        The C64's KERNAL scans the keyboard buffer at IRQ time (every
        1/60s). As long as the C64 is not in blocking KERNAL I/O
        (IEC, tape, RS-232), this works reliably. For blocking I/O
        situations, use press_stop() first to unstick the C64.

        Args:
            text: PETSCII characters to send
            press_enter: append RETURN (chr$(13))
            delay: inter-chunk delay
            flush: flush stale buffer contents first (default True)
        """
        if not text and not press_enter:
            return

        # Flush stale keys before starting
        if flush:
            self._flush_keyboard_buffer()

        # Build key code list, append RETURN if needed.
        # Keyboard buffer at $0277 expects PETSCII key codes.
        # The C64 keyboard encoder is case-inverting: sending PETSCII
        # 0x41 ('A') produces lowercase 'a' on screen, and PETSCII
        # 0x61 ('a') produces uppercase 'A' on screen.
        # We swap the case of every letter so the injected keystrokes
        # match the caller's intent.  Non-alphabetic chars pass through.
        codes = []
        for c in text:
            if 'a' <= c <= 'z':
                codes.append(ord(c.upper()))
            elif 'A' <= c <= 'Z':
                codes.append(ord(c.lower()))
            else:
                codes.append(ord(c))
        if press_enter:
            codes.append(13)  # RETURN

        CHUNK_SIZE = 8  # ≤8 to leave margin in the 10-byte FIFO

        i = 0
        while i < len(codes):
            chunk = codes[i:i + CHUNK_SIZE]

            # Step 1: write key codes to the FIFO buffer
            # (all bytes written atomically within one PUT request —
            #  the U64's DMA writes them before the IRQ can fire)
            data_str = "".join(f"{c:02X}" for c in chunk)
            self._put("machine:writemem",
                       {"address": KB_BUF_ADDR, "data": data_str})

            # Step 2: set the count — this triggers the KERNAL IRQ
            # to read the bytes we just wrote
            self._put("machine:writemem",
                       {"address": KB_CNT_ADDR, "data": f"{len(chunk):02X}"})

            i += CHUNK_SIZE

            # Step 3: if more chunks follow, drain the buffer
            if i < len(codes):
                drained = self._wait_kb_drain(timeout=3.0)
                if not drained:
                    # Buffer didn't drain — C64 might be busy.
                    # Flush stale, retry this chunk in 1-byte mode.
                    self._flush_keyboard_buffer()
                    # Send remaining bytes one at a time
                    remaining = codes[i:]
                    for code in remaining:
                        self._put("machine:writemem",
                                   {"address": KB_BUF_ADDR, "data": f"{code:02X}"})
                        self._put("machine:writemem",
                                   {"address": KB_CNT_ADDR, "data": "01"})
                        drained = self._wait_kb_drain(timeout=2.0)
                        if not drained:
                            self._flush_keyboard_buffer()
                        time.sleep(delay)
                    return

            time.sleep(delay)

    def _wait_kb_drain(self, timeout: float = 3.0) -> bool:
        """Poll $00C6 until the keyboard buffer is empty.

        Returns True if drained, False if timed out.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                raw = self._get_mem(KB_CNT_ADDR, 1)
                if raw[0] == 0:
                    return True
                if raw[0] >= 10:
                    # Bogus count (>9) — buffer corrupted, force clear
                    self._flush_keyboard_buffer()
                    return True
            except requests.RequestException:
                pass
            time.sleep(0.02)
        return False  # timed out

    # ── Panic / recovery ───────────────────────────────────────────

    def press_stop_restore(self) -> None:
        """Emergency unstick: send STOP, then clear the buffer.

        If the C64 is stuck in KERNAL I/O (IEC bus, tape), the
        keyboard IRQ doesn't fire and the buffer doesn't drain.
        This sends STOP (RUN/STOP key) repeatedly — the KERNAL
        checks for STOP during I/O at specific points.
        """
        # Send STOP up to 3 times with pauses
        for _ in range(3):
            self._put("machine:writemem",
                       {"address": KB_BUF_ADDR, "data": "03"})
            self._put("machine:writemem",
                       {"address": KB_CNT_ADDR, "data": "01"})
            time.sleep(0.5)
        # Flush any remaining
        self._flush_keyboard_buffer()
        # Send just RETURN to get to READY
        self._put("machine:writemem",
                   {"address": KB_BUF_ADDR, "data": "0D"})
        self._put("machine:writemem",
                   {"address": KB_CNT_ADDR, "data": "01"})
        time.sleep(0.5)

    # ── High-level C64 operations ────────────────────────────────────

    def reset(self) -> None:
        """Hardware reset. Wait for C64 to boot."""
        self._put("machine:reset", {})
        time.sleep(3)

    def press_stop(self) -> None:
        """Send STOP (RUN/STOP key) to halt BASIC execution."""
        self.press_stop_restore()

    def clear_screen(self, *, force: bool = False) -> None:
        """Send CLR/HOME to clear the screen."""
        self._put("machine:writemem",
                   {"address": KB_BUF_ADDR, "data": "930D"})
        self._put("machine:writemem",
                   {"address": KB_CNT_ADDR, "data": "02"})
        time.sleep(0.3)
        if force:
            time.sleep(0.3)
            self._put("machine:writemem",
                       {"address": KB_BUF_ADDR, "data": "930D"})
            self._put("machine:writemem",
                       {"address": KB_CNT_ADDR, "data": "02"})
            time.sleep(0.3)

    def read_screen(self) -> str:
        """Read screen memory ($0400) and decode screen codes to ASCII.

        Always assumes mixed-case (lowercase/uppercase) mode, which is
        the expected C64 mode for Meatloaf debugging. PETSCII screen
        codes 1-26 decode to lowercase a-z, codes 65-90 to uppercase A-Z.
        """
        raw_screen = self._get_mem(SCREEN_ADDR, SCREEN_SIZE)
        return self._decode_screen(raw_screen)

    def inject_basic(self, text: str) -> None:
        """Compile bc64 source and inject into C64 memory at $0801.

        Uses the bc64 compiler to handle labels, @aliases, auto-numbering,
        and all other bc64 extended syntax. The 2-byte PRG load address
        header is stripped — only the tokenized body is sent.
        """
        data = compile_bc64(text)
        self._post_mem(f"{BASIC_ADDR:04X}", data)

        # Set variable pointer to end of program so variables work
        end_addr = BASIC_ADDR + len(data)
        var_ptr_hex = struct.pack("<H", end_addr).hex().upper()
        self._put("machine:writemem",
                   {"address": VAR_PTR_ADDR, "data": var_ptr_hex})

    def inject_prg(self, prg_path: str) -> int:
        """Inject a .prg file (cc65, bc64, KickAss, etc.) at its load address.

        RELIABLE PROTOCOL:
            1. reset()        — hardware reset
            2. sleep(20)      — wait for full READY. prompt
            3. inject_prg()   — write program body + fix BASIC pointers
            4. sleep(1)       — let writes settle
            5. type_text('run')  — BASIC PRGs; cc65 PRGs have EXEHDR SYS stub
        Do NOT inject before BASIC fully boots (first ~8s) — BASIC's cold
        init overwrites the program area. Inject at 20s+ when pointers are
        stable.

        For BASIC PRGs at $0801 (bc64-compiled), also restores pointers:
            $2B PNTR    = $0801
            $2D VARTAB  = end of program past zero-link
            $2F ARYTAB  = same as VARTAB
            $31 STREND  = $A000   ← critical for PRINT/INPUT
            $33 FRESPC  = $A000
            $37 FRETOP  = $A000
        Without STREND at $A000, every PRINT/INPUT throws ?SYNTAX ERROR.

        Reads the 2-byte little-endian load address from the PRG header
        and writes the body to that address via the unlimited POST endpoint.

        For cc65 PRGs defaults to $0801 with an 8-byte EXEHDR: the first
        bytes are `01 08 0B 08 ...` (load $0801, jump to STARTUP at $080B).
        SYS to $0801 works because EXEHDR immediately jumps to main().

        Args:
            prg_path: path to a .prg file (raw 2-byte LE header + body)

        Returns:
            The load address (int). Launch with `SYS {addr}`.
        """
        with open(prg_path, "rb") as f:
            prg = f.read()
        if len(prg) < 3:
            raise RuntimeError(f"PRG too small ({len(prg)} bytes): {prg_path}")
        load_addr = prg[0] | (prg[1] << 8)
        body = prg[2:]
        if not body:
            raise RuntimeError(f"PRG has empty body: {prg_path}")
        self._post_mem(f"{load_addr:04X}", body)

        # For BASIC programs at $0801, restore ALL internal pointers so
        # the interpreter can RUN the program without SYNTAX ERROR.
        #
        # After reset BASIC claims the memory up to the first zero-link,
        # sets VARTAB/ARYTAB past it, and STREND/FRETOP to $A000 (top of
        # BASIC RAM on a PAL C64).  Writing our larger program into $0801
        # overwrites BASIC's view — we must correct the pointers:
        #
        #   $2B  PNTR    = $0801            (always for BASIC programs)
        #   $2D  VARTAB  = end of program   (past zero-link $00 $00)
        #   $2F  ARYTAB  = same as VARTAB   (no array vars yet)
        #   $31  STREND  = $A000            (top of BASIC string space)
        #   $33  FRESPC  = $A000            (string utility pointer)
        #   $37  FRETOP  = $A000            (string GC threshold)
        #
        # STREND/FRESPC/FRETOP are the critical ones — if they're
        # accidentally left at $0803 (a common stale value after boot),
        # BASIC has < 1 byte of string space and every PRINT or INPUT
        # immediately throws SYNTAX ERROR.
        if load_addr == 0x0801:
            TOP_OF_BASIC = 0xA000  # PAL C64 top-of-BASIC
            end_of_prog = load_addr + len(body)
            for i in range(len(body) - 1, 0, -1):
                if body[i] == 0 and body[i-1] == 0:
                    end_of_prog = load_addr + i + 1
                    break
            self._post_mem("002B", struct.pack("<H", load_addr))
            self._post_mem("002D", struct.pack("<H", end_of_prog))
            self._post_mem("002F", struct.pack("<H", end_of_prog))
            self._post_mem("0031", struct.pack("<H", TOP_OF_BASIC))
            self._post_mem("0033", struct.pack("<H", TOP_OF_BASIC))
            self._post_mem("0037", struct.pack("<H", TOP_OF_BASIC))

        return load_addr

    def write_mem(self, address: int, data: bytes) -> None:
        """Write bytes to an arbitrary memory address.

        Use this to upload auxiliary data alongside a C PRG — sprite
        patterns, lookup tables, character sets, raw bitmaps, etc.
        Uses the unlimited POST endpoint so data can be any size.

        Example — upload 512 bytes of sprite data to $5A00:
            u64.write_mem(0x5A00, open("sprites.bin", "rb").read())
        """
        self._post_mem(f"{address:04X}", data)

    def read_mem(self, address: int, length: int) -> bytes:
        """Read bytes from memory.

        Useful for verifying an upload worked — read back the bytes
        you just wrote and diff against the source file. The bytes
        come back exactly as stored (no charset conversion).

        Example:
            raw = u64.read_mem(0x0801, len(open("foo.prg","rb").read()) - 2)
            assert raw == open("foo.prg","rb").read()[2:]
        """
        return self._get_mem(f"{address:04X}", length)

    def type_command(self, command: str, *, wait: float = 1.5,
                     clear: bool = True) -> str:
        """Clear screen, type BASIC command, wait, read screen."""
        if clear:
            self.clear_screen()
            time.sleep(0.2)
        self.type_text(command)
        time.sleep(wait)
        return self.read_screen()

    def wait_for_screen_change(self, baseline: str = "",
                                timeout: float = 10.0,
                                poll: float = 0.3) -> str:
        """Wait until screen content changes from baseline."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            screen = self.read_screen()
            if screen != baseline:
                return screen
            time.sleep(poll)
        return self.read_screen()

    def wait_for_text(self, text: str, *, timeout: float = 10.0,
                      poll: float = 0.5) -> Optional[str]:
        """Poll screen until text appears. Returns screen or None."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            screen = self.read_screen()
            if text in screen:
                return screen
            time.sleep(poll)
        return None

    # ── Screen decoder ────────────────────────────────────────────

    @staticmethod
    def _decode_screen(raw: bytes) -> str:
        """Decode screen memory ($0400) PETSCII screen codes to ASCII.

        Always assumes mixed-case (lowercase/uppercase) mode:
          - Screen codes 1-26  → lowercase a-z  (PETSCII codes 0xC1-0xDA)
          - Screen codes 33-64 → punctuation, digits, @
          - Screen codes 65-90 → uppercase A-Z  (PETSCII codes 0x41-0x5A)
          - Everything else    → placeholder

        This is the expected mode for Meatloaf debugging — the C64 normally
        boots into uppercase/graphics mode but mixed-case is set by Meatloaf
        or standard BASIC programs to show both upper and lower case.
        """
        lines = []
        for row in range(25):
            start = row * 40
            row_bytes = raw[start:start + 40]
            chars = []
            for b in row_bytes:
                b = b & 0x7F  # strip reverse (high-bit) flag

                if b == 0:
                    chars.append("@")
                elif 1 <= b <= 26:
                    # Screen codes 1-26 = lowercase a-z (PETSCII 0xC1-0xDA)
                    chars.append(chr(96 + b))
                elif 27 <= b <= 31:
                    # Cursor control / special chars → space
                    chars.append(" ")
                elif b == 32:
                    chars.append(" ")
                elif 33 <= b <= 64:
                    # 33-63: punctuation, digits
                    # 64: @
                    chars.append(chr(b))
                elif 65 <= b <= 90:
                    # Screen codes 65-90 = uppercase A-Z (PETSCII 0x41-0x5A)
                    chars.append(chr(b))
                elif 91 <= b <= 95:
                    # Box drawing chars → placeholder
                    chars.append("·")
                else:
                    # 96-127: graphics or reverse chars
                    chars.append("·")

            lines.append("".join(chars).rstrip())
        return "\n".join(lines)


# ═══════════════════════════════════════════════════════════════════════
# Serial Capture Manager
# ═══════════════════════════════════════════════════════════════════════

def ensure_capture_script():
    """Ensure serial_capture.py exists and is executable."""
    if not SERIAL_SCRIPT.exists():
        print(f"[serial] ERROR: {SERIAL_SCRIPT} not found!")
        return False
    if not os.access(str(SERIAL_SCRIPT), os.X_OK):
        SERIAL_SCRIPT.chmod(0o755)
    return True


def start_capture(port: str = "") -> bool:
    """Start the robust serial capture daemon.

    Features:
    - Auto-reconnect if port disconnects (ESP reboot, flash cycle)
    - Health heartbeat file
    - PID tracking
    - Exponential backoff on reconnect
    - Cleans up any stale capture first

    Returns True if confirmed running, False otherwise.
    """
    if not ensure_capture_script():
        return False

    port = port or SERIAL_PORT
    if not port:
        print("[serial] ERROR: no serial port. Set monitor_port in "
              f"{MEATLOAF_DIR / 'platformio.ini'} or MEATLOAF_UPLOAD_PORT.")
        return False
    stop_capture()  # kill stale one first
    time.sleep(0.5)

    cmd = [
        sys.executable or "python3",
        str(SERIAL_SCRIPT),
        "--port", port,
        "--baud", str(MONITOR_SPEED),
        "--out", str(SERIAL_LOG),
        "--pid", str(SERIAL_PID),
    ]

    # Suppress stdout/stderr — the daemon writes to serial log directly
    with open(os.devnull, "w") as null:
        proc = subprocess.Popen(
            cmd, stdout=null, stderr=null,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )

    # Wait for it to initialize and check health
    time.sleep(2)

    if is_capture_running():
        print(f"[serial] Capture running (pid={proc.pid}) → {SERIAL_LOG}")
        return True
    else:
        print(f"[serial] Warning: capture may not have started (check {SERIAL_LOG})")
        return False


def stop_capture() -> None:
    """Stop the serial capture daemon."""
    if SERIAL_PID.exists():
        try:
            pid = int(SERIAL_PID.read_text().strip())
            # SIGTERM first
            try:
                os.kill(pid, signal.SIGTERM)
                time.sleep(0.5)
            except ProcessLookupError:
                pass
            # Force kill if still alive
            try:
                os.kill(pid, 0)
                os.kill(pid, signal.SIGKILL)
                time.sleep(0.3)
            except ProcessLookupError:
                pass
        except (ValueError, OSError):
            pass
        SERIAL_PID.unlink(missing_ok=True)

    # Cleanup heartbeat too
    SERIAL_HEARTBEAT.unlink(missing_ok=True)


def is_capture_running() -> bool:
    """Check if the serial capture is healthy.

    Checks both the PID and heartbeat freshness.
    """
    if not SERIAL_PID.exists():
        return False
    if not SERIAL_HEARTBEAT.exists():
        return False

    try:
        pid = int(SERIAL_PID.read_text().strip())
        os.kill(pid, 0)  # Process alive check (signal 0)
    except (ProcessLookupError, ValueError, OSError):
        return False

    # Check heartbeat is fresh (< 30s old)
    try:
        hb_age = time.time() - SERIAL_HEARTBEAT.stat().st_mtime
        if hb_age > 30:
            return False
    except OSError:
        return False

    return True


def serial_log_size() -> int:
    """Size of the serial log in bytes."""
    return SERIAL_LOG.stat().st_size if SERIAL_LOG.exists() else 0


def read_log(lines: int = 50) -> str:
    """Read last N lines of the serial log."""
    if not SERIAL_LOG.exists():
        return "[serial] No log file yet — capture may still be initializing"
    with open(str(SERIAL_LOG)) as f:
        all_lines = f.readlines()
    tail = all_lines[-lines:] if len(all_lines) > lines else all_lines
    return "".join(tail)


def grep_log(pattern: str) -> list[str]:
    """Grep the serial log for a pattern."""
    if not SERIAL_LOG.exists():
        return []
    with open(str(SERIAL_LOG)) as f:
        return [line for line in f if re.search(pattern, line, re.IGNORECASE)]


def send_command(command: str, *, wait: float = 1.0) -> str:
    """Send a command to Meatloaf via the daemon's command FIFO.

    The daemon holds the serial port open. We write commands to the
    FIFO pipe; the daemon relays them to Meatloaf and captures output.

    Args:
        command: The Meatloaf console command (e.g. 'sysinfo', 'meminfo')
        wait: Seconds to wait for the output to appear in the log

    Returns:
        Lines from the serial log that appear after the command was sent.
    """
    fifo_path = "/tmp/serial_capture_cmd"

    if not os.path.exists(fifo_path):
        return "[send] ERROR: Command FIFO not found. Is capture running?"

    if not is_capture_running():
        return "[send] ERROR: Capture not running. Start it first."

    # Get current log size as baseline
    before_size = serial_log_size()

    try:
        with open(fifo_path, "w") as fifo:
            fifo.write(command + "\n")
    except OSError as e:
        return f"[send] ERROR: Could not write to FIFO: {e}"

    time.sleep(wait)

    # Read new lines that appeared after our command
    new_data = read_log_after(before_size)
    return new_data


def read_log_after(offset: int) -> str:
    """Read all data from the serial log starting at byte offset `offset`."""
    if not SERIAL_LOG.exists():
        return ""
    try:
        with open(str(SERIAL_LOG), "rb") as f:
            f.seek(offset)
            return f.read().decode("utf-8", errors="replace")
    except OSError:
        return ""


def wait_for_serial_data(timeout: float = 15.0, poll: float = 0.5) -> bool:
    """Wait until the serial log has data (new firmware has booted)."""
    baseline = serial_log_size()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if serial_log_size() > baseline:
            return True
        time.sleep(poll)
    return False


# ═══════════════════════════════════════════════════════════════════════
# Meatloaf Build / Flash
# ═══════════════════════════════════════════════════════════════════════

def build_config() -> dict:
    """Effective build settings: platformio.ini values plus any env-var
    overrides actually in force."""
    return {
        "project_root": str(MEATLOAF_DIR),
        "ini_path": PIO["ini_path"],
        "environment": BUILD_ENV,
        "board": PIO["board"],
        "mcu": PIO["mcu"],
        "flash_size": PIO["flash_size"],
        "build_platform": PIO["build_platform"],
        "u64_ip_address": U64_IP_ADDRESS,
        "upload_port": UPLOAD_PORT,
        "serial_port": SERIAL_PORT,
        "monitor_speed": MONITOR_SPEED,
        "verbose_flags": PIO["verbose_flags"],
        "defines": sorted(PIO["defines"]),
        # U64_PASSWORD is deliberately not reported.
        "overrides": {k: v for k, v in os.environ.items()
                      if k.startswith("MEATLOAF_") or k == "U64_IP_ADDRESS"},
    }


def get_build_env() -> str:
    """Active build environment ([meatloaf] environment in platformio.ini)."""
    if not BUILD_ENV:
        raise RuntimeError(
            "No build environment resolved. Uncomment one 'environment =' line "
            f"in {MEATLOAF_DIR / 'platformio.ini'}, or set MEATLOAF_BUILD_ENV."
        )
    return BUILD_ENV


def build_and_flash(*, port: str = "") -> None:
    """Build Meatloaf firmware and flash to device.

    WARNING: This takes 30-60 seconds and reboots the ESP32.
    The serial capture will lose connection during flash and
    auto-reconnect after the ESP reboots.
    """
    env = get_build_env()
    upload_port = port or UPLOAD_PORT
    cmd = ["pio", "run", "-e", env, "-t", "upload"]
    if upload_port:
        # Otherwise let PlatformIO use platformio.ini's own upload_port.
        cmd += ["--upload-port", upload_port]
    print(f"[build] Building and flashing ({env})...")
    print(f"[build] {' '.join(cmd)}")
    subprocess.run(cmd, cwd=str(MEATLOAF_DIR), check=True)
    print("[build] Flash complete. ESP32 rebooting...")
    time.sleep(2)


# ═══════════════════════════════════════════════════════════════════════
# Echo Test Server
# ═══════════════════════════════════════════════════════════════════════

def start_echo_server(port: int = 8080) -> bool:
    """Start the Meatloaf echo test server on the given port."""
    stop_echo_server()

    log_path = "/tmp/meatloaf_echo_server.log"
    env = os.environ.copy()
    env["MEATLOAF_LISTEN_PORT"] = str(port)

    proc = subprocess.Popen(
        [sys.executable or "python3", str(ECHO_SERVER_SCRIPT), str(port)],
        stdout=open(log_path, "w"),
        stderr=subprocess.STDOUT,
        cwd=str(ECHO_SERVER_DIR),
        start_new_session=True,
    )
    ECHO_SERVER_PID.write_text(str(proc.pid))

    # Health check
    time.sleep(1.5)
    try:
        r = requests.get(f"http://127.0.0.1:{port}/echo", timeout=3)
        print(f"[echo] Server running (pid={proc.pid}) on port {port}")
        return True
    except requests.RequestException:
        print(f"[echo] Warning: server may not have started (port {port})")
        return False


def stop_echo_server() -> None:
    """Stop the echo server."""
    if ECHO_SERVER_PID.exists():
        try:
            pid = int(ECHO_SERVER_PID.read_text().strip())
            try:
                os.kill(pid, signal.SIGTERM)
                time.sleep(0.3)
            except ProcessLookupError:
                pass
        except (ValueError, OSError):
            pass
        ECHO_SERVER_PID.unlink(missing_ok=True)


# ═══════════════════════════════════════════════════════════════════════
# High-level Debug Cycle
# ═══════════════════════════════════════════════════════════════════════

class MeatloafDebugCycle:
    """Orchestrates the unattended modify → deploy → run → read → fix loop."""

    def __init__(self, u64_base_url: str = "", password: str = ""):
        self.u64 = U64Remote(u64_base_url, password)
        self.echo_server_running = False

    # ── Diagnostics ─────────────────────────────────────────────────

    def full_diagnostic(self) -> dict:
        """Check connectivity of all components."""
        result = {"build_config": build_config()}

        # U64 reachability
        try:
            screen = self.u64.read_screen()
            result["u64"] = "reachable"
            result["screen_preview"] = screen[:120].replace("\n", " | ")
        except Exception as e:
            result["u64"] = f"ERROR: {e}"

        # U64 version/info
        try:
            r = requests.get(f"{self.u64.api}/info",
                              headers=self.u64._headers, timeout=5)
            if r.ok:
                info = r.json()
                result["u64_info"] = {
                    "product": info.get("product", ""),
                    "firmware": info.get("firmware", ""),
                    "hostname": info.get("hostname", ""),
                }
        except Exception as e:
            result["u64_info"] = str(e)

        # Serial capture
        result["serial_capture"] = {
            "running": is_capture_running(),
            "log_path": str(SERIAL_LOG),
            "log_size": serial_log_size(),
        }

        # Echo server
        result["echo_server"] = {
            "pid_file": ECHO_SERVER_PID.exists(),
        }

        return result

    # ── Single cycle ─────────────────────────────────────────────────

    def run_cycle(self, basic_code: str = "", *,
                  firmware_changed: bool = False,
                  run_command: str = "RUN",
                  wait_time: float = 2.0,
                  ensure_ready: bool = True) -> str:
        """Run one complete modify → deploy → run → read cycle.

        Args:
            basic_code: BASIC to inject (empty = skip, use existing)
            firmware_changed: if True, build+flash firmware first
            run_command: BASIC command to type
            wait_time: seconds to wait for output
            ensure_ready: if True, press STOP + ENTER before starting

        Returns:
            Screen output as decoded text
        """
        # Phase 1: Ensure capture is running
        if not is_capture_running():
            print("[cycle] ⚠ Serial capture not running — starting...")
            start_capture()

        # Phase 2: Deploy firmware if needed
        if firmware_changed:
            print("[cycle] 🔨 Building and flashing firmware...")
            build_and_flash()
            # After flash, wait for ESP32 to reboot and start logging
            print("[cycle] Waiting for ESP32 to reboot...")
            time.sleep(3)
            if not wait_for_serial_data(timeout=20):
                print("[cycle] ⚠ Serial data not appearing after flash")

        # Phase 3: Ensure C64 is at READY
        if ensure_ready:
            print("[cycle] Ensuring C64 is ready...")
            self.u64.press_stop()
            time.sleep(0.3)
            # Press ENTER to dismiss any BREAK message
            self.u64.type_text("", press_enter=True)
            time.sleep(0.5)
            # Verify we're at READY (case-insensitive — can be "ready." in lowercase mode)
            screen = self.u64.read_screen()
            if "ready" not in screen.lower():
                print(f"[cycle] Screen doesn't show READY: {screen[:80]!r}")
                print("[cycle] Resetting...")
                self.u64.reset()

        # Phase 4: Inject BASIC
        if basic_code:
            print(f"[cycle] Injecting BASIC ({len(basic_code.splitlines())} lines)...")
            self.u64.inject_basic(basic_code)
            time.sleep(0.3)

        # Phase 5: Run
        print(f"[cycle] Running: {run_command}")
        result = self.u64.type_command(run_command, wait=wait_time, clear=True)

        print(f"[cycle] Screen ({len(result)} chars):")
        print(result)
        return result

    def inject_and_run(self, basic_code: str, *, wait: float = 3.0) -> str:
        """Convenience: STOP → ENTER → inject → RUN → read screen."""
        return self.run_cycle(basic_code, wait_time=wait, ensure_ready=True)

    def run_existing(self, run_command: str = "RUN", *, wait: float = 2.0) -> str:
        """Run a command on an already-injected program. No inject."""
        self.u64.clear_screen()
        time.sleep(0.2)
        self.u64.type_text(run_command)
        time.sleep(wait)
        return self.u64.read_screen()


# ═══════════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════════

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Meatloaf C64 Debug Automation — unattended debug cycle"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # ── C64 control ──────────────────────────────────────────────────
    p = sub.add_parser("config",
                       help="Show build settings resolved from platformio.ini")
    p = sub.add_parser("diag", help="Full setup diagnostic")
    p = sub.add_parser("screen", help="Read C64 screen")
    p.add_argument("--url", default="")
    p = sub.add_parser("reset", help="Hardware reset C64")
    p.add_argument("--url", default="")
    p = sub.add_parser("stop", help="Press STOP key")
    p.add_argument("--url", default="")

    p = sub.add_parser("inject", help="Inject BASIC program")
    p.add_argument("file", nargs="?", default="-",
                   help="BASIC file (- = stdin)")
    p.add_argument("--url", default="")

    p = sub.add_parser("run", help="Type command + read screen")
    p.add_argument("command", nargs="?", default="RUN")
    p.add_argument("--wait", type=float, default=1.5)
    p.add_argument("--url", default="")

    p = sub.add_parser("type", help="Type text (no screen read)")
    p.add_argument("text", help="Text to type")
    p.add_argument("--url", default="")

    p = sub.add_parser("test", help="Inject BASIC + RUN + read screen")
    p.add_argument("file", nargs="?", default="-")
    p.add_argument("--wait", type=float, default=3.0)
    p.add_argument("--url", default="")

    p = sub.add_parser("inject-prg", help="Inject a compiled .prg (cc65/etc.) at its native load address")
    p.add_argument("file", help="Path to .prg file")
    p.add_argument("--run", metavar="ADDR",
                   help="After inject, type 'SYS ADDR' (decimal or $hex) to launch. "
                        "Default: SYS the load address.")
    p.add_argument("--wait", type=float, default=1.5,
                   help="Wait after launch before exiting (sec)")
    p.add_argument("--url", default="")

    p = sub.add_parser("write-mem", help="Write bytes to an arbitrary memory address")
    p.add_argument("address", help="Hex address (e.g. 5A00)")
    p.add_argument("file", help="Path to binary file")
    p.add_argument("--url", default="")

    p = sub.add_parser("read-mem", help="Read bytes from memory (writes hex to stdout)")
    p.add_argument("address", help="Hex address (e.g. 0801)")
    p.add_argument("length", type=int, help="Number of bytes to read")
    p.add_argument("--raw", action="store_true",
                   help="Emit raw bytes (redirect to file), not hex")
    p.add_argument("--url", default="")

    # ── Serial capture ──────────────────────────────────────────────
    p = sub.add_parser("capture", help="Manage serial capture")
    p.add_argument("action", choices=["start", "stop", "status",
                                       "tail", "grep", "wait"])
    p.add_argument("--pattern", default="", help="Grep pattern")
    p.add_argument("--port", default="")
    p.add_argument("--lines", type=int, default=50, help="Tail lines")
    p.add_argument("--timeout", type=float, default=15.0)

    # ── Meatloaf console ───────────────────────────────────────────
    p = sub.add_parser("send", help="Send command to Meatloaf via serial console")
    p.add_argument("cmd", help="Meatloaf console command (e.g. sysinfo, meminfo)")
    p.add_argument("--wait", type=float, default=2.0, help="Wait for response")

    # ── Build/flash ──────────────────────────────────────────────────
    p = sub.add_parser("build", help="Build/flash Meatloaf firmware")
    p.add_argument("--flash", action="store_true",
                   help="Flash after build")
    p.add_argument("--port", default="")

    # ── Echo server ──────────────────────────────────────────────────
    p = sub.add_parser("echo", help="Manage echo test server")
    p.add_argument("action", choices=["start", "stop", "status"])
    p.add_argument("--port", type=int, default=8080)

    # ── Cycle ────────────────────────────────────────────────────────
    p = sub.add_parser("cycle", help="Full unattended debug cycle")
    p.add_argument("--basic", default="", help="BASIC code or file")
    p.add_argument("--firmware", action="store_true",
                   help="Build+flash first")
    p.add_argument("--run", default="RUN", help="Run command")
    p.add_argument("--wait", type=float, default=2.0)
    p.add_argument("--url", default="")

    args = parser.parse_args()

    # Needs no U64 connection — answer before building the cycle.
    if args.command == "config":
        print(json.dumps(build_config(), indent=2))
        return

    cycle = MeatloafDebugCycle(
        args.url if hasattr(args, 'url') and args.url else "",
        os.environ.get("U64_PASSWORD", ""),
    )

    # ── Dispatch ──────────────────────────────────────────────────────
    if args.command == "diag":
        result = cycle.full_diagnostic()
        print(json.dumps(result, indent=2))

    elif args.command == "screen":
        print(cycle.u64.read_screen())

    elif args.command == "reset":
        cycle.u64.reset()
        print("Reset complete.")

    elif args.command == "stop":
        cycle.u64.press_stop()
        print("STOP sent.")

    elif args.command == "inject":
        if args.file == "-":
            code = sys.stdin.read()
        else:
            with open(args.file) as f:
                code = f.read()
        cycle.u64.inject_basic(code)
        print("Injected.")

    elif args.command == "inject-prg":
        load_addr = cycle.u64.inject_prg(args.file)
        body_size = os.path.getsize(args.file) - 2
        target_addr = args.run
        if not target_addr:
            sys_addr = load_addr
        elif target_addr.startswith("$"):
            sys_addr = int(target_addr[1:], 16)
        else:
            sys_addr = int(target_addr)
        print(f"Injected {body_size} bytes at ${load_addr:04X}; launching SYS {sys_addr}")
        cycle.u64.press_stop()
        time.sleep(0.2)
        cycle.u64.clear_screen()
        time.sleep(0.1)
        cycle.u64.type_text(f"SYS {sys_addr}")
        time.sleep(args.wait)
        print(cycle.u64.read_screen())

    elif args.command == "write-mem":
        addr = int(args.address.lstrip("$"), 16)
        with open(args.file, "rb") as f:
            data = f.read()
        cycle.u64.write_mem(addr, data)
        print(f"Wrote {len(data)} bytes to ${addr:04X}")

    elif args.command == "read-mem":
        addr = int(args.address.lstrip("$"), 16)
        raw = cycle.u64.read_mem(addr, args.length)
        if args.raw:
            sys.stdout.buffer.write(raw)
        else:
            print(raw.hex())

    elif args.command == "run":
        result = cycle.u64.type_command(args.command, wait=args.wait)
        print(result)

    elif args.command == "type":
        cycle.u64.type_text(args.text, press_enter=True)

    elif args.command == "test":
        if args.file == "-":
            code = sys.stdin.read()
        else:
            with open(args.file) as f:
                code = f.read()
        result = cycle.inject_and_run(code, wait=args.wait)
        print(result)

    elif args.command == "send":
        result = send_command(args.cmd, wait=args.wait)
        print(result)

    elif args.command == "capture":
        if args.action == "start":
            start_capture(args.port)
        elif args.action == "stop":
            stop_capture()
        elif args.action == "status":
            running = is_capture_running()
            size = serial_log_size()
            print(f"Running: {running}")
            print(f"Log:     {SERIAL_LOG} ({size} bytes)")
            if running:
                print("--- last 10 lines ---")
                print(read_log(10))
        elif args.action == "tail":
            print(read_log(args.lines))
        elif args.action == "grep":
            for line in grep_log(args.pattern):
                print(line, end="")
        elif args.action == "wait":
            ok = wait_for_serial_data(timeout=args.timeout)
            print(f"Serial data appeared: {ok}")
            if ok:
                print(read_log(10))

    elif args.command == "build":
        if args.flash:
            build_and_flash(port=args.port)
        else:
            env = get_build_env()
            cmd = ["pio", "run", "-e", env]
            print(f"[build] {' '.join(cmd)}")
            subprocess.run(cmd, cwd=str(MEATLOAF_DIR), check=True)
            print("[build] Build complete.")

    elif args.command == "echo":
        if args.action == "start":
            start_echo_server(args.port)
        elif args.action == "stop":
            stop_echo_server()
        elif args.action == "status":
            alive = ECHO_SERVER_PID.exists()
            print(f"Running: {alive}")

    elif args.command == "cycle":
        basic = ""
        if args.basic:
            if os.path.isfile(args.basic):
                with open(args.basic) as f:
                    basic = f.read()
            else:
                basic = args.basic
        output = cycle.run_cycle(
            basic,
            firmware_changed=args.firmware,
            run_command=args.run,
            wait_time=args.wait,
        )
        print(output)


if __name__ == "__main__":
    # 'config' only reads platformio.ini — usable before requests is installed,
    # which is exactly when you want to check your setup.
    if not requests and sys.argv[1:2] != ["config"]:
        print("Error: 'requests' library required. pip install requests")
        sys.exit(1)
    main()
