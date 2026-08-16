#!/usr/bin/env python3
"""
VICE binary monitor client.

Implements the wire protocol documented at
https://vice-emu.sourceforge.io/vice_13.html (Binary monitor).

Standalone: this module has no dependencies outside the standard library and
does not import the other scripts in this skill. Import it directly if you
want to drive VICE from your own Python.

    from vice_monitor import ViceMonitor
    with ViceMonitor() as m:
        print(m.registers())
        print(m.read_memory(0x0400, 40))

Protocol shape (API version 2):

    command   STX(1) API(1) BODYLEN(4 LE) REQID(4 LE) CMDTYPE(1) BODY...
    response  STX(1) API(1) BODYLEN(4 LE) RESPTYPE(1) ERR(1) REQID(4 LE) BODY...

The command header is 11 bytes, the response header 12. BODYLEN counts only
the bytes after the header. Asynchronous events (checkpoint hit, stopped,
resumed, JAM) arrive with request id 0xFFFFFFFF interleaved with responses,
so every read goes through a single pump that sorts them apart.
"""

from __future__ import annotations

import socket
import struct
import time
from collections import deque
from dataclasses import dataclass, field

STX = 0x02
API_VERSION = 0x02
EVENT_REQUEST_ID = 0xFFFFFFFF

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 6502

# --- command types ---------------------------------------------------------
CMD_MEM_GET = 0x01
CMD_MEM_SET = 0x02
CMD_CHECKPOINT_GET = 0x11
CMD_CHECKPOINT_SET = 0x12
CMD_CHECKPOINT_DELETE = 0x13
CMD_CHECKPOINT_LIST = 0x14
CMD_CHECKPOINT_TOGGLE = 0x15
CMD_CONDITION_SET = 0x22
CMD_REGISTERS_GET = 0x31
CMD_REGISTERS_SET = 0x32
CMD_DUMP = 0x41
CMD_UNDUMP = 0x42
CMD_RESOURCE_GET = 0x51
CMD_RESOURCE_SET = 0x52
CMD_ADVANCE_INSTRUCTIONS = 0x71
CMD_KEYBOARD_FEED = 0x72
CMD_EXECUTE_UNTIL_RETURN = 0x73
CMD_PING = 0x81
CMD_BANKS_AVAILABLE = 0x82
CMD_REGISTERS_AVAILABLE = 0x83
CMD_DISPLAY_GET = 0x84
CMD_VICE_INFO = 0x85
CMD_PALETTE_GET = 0x91
CMD_JOYPORT_SET = 0xA2
CMD_USERPORT_SET = 0xB2
CMD_EXIT = 0xAA
CMD_QUIT = 0xBB
CMD_RESET = 0xCC
CMD_AUTOSTART = 0xDD

# --- response / event types ------------------------------------------------
RESP_INVALID = 0x00
RESP_CHECKPOINT_INFO = 0x11
RESP_REGISTER_INFO = 0x31
RESP_JAM = 0x61
RESP_STOPPED = 0x62
RESP_RESUMED = 0x63

# --- memspaces -------------------------------------------------------------
MEMSPACE_MAIN = 0x00
MEMSPACE_DRIVE8 = 0x01
MEMSPACE_DRIVE9 = 0x02
MEMSPACE_DRIVE10 = 0x03
MEMSPACE_DRIVE11 = 0x04

# --- checkpoint operation bits --------------------------------------------
OP_LOAD = 0x01
OP_STORE = 0x02
OP_EXEC = 0x04

ERROR_TEXT = {
    0x00: "OK",
    0x01: "object does not exist",
    0x02: "invalid memspace",
    0x80: "incorrect command length",
    0x81: "invalid parameter value",
    0x82: "API version not understood",
    0x83: "command type not understood",
    0x8F: "general failure",
}


class ViceMonitorError(RuntimeError):
    """Raised when VICE returns a non-zero error code or the link breaks."""


class ViceNotRunning(ViceMonitorError):
    """Raised when the monitor socket cannot be reached at all."""


@dataclass
class Packet:
    type: int
    error: int
    request_id: int
    body: bytes


@dataclass
class Checkpoint:
    number: int
    hit: bool
    start: int
    end: int
    stop_when_hit: bool
    enabled: bool
    operation: int
    temporary: bool
    hit_count: int
    ignore_count: int
    has_condition: bool
    memspace: int

    @property
    def kind(self) -> str:
        bits = []
        if self.operation & OP_LOAD:
            bits.append("load")
        if self.operation & OP_STORE:
            bits.append("store")
        if self.operation & OP_EXEC:
            bits.append("exec")
        return "+".join(bits) or "none"

    def describe(self) -> str:
        span = f"${self.start:04x}" if self.end == self.start else f"${self.start:04x}-${self.end:04x}"
        flags = [self.kind]
        if not self.enabled:
            flags.append("disabled")
        if self.temporary:
            flags.append("temporary")
        if not self.stop_when_hit:
            flags.append("trace")
        if self.has_condition:
            flags.append("conditional")
        return f"#{self.number} {span} {' '.join(flags)} hits={self.hit_count}"


@dataclass
class Event:
    """An asynchronous message from VICE (request id 0xFFFFFFFF)."""

    type: int
    body: bytes
    at: float = field(default_factory=time.time)

    @property
    def name(self) -> str:
        return {
            RESP_CHECKPOINT_INFO: "checkpoint",
            RESP_JAM: "jam",
            RESP_STOPPED: "stopped",
            RESP_RESUMED: "resumed",
        }.get(self.type, f"0x{self.type:02x}")

    @property
    def pc(self):
        """Program counter for jam/stopped/resumed events, else None."""
        if self.type in (RESP_JAM, RESP_STOPPED, RESP_RESUMED) and len(self.body) >= 2:
            return struct.unpack_from("<H", self.body, 0)[0]
        return None


def _parse_checkpoint(body: bytes) -> Checkpoint:
    # CN(4) CH(1) SA(2) EA(2) ST(1) EN(1) OP(1) TM(1) HC(4) IC(4) CE(1) MS(1)
    if len(body) < 23:
        raise ViceMonitorError(f"short checkpoint body ({len(body)} bytes)")
    (num, hit, start, end, stop, enabled, op, temp,
     hits, ignores, has_cond, memspace) = struct.unpack_from("<IBHHBBBBIIBB", body, 0)
    return Checkpoint(
        number=num, hit=bool(hit), start=start, end=end,
        stop_when_hit=bool(stop), enabled=bool(enabled), operation=op,
        temporary=bool(temp), hit_count=hits, ignore_count=ignores,
        has_condition=bool(has_cond), memspace=memspace,
    )


class ViceMonitor:
    """A connection to VICE's binary monitor.

    Every exchange goes through `request()`, which pumps packets until the one
    matching its request id arrives. Anything with request id 0xFFFFFFFF is an
    asynchronous event and gets queued on `self.events` instead, so a
    checkpoint firing mid-command never corrupts a reply.
    """

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT,
                 timeout: float = 10.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self.events: deque[Event] = deque(maxlen=512)
        self.stopped: bool | None = None   # None = unknown, True = at monitor prompt
        self.last_pc: int | None = None
        self._next_id = 1
        self._reg_meta_cache: dict[int, dict] | None = None

    # -- connection ---------------------------------------------------------
    def connect(self, retries: int = 1, retry_delay: float = 0.5) -> "ViceMonitor":
        last = None
        for attempt in range(max(1, retries)):
            try:
                s = socket.create_connection((self.host, self.port), timeout=self.timeout)
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.sock = s
                return self
            except OSError as exc:
                last = exc
                if attempt + 1 < retries:
                    time.sleep(retry_delay)
        raise ViceNotRunning(
            f"cannot reach VICE binary monitor at {self.host}:{self.port} ({last}). "
            f"Start VICE with: -binarymonitor -binarymonitoraddress ip4://{self.host}:{self.port}"
        )

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self) -> "ViceMonitor":
        if self.sock is None:
            self.connect()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # -- raw framing --------------------------------------------------------
    def _read_exact(self, n: int, deadline: float | None = None) -> bytes:
        if self.sock is None:
            raise ViceMonitorError("not connected")
        buf = bytearray()
        while len(buf) < n:
            if deadline is not None:
                remaining = deadline - time.time()
                if remaining <= 0:
                    raise TimeoutError("timed out reading from VICE")
                self.sock.settimeout(min(remaining, self.timeout))
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ViceMonitorError("VICE closed the monitor connection")
            buf += chunk
        return bytes(buf)

    def _read_packet(self, deadline: float | None = None) -> Packet:
        # Resynchronise on STX: a desynced stream is unrecoverable otherwise.
        while True:
            first = self._read_exact(1, deadline)
            if first[0] == STX:
                break
        header = self._read_exact(11, deadline)
        api = header[0]
        body_len, resp_type, err, req_id = struct.unpack_from("<IBBI", header, 1)
        if api != API_VERSION:
            raise ViceMonitorError(f"unexpected API version 0x{api:02x} (expected 0x02)")
        body = self._read_exact(body_len, deadline) if body_len else b""
        return Packet(type=resp_type, error=err, request_id=req_id, body=body)

    def _note_event(self, pkt: Packet) -> None:
        ev = Event(type=pkt.type, body=pkt.body)
        if ev.type == RESP_STOPPED:
            self.stopped = True
            self.last_pc = ev.pc
        elif ev.type == RESP_RESUMED:
            self.stopped = False
            self.last_pc = ev.pc
        elif ev.type == RESP_JAM:
            self.stopped = True
            self.last_pc = ev.pc
        self.events.append(ev)

    def _send(self, cmd_type: int, body: bytes = b"") -> int:
        if self.sock is None:
            raise ViceMonitorError("not connected")
        req_id = self._next_id
        self._next_id = (self._next_id + 1) & 0x7FFFFFFF or 1
        header = struct.pack("<BBIIB", STX, API_VERSION, len(body), req_id, cmd_type)
        self.sock.sendall(header + body)
        return req_id

    def request(self, cmd_type: int, body: bytes = b"", *,
                timeout: float | None = None, check: bool = True) -> Packet:
        """Send a command and return its reply, queueing any events seen first."""
        req_id = self._send(cmd_type, body)
        deadline = time.time() + (timeout if timeout is not None else self.timeout)
        while True:
            pkt = self._read_packet(deadline)
            if pkt.request_id == EVENT_REQUEST_ID:
                self._note_event(pkt)
                continue
            if pkt.request_id != req_id:
                # A reply to a request we abandoned; drop it.
                continue
            if check and pkt.error != 0:
                raise ViceMonitorError(
                    f"command 0x{cmd_type:02x} failed: "
                    f"{ERROR_TEXT.get(pkt.error, 'unknown')} (0x{pkt.error:02x})"
                )
            return pkt

    def pump(self, duration: float = 0.0) -> list[Event]:
        """Drain pending events for up to `duration` seconds. Never raises on timeout."""
        deadline = time.time() + duration
        seen: list[Event] = []
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                return seen
            try:
                pkt = self._read_packet(time.time() + remaining)
            except TimeoutError:
                return seen
            if pkt.request_id == EVENT_REQUEST_ID:
                self._note_event(pkt)
                seen.append(self.events[-1])

    def wait_for_stop(self, timeout: float = 30.0) -> Event | None:
        """Block until a stopped/JAM event arrives. Returns it, or None on timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                pkt = self._read_packet(deadline)
            except TimeoutError:
                return None
            if pkt.request_id == EVENT_REQUEST_ID:
                self._note_event(pkt)
                if pkt.type in (RESP_STOPPED, RESP_JAM):
                    return self.events[-1]
        return None

    # -- introspection ------------------------------------------------------
    def ping(self) -> bool:
        self.request(CMD_PING)
        return True

    def vice_info(self) -> dict:
        body = self.request(CMD_VICE_INFO).body
        ml = body[0]
        version = list(body[1:1 + ml])
        off = 1 + ml
        sl = body[off]
        svn = body[off + 1:off + 1 + sl]
        rev = int.from_bytes(svn, "little") if sl else 0
        return {
            "version": ".".join(str(b) for b in version),
            "svn_revision": rev,
        }

    def banks(self) -> dict[str, int]:
        body = self.request(CMD_BANKS_AVAILABLE).body
        count = struct.unpack_from("<H", body, 0)[0]
        off, out = 2, {}
        for _ in range(count):
            item_size = body[off]
            bank_id = struct.unpack_from("<H", body, off + 1)[0]
            nl = body[off + 3]
            name = body[off + 4:off + 4 + nl].decode("ascii", "replace")
            out[name] = bank_id
            off += 1 + item_size
        return out

    def register_meta(self, memspace: int = MEMSPACE_MAIN, refresh: bool = False) -> dict[int, dict]:
        """Map register id -> {name, bits}. VICE assigns ids per emulator, so
        never hardcode them; look them up here."""
        if self._reg_meta_cache is not None and not refresh and memspace == MEMSPACE_MAIN:
            return self._reg_meta_cache
        body = self.request(CMD_REGISTERS_AVAILABLE, bytes([memspace])).body
        count = struct.unpack_from("<H", body, 0)[0]
        off, out = 2, {}
        for _ in range(count):
            item_size = body[off]
            reg_id, bits, nl = body[off + 1], body[off + 2], body[off + 3]
            name = body[off + 4:off + 4 + nl].decode("ascii", "replace")
            out[reg_id] = {"name": name, "bits": bits}
            off += 1 + item_size
        if memspace == MEMSPACE_MAIN:
            self._reg_meta_cache = out
        return out

    # -- registers ----------------------------------------------------------
    def _decode_registers(self, body: bytes, memspace: int) -> dict[str, int]:
        meta = self.register_meta(memspace)
        count = struct.unpack_from("<H", body, 0)[0]
        off, out = 2, {}
        for _ in range(count):
            item_size = body[off]
            reg_id = body[off + 1]
            value = struct.unpack_from("<H", body, off + 2)[0]
            name = meta.get(reg_id, {}).get("name", f"r{reg_id}")
            bits = meta.get(reg_id, {}).get("bits", 16)
            out[name] = value & ((1 << bits) - 1) if bits else value
            off += 1 + item_size
        return out

    def registers(self, memspace: int = MEMSPACE_MAIN) -> dict[str, int]:
        pkt = self.request(CMD_REGISTERS_GET, bytes([memspace]))
        return self._decode_registers(pkt.body, memspace)

    def set_registers(self, values: dict[str, int], memspace: int = MEMSPACE_MAIN) -> dict[str, int]:
        meta = self.register_meta(memspace)
        by_name = {info["name"].upper(): rid for rid, info in meta.items()}
        items = b""
        for name, value in values.items():
            rid = by_name.get(name.upper())
            if rid is None:
                raise ViceMonitorError(
                    f"unknown register {name!r}; available: {sorted(by_name)}"
                )
            items += struct.pack("<BBH", 3, rid, value & 0xFFFF)
        body = bytes([memspace]) + struct.pack("<H", len(values)) + items
        pkt = self.request(CMD_REGISTERS_SET, body)
        return self._decode_registers(pkt.body, memspace)

    # -- memory -------------------------------------------------------------
    def read_memory(self, start: int, length: int, *, memspace: int = MEMSPACE_MAIN,
                    bank: int = 0, side_effects: bool = False) -> bytes:
        """Read `length` bytes from `start`.

        side_effects=False is the safe default: reading an I/O register such as
        $D019 with side effects enabled can clear latched bits and change what
        the running program sees.
        """
        if length <= 0:
            return b""
        out = bytearray()
        while length > 0:
            # A single request cannot straddle the 16-bit address space.
            chunk = min(length, 0x10000 - (start & 0xFFFF))
            end = (start + chunk - 1) & 0xFFFF
            body = struct.pack("<BHHBH", 1 if side_effects else 0,
                               start & 0xFFFF, end, memspace, bank)
            resp = self.request(CMD_MEM_GET, body).body
            seg_len = struct.unpack_from("<H", resp, 0)[0]
            out += resp[2:2 + seg_len]
            start = (start + chunk) & 0xFFFF
            length -= chunk
        return bytes(out)

    def write_memory(self, start: int, data: bytes, *, memspace: int = MEMSPACE_MAIN,
                     bank: int = 0, side_effects: bool = False) -> int:
        data = bytes(data)
        if not data:
            return 0
        written, addr = 0, start & 0xFFFF
        while written < len(data):
            chunk = min(len(data) - written, 0x10000 - addr)
            payload = data[written:written + chunk]
            end = (addr + chunk - 1) & 0xFFFF
            body = struct.pack("<BHHBH", 1 if side_effects else 0,
                               addr, end, memspace, bank) + payload
            self.request(CMD_MEM_SET, body)
            written += chunk
            addr = (addr + chunk) & 0xFFFF
        return written

    # -- checkpoints --------------------------------------------------------
    def set_checkpoint(self, start: int, end: int | None = None, *,
                       stop_when_hit: bool = True, enabled: bool = True,
                       operation: int = OP_EXEC, temporary: bool = False,
                       memspace: int = MEMSPACE_MAIN) -> Checkpoint:
        end = start if end is None else end
        body = struct.pack("<HHBBBBB", start & 0xFFFF, end & 0xFFFF,
                           1 if stop_when_hit else 0, 1 if enabled else 0,
                           operation, 1 if temporary else 0, memspace)
        return _parse_checkpoint(self.request(CMD_CHECKPOINT_SET, body).body)

    def get_checkpoint(self, number: int) -> Checkpoint:
        return _parse_checkpoint(
            self.request(CMD_CHECKPOINT_GET, struct.pack("<I", number)).body
        )

    def delete_checkpoint(self, number: int) -> None:
        self.request(CMD_CHECKPOINT_DELETE, struct.pack("<I", number))

    def toggle_checkpoint(self, number: int, enabled: bool) -> None:
        self.request(CMD_CHECKPOINT_TOGGLE,
                     struct.pack("<IB", number, 1 if enabled else 0))

    def set_condition(self, number: int, expression: str) -> None:
        """Attach a monitor condition, e.g. 'A == $ff' or '@0:.X > 3'.

        The expression uses VICE text-monitor syntax and is sent as raw bytes.
        """
        raw = expression.encode("ascii", "replace")
        if len(raw) > 255:
            raise ViceMonitorError("condition expression longer than 255 bytes")
        self.request(CMD_CONDITION_SET,
                     struct.pack("<IB", number, len(raw)) + raw)

    def list_checkpoints(self) -> list[Checkpoint]:
        """Checkpoint list replies with one 0x11 per checkpoint, then a 0x14
        carrying the count -- so this collects rather than reading one packet."""
        req_id = self._send(CMD_CHECKPOINT_LIST)
        deadline = time.time() + self.timeout
        found: list[Checkpoint] = []
        while True:
            pkt = self._read_packet(deadline)
            if pkt.request_id == EVENT_REQUEST_ID:
                self._note_event(pkt)
                continue
            if pkt.request_id != req_id:
                continue
            if pkt.error != 0:
                raise ViceMonitorError(
                    f"checkpoint list failed: {ERROR_TEXT.get(pkt.error, 'unknown')}"
                )
            if pkt.type == RESP_CHECKPOINT_INFO:
                found.append(_parse_checkpoint(pkt.body))
                continue
            if pkt.type == CMD_CHECKPOINT_LIST:
                return found

    def delete_all_checkpoints(self) -> int:
        existing = self.list_checkpoints()
        for cp in existing:
            self.delete_checkpoint(cp.number)
        return len(existing)

    # -- execution control --------------------------------------------------
    def resume(self) -> None:
        """Leave the monitor and let the emulator run (binary command 'exit')."""
        self.request(CMD_EXIT)
        self.stopped = False

    def step(self, count: int = 1, step_over: bool = False) -> None:
        self.request(CMD_ADVANCE_INSTRUCTIONS,
                     struct.pack("<BH", 1 if step_over else 0, count))

    def step_out(self) -> None:
        """Run until the current subroutine returns."""
        self.request(CMD_EXECUTE_UNTIL_RETURN)

    def reset(self, mode: int = 0) -> None:
        """mode 0 = soft reset, 1 = power cycle, 8..11 = reset that drive."""
        self.request(CMD_RESET, bytes([mode]))

    def quit_vice(self) -> None:
        try:
            self.request(CMD_QUIT, timeout=2.0)
        except (ViceMonitorError, TimeoutError, OSError):
            pass  # VICE usually dies before it can answer

    def autostart(self, path: str, *, run: bool = True, index: int = 0) -> None:
        raw = str(path).encode("utf-8")
        if len(raw) > 255:
            raise ViceMonitorError("autostart path longer than 255 bytes")
        body = struct.pack("<BHB", 1 if run else 0, index, len(raw)) + raw
        self.request(CMD_AUTOSTART, body)

    # -- input --------------------------------------------------------------
    def keyboard_feed(self, text: str | bytes) -> None:
        """Push PETSCII bytes into the keyboard buffer (255 bytes max per call)."""
        raw = text if isinstance(text, (bytes, bytearray)) else ascii_to_petscii(text)
        raw = bytes(raw)
        for i in range(0, len(raw), 255):
            chunk = raw[i:i + 255]
            self.request(CMD_KEYBOARD_FEED, bytes([len(chunk)]) + chunk)

    # -- resources ----------------------------------------------------------
    def get_resource(self, name: str):
        raw = name.encode("ascii")
        body = self.request(CMD_RESOURCE_GET, bytes([len(raw)]) + raw).body
        rtype, vlen = body[0], body[1]
        value = body[2:2 + vlen]
        if rtype == 0x00:       # string
            return value.decode("utf-8", "replace")
        return int.from_bytes(value, "little")

    def set_resource(self, name: str, value) -> None:
        raw = name.encode("ascii")
        if isinstance(value, str):
            rtype, val = 0x00, value.encode("utf-8")
        else:
            rtype, val = 0x01, int(value).to_bytes(4, "little")
        body = (bytes([rtype, len(raw)]) + raw + bytes([len(val)]) + val)
        self.request(CMD_RESOURCE_SET, body)

    # -- convenience --------------------------------------------------------
    def screen_geometry(self) -> dict:
        """Resolve where screen RAM actually is, honouring VIC bank and $D018.

        Programs that relocate the screen (common in C and assembly) would
        otherwise be read from the wrong address.
        """
        d018 = self.read_memory(0xD018, 1)[0]
        dd00 = self.read_memory(0xDD00, 1)[0]
        vic_bank = (3 - (dd00 & 0x03)) * 0x4000
        screen = vic_bank + ((d018 >> 4) & 0x0F) * 0x0400
        return {
            "screen_base": screen,
            "vic_bank": vic_bank,
            "lowercase": bool(d018 & 0x02),
            "d018": d018,
            "dd00": dd00,
        }

    def read_screen(self, *, columns: int = 40, rows: int = 25,
                    base: int | None = None, lowercase: int | None = None) -> list[str]:
        geo = self.screen_geometry()
        base = geo["screen_base"] if base is None else base
        lower = geo["lowercase"] if lowercase is None else bool(lowercase)
        raw = self.read_memory(base, columns * rows)
        return [
            "".join(screencode_to_char(b, lower) for b in raw[r * columns:(r + 1) * columns])
            for r in range(rows)
        ]


# --- PETSCII / screen-code helpers ----------------------------------------

# Screen codes 0-31 are @A-Z[£]^< in the uppercase set; 32-63 match ASCII.
_SC_SYMBOLS = {0: "@", 27: "[", 28: "£", 29: "]", 30: "↑", 31: "←"}


def screencode_to_char(code: int, lowercase: bool = True) -> str:
    """Translate one screen-memory byte into a printable character.

    `lowercase` selects the mixed-case character set (the mode BASIC programs
    normally run in). Reverse-video codes (bit 7) are folded onto their normal
    counterparts so a reverse-printed line still reads as text.
    """
    code &= 0x7F  # fold reverse video
    if code in _SC_SYMBOLS:
        return _SC_SYMBOLS[code]
    if 1 <= code <= 26:
        return chr(ord("a") + code - 1) if lowercase else chr(ord("A") + code - 1)
    if 32 <= code <= 63:
        return chr(code)
    if 65 <= code <= 90 and lowercase:
        return chr(code)
    if code == 64:
        return "-"
    return "."   # graphics / unmapped


def ascii_to_petscii(text: str) -> bytes:
    """Convert host text to the PETSCII bytes the keyboard buffer expects.

    Unshifted C64 letters are PETSCII $41-$5A, which is where ASCII *uppercase*
    lives -- so BASIC keywords must be fed as uppercase. Lowercase ASCII
    ($61-$7A) is the shifted range and produces graphics characters instead.
    This function uppercases letters for you; pass bytes directly if you need
    exact control. '\\n' becomes CR ($0D), which is what RETURN sends.
    """
    out = bytearray()
    for ch in text:
        if ch == "\n" or ch == "\r":
            out.append(0x0D)
        elif "a" <= ch <= "z":
            out.append(ord(ch.upper()))
        elif ord(ch) < 0x100:
            out.append(ord(ch))
        else:
            out.append(0x3F)  # '?'
    return bytes(out)


def petscii_to_ascii(data: bytes) -> str:
    """Best-effort rendering of PETSCII bytes as host text."""
    out = []
    for b in data:
        if b == 0x0D:
            out.append("\n")
        elif 0x20 <= b <= 0x40 or b in (0x5B, 0x5D):
            out.append(chr(b))
        elif 0x41 <= b <= 0x5A:
            out.append(chr(b).lower())
        elif 0x61 <= b <= 0x7A:
            out.append(chr(b - 0x20))
        elif 0xC1 <= b <= 0xDA:
            out.append(chr(b - 0x80))
        else:
            out.append(".")
    return "".join(out)


if __name__ == "__main__":  # pragma: no cover - smoke check
    import json
    import sys

    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    with ViceMonitor(port=port) as mon:
        print(json.dumps({
            "vice": mon.vice_info(),
            "registers": mon.registers(),
            "banks": mon.banks(),
        }, indent=2))
