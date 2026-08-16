#!/usr/bin/env python3
"""
VICE debug driver -- CLI and importable automation for the unattended
C64 debug cycle: build -> launch -> run -> inspect -> fix -> repeat.

    python vice_debug.py config
    python vice_debug.py launch --warp
    python vice_debug.py cycle hello.bas
    python vice_debug.py brk .main --exec
    python vice_debug.py wait
    python vice_debug.py regs
    python vice_debug.py mem \\$0400 --len 40 --ascii

As a module:

    from vice_debug import ViceSession
    with ViceSession() as s:
        s.autostart("game.prg")
        print("\\n".join(s.screen()))

State (launched PID, loaded label file) lives in a small JSON file under the
system temp dir so separate CLI invocations share it.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vice_monitor import (  # noqa: E402
    MEMSPACE_MAIN, OP_EXEC, OP_LOAD, OP_STORE, ViceMonitor, ViceMonitorError,
    ViceNotRunning, ascii_to_petscii, petscii_to_ascii,
)
from vice_toolchain import (  # noqa: E402
    BuildResult, Toolchain, ToolchainError, build, discover, load_labels,
    make_d64, prg_load_address, resolve_address,
)

STATE_PATH = Path(tempfile.gettempdir()) / "vice_debug_state.json"


# --------------------------------------------------------------------------
# Persistent state shared between CLI invocations
# --------------------------------------------------------------------------

def read_state() -> dict:
    try:
        return json.loads(STATE_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def write_state(**updates) -> dict:
    state = read_state()
    state.update(updates)
    try:
        STATE_PATH.write_text(json.dumps(state, indent=2), encoding="utf-8")
    except OSError:
        pass
    return state


def current_labels() -> dict[str, int]:
    path = read_state().get("labels")
    if path and Path(path).is_file():
        try:
            return load_labels(path)
        except ToolchainError:
            return {}
    return {}


# --------------------------------------------------------------------------
# Session
# --------------------------------------------------------------------------

class ViceSession:
    """A monitor connection plus the toolchain and symbol table around it.

    IMPORTANT -- disconnecting while the emulator is halted at a checkpoint
    permanently kills VICE's monitor listener (see `close()`), so this class
    always resumes before it closes. Anything that must observe a halted
    machine has to happen on ONE session, which is what the `batch` command
    exists for.
    """

    def __init__(self, toolchain: Toolchain | None = None, *,
                 host: str | None = None, port: int | None = None,
                 timeout: float = 15.0, connect: bool = True,
                 shared: bool = False):
        self.tc = toolchain or discover()
        self.mon = ViceMonitor(
            host or self.tc.monitor_host,
            port or self.tc.monitor_port,
            timeout=timeout,
        )
        self.labels: dict[str, int] = current_labels()
        self.shared = shared
        if connect:
            self.mon.connect(retries=2)

    def close(self) -> None:
        """Resume the emulator, then drop the connection.

        VICE 3.10 tears down its binary monitor listener if a client
        disconnects while the machine is halted -- the emulator keeps running
        but becomes permanently unreachable until it is restarted. Resuming
        first is what keeps the listener alive.
        """
        try:
            if self.mon.sock is not None and self.mon.stopped:
                self.mon.resume()
        except (ViceMonitorError, OSError):
            pass
        self.mon.close()

    def __enter__(self) -> "ViceSession":
        if self.mon.sock is None:
            self.mon.connect(retries=2)
        return self

    def __exit__(self, *exc) -> None:
        if not self.shared:
            self.close()

    # -- symbols ------------------------------------------------------------
    def use_labels(self, path: str | Path) -> int:
        self.labels = load_labels(path)
        write_state(labels=str(Path(path).resolve()))
        return len(self.labels)

    def addr(self, token: str | int) -> int:
        if isinstance(token, int):
            return token
        return resolve_address(token, self.labels)

    # -- program deployment -------------------------------------------------
    def autostart(self, path: str | Path, run: bool = True, index: int = 0) -> None:
        """Hand the file to VICE's own autostart (handles PRG, D64, T64, CRT).

        This is the reliable way to start a program: VICE injects or attaches
        as appropriate, sets up true drive emulation, and types RUN itself.
        """
        self.mon.autostart(str(Path(path).resolve()), run=run, index=index)

    def load_prg(self, path: str | Path, *, address: int | None = None,
                 fix_basic_pointers: bool | None = None) -> dict:
        """Write a .prg into memory manually, without running it.

        Use this (rather than autostart) when you need the code resident so you
        can set checkpoints and poke memory before the first instruction runs.

        The 2-byte little-endian load address header is stripped; the body goes
        to that address unless `address` overrides it. For a BASIC program the
        pointers at $2D/$2F/$31 must follow the program end or RUN sees a
        zero-length program -- that is what `fix_basic_pointers` does, and it
        defaults to on only when loading to the BASIC start ($0801).
        """
        raw = Path(path).read_bytes()
        if len(raw) < 3:
            raise ToolchainError(f"{path} is not a usable .prg")
        native = raw[0] | (raw[1] << 8)
        target = native if address is None else address
        body = raw[2:]
        self.mon.write_memory(target, body)
        end = target + len(body)
        if fix_basic_pointers is None:
            fix_basic_pointers = (target == 0x0801)
        if fix_basic_pointers:
            ptr = (end).to_bytes(2, "little")
            # VARTAB / ARYTAB / STREND all sit just past the program text.
            self.mon.write_memory(0x2D, ptr + ptr + ptr)
        return {
            "path": str(path), "load_address": target, "native_address": native,
            "size": len(body), "end": end, "basic_pointers_fixed": fix_basic_pointers,
        }

    def build(self, source: str | Path, output: str | Path | None = None,
              **kw) -> BuildResult:
        result = build(source, output, toolchain=self.tc, **kw)
        if result.labels:
            try:
                self.use_labels(result.labels)
            except ToolchainError:
                pass
        return result

    # -- interaction --------------------------------------------------------
    def type_text(self, text: str, press_return: bool = True) -> None:
        payload = text + ("\r" if press_return else "")
        self.mon.keyboard_feed(payload)

    def screen(self) -> list[str]:
        return self.mon.read_screen()

    def screen_text(self, strip: bool = True) -> str:
        rows = self.screen()
        if strip:
            rows = [r.rstrip() for r in rows]
        return "\n".join(rows)

    # -- checkpoints --------------------------------------------------------
    def brk(self, where: str | int, end: str | int | None = None, *,
            operation: int = OP_EXEC, temporary: bool = False,
            stop: bool = True, condition: str | None = None):
        start = self.addr(where)
        stop_addr = self.addr(end) if end is not None else start
        cp = self.mon.set_checkpoint(start, stop_addr, operation=operation,
                                     temporary=temporary, stop_when_hit=stop)
        if condition:
            self.mon.set_condition(cp.number, condition)
            cp = self.mon.get_checkpoint(cp.number)
        return cp

    # -- run control --------------------------------------------------------
    def run_and_wait(self, timeout: float = 30.0):
        """Resume, then block until something stops the emulator."""
        self.mon.resume()
        return self.mon.wait_for_stop(timeout)

    def where(self) -> dict:
        regs = self.mon.registers()
        pc = regs.get("PC", self.mon.last_pc)
        out = {"registers": regs, "pc": pc}
        if pc is not None:
            out["pc_hex"] = f"${pc:04x}"
            match = [n for n, a in self.labels.items() if a == pc]
            if match:
                out["symbol"] = match[0]
        return out


# --------------------------------------------------------------------------
# Launching VICE
# --------------------------------------------------------------------------

def port_open(host: str, port: int, timeout: float = 0.4) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def launch_vice(tc: Toolchain, *, prg: str | None = None, warp: bool = False,
                run: bool = True, extra: list[str] | None = None,
                wait: float = 25.0) -> dict:
    """Start the emulator with the binary monitor enabled and wait for it."""
    emulator = tc.require(
        "emulator",
        f"Install VICE, or set VICE_EMULATOR / VICE_BIN_DIR "
        f"(looking for {os.path.basename(str(tc.emulator or ''))or 'x64sc'})."
    )
    if port_open(tc.monitor_host, tc.monitor_port):
        return {"status": "already-running", "host": tc.monitor_host,
                "port": tc.monitor_port}

    cmd = [
        emulator,
        "-binarymonitor",
        "-binarymonitoraddress", f"ip4://{tc.monitor_host}:{tc.monitor_port}",
    ]
    if warp:
        cmd += ["-warp"]
    if prg:
        cmd += ["-autostart" if run else "-autoload", str(Path(prg).resolve())]
    cmd += list(extra or [])

    # VICE aborts during startup (Windows exit code 0xe0464645) if it cannot
    # bind the monitor address. The usual cause is a previous instance that was
    # force-killed moments ago and whose socket has not been released yet, so a
    # single immediate exit is worth retrying rather than reporting as fatal.
    attempts = 2
    for attempt in range(attempts):
        proc = _spawn(cmd)
        deadline = time.time() + wait
        while time.time() < deadline:
            if port_open(tc.monitor_host, tc.monitor_port):
                write_state(pid=proc.pid, emulator=emulator,
                            port=tc.monitor_port, host=tc.monitor_host)
                return {"status": "started", "pid": proc.pid, "command": cmd,
                        "host": tc.monitor_host, "port": tc.monitor_port,
                        "attempts": attempt + 1}
            if proc.poll() is not None:
                break
            time.sleep(0.3)
        else:
            raise ToolchainError(
                f"VICE started (pid {proc.pid}) but the binary monitor never "
                f"opened on {tc.monitor_host}:{tc.monitor_port} within {wait}s"
            )
        if attempt + 1 < attempts:
            time.sleep(2.0)   # let the old socket drain, then try once more

    raise ToolchainError(
        f"VICE exited during startup (code 0x{proc.returncode & 0xFFFFFFFF:08x}). "
        f"The most likely cause is that {tc.monitor_host}:{tc.monitor_port} is "
        f"still held by a previous emulator -- check with "
        f"'netstat -ano | findstr {tc.monitor_port}', or pick another port with "
        f"--port / VICE_MONITOR_PORT. Command: "
        + " ".join(shlex.quote(c) for c in cmd)
    )


def _spawn(cmd: list[str]) -> subprocess.Popen:
    """Start the emulator detached so it outlives this CLI invocation."""
    creation = 0
    if os.name == "nt":
        creation = subprocess.CREATE_NEW_PROCESS_GROUP | getattr(
            subprocess, "DETACHED_PROCESS", 0)
    return subprocess.Popen(
        cmd, creationflags=creation,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=(os.name != "nt"),
    )


def kill_vice(tc: Toolchain) -> dict:
    """Ask VICE to quit over the monitor, then fall back to killing the pid."""
    result = {"quit_sent": False, "killed": False}
    if port_open(tc.monitor_host, tc.monitor_port):
        try:
            mon = ViceMonitor(tc.monitor_host, tc.monitor_port, timeout=3.0)
            mon.connect()
            mon.quit_vice()
            mon.close()
            result["quit_sent"] = True
        except (ViceMonitorError, OSError):
            pass
    deadline = time.time() + 5
    while time.time() < deadline and port_open(tc.monitor_host, tc.monitor_port):
        time.sleep(0.25)
    if port_open(tc.monitor_host, tc.monitor_port):
        pid = read_state().get("pid")
        if pid:
            try:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(pid), "/F"],
                                   capture_output=True)
                else:
                    os.kill(int(pid), 9)
                result["killed"] = True
            except (OSError, ValueError):
                pass
    # Wait for the listening socket to actually go away. Relaunching while it
    # lingers makes the next VICE abort at startup.
    deadline = time.time() + 5
    while time.time() < deadline and port_open(tc.monitor_host, tc.monitor_port):
        time.sleep(0.25)
    result["port_released"] = not port_open(tc.monitor_host, tc.monitor_port)
    write_state(pid=None)
    return result


# --------------------------------------------------------------------------
# Output helpers
# --------------------------------------------------------------------------

def hexdump(data: bytes, base: int = 0, width: int = 16) -> str:
    lines = []
    for off in range(0, len(data), width):
        chunk = data[off:off + width]
        hexes = " ".join(f"{b:02x}" for b in chunk).ljust(width * 3 - 1)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"${base + off:04x}  {hexes}  {text}")
    return "\n".join(lines)


def print_screen(rows: list[str]) -> None:
    width = len(rows[0]) if rows else 40
    print("+" + "-" * width + "+")
    for row in rows:
        print("|" + row + "|")
    print("+" + "-" * width + "+")


def print_json(obj) -> None:
    print(json.dumps(obj, indent=2, default=str))


def split_command(line: str) -> list[str]:
    """Split a batch line into argv, honouring quotes but NOT backslashes.

    Plain shlex.split() treats '\\' as an escape, which silently destroys
    Windows paths ('C:\\Users\\x' -> 'C:Usersx'). File paths are far more
    common in these lines than escape sequences, so escaping is disabled.
    """
    lexer = shlex.shlex(line, posix=True)
    lexer.whitespace_split = True
    lexer.escape = ""
    return list(lexer)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

_SHARED_SESSION: ViceSession | None = None


def _session(args) -> ViceSession:
    """Return the process-wide session, creating it on first use.

    One connection per process -- never one per command -- because a
    disconnect while halted kills VICE's listener. `batch` relies on this to
    keep a checkpoint stop observable across several operations.
    """
    global _SHARED_SESSION
    if _SHARED_SESSION is not None:
        return _SHARED_SESSION
    tc = discover(getattr(args, "machine", None))
    if getattr(args, "port", None):
        tc.monitor_port = args.port
    try:
        _SHARED_SESSION = ViceSession(tc, shared=True)
    except ViceNotRunning as exc:
        raise SystemExit(f"error: {exc}")
    return _SHARED_SESSION


def _close_shared_session() -> None:
    global _SHARED_SESSION
    if _SHARED_SESSION is not None:
        _SHARED_SESSION.close()
        _SHARED_SESSION = None


def flush_keyboard(args, settle: float) -> ViceSession:
    """Force pending keyboard-feed text to actually reach the emulated C64.

    VICE 3.10 holds fed keystrokes for as long as a binary monitor client is
    connected -- polling the screen for 20s while connected shows nothing, and
    the text appears the moment the client disconnects. So the only way to feed
    keys and then observe the result in one run is to drop the connection,
    wait, and reconnect.
    """
    _close_shared_session()
    time.sleep(max(settle, 0.5))
    return _session(args)


def cmd_config(args) -> int:
    tc = discover(args.machine)
    info = tc.as_dict()
    info["monitor_reachable"] = port_open(tc.monitor_host, tc.monitor_port)
    info["state_file"] = str(STATE_PATH)
    info["state"] = read_state()
    print_json(info)
    return 0


def cmd_launch(args) -> int:
    tc = discover(args.machine)
    if args.port:
        tc.monitor_port = args.port
    prg = args.prg
    if prg and Path(prg).suffix.lower() not in (".prg", ".d64", ".t64", ".crt", ".tap"):
        prg = build(prg, toolchain=tc).prg
    print_json(launch_vice(tc, prg=prg, warp=args.warp, run=not args.no_run,
                           extra=args.extra or []))
    return 0


def cmd_kill(args) -> int:
    print_json(kill_vice(discover(args.machine)))
    return 0


def cmd_ping(args) -> int:
    with _session(args) as s:
        s.mon.ping()
        print_json({"ping": "ok", "vice": s.mon.vice_info()})
    return 0


def cmd_info(args) -> int:
    with _session(args) as s:
        print_json({
            "vice": s.mon.vice_info(),
            "banks": s.mon.banks(),
            "registers": s.mon.registers(),
            "register_meta": {v["name"]: {"id": k, "bits": v["bits"]}
                              for k, v in s.mon.register_meta().items()},
            "screen": s.mon.screen_geometry(),
        })
    return 0


def cmd_build(args) -> int:
    tc = discover(args.machine)
    result = build(args.source, args.output, toolchain=tc,
                   debug=not args.release, extra_flags=args.extra or [])
    if result.labels:
        write_state(labels=result.labels)
    print_json(result.as_dict())
    return 0


def cmd_autostart(args) -> int:
    with _session(args) as s:
        path = args.file
        if Path(path).suffix.lower() not in (".prg", ".d64", ".t64", ".crt", ".tap"):
            built = s.build(path)
            path = built.prg
            print_json(built.as_dict())
        s.autostart(path, run=not args.no_run)
        if args.wait:
            time.sleep(args.wait)
            print_screen(s.screen())
    return 0


def cmd_load(args) -> int:
    s = _session(args)
    path = args.file
    if Path(path).suffix.lower() != ".prg":
        path = s.build(path).prg
    addr = s.addr(args.address) if args.address else None
    info = s.load_prg(path, address=addr,
                      fix_basic_pointers=(True if args.basic else
                                          False if args.no_basic else None))
    print_json(info)
    if args.run:
        s.type_text("RUN")
        s = flush_keyboard(args, args.wait)
        if args.wait:
            print_screen(s.screen())
    return 0


def cmd_type(args) -> int:
    s = _session(args)
    s.type_text(" ".join(args.text), press_return=not args.no_return)
    if args.wait:
        # Keys only reach the C64 once we let go of the monitor connection.
        s = flush_keyboard(args, args.wait)
        print_screen(s.screen())
    return 0


def cmd_screen(args) -> int:
    with _session(args) as s:
        if args.raw:
            geo = s.mon.screen_geometry()
            data = s.mon.read_memory(geo["screen_base"], 1000)
            print(hexdump(data, geo["screen_base"]))
        else:
            rows = s.screen()
            if args.json:
                print_json({"rows": rows, "geometry": s.mon.screen_geometry()})
            else:
                print_screen(rows)
    return 0


def cmd_regs(args) -> int:
    with _session(args) as s:
        if args.set:
            values = {}
            for item in args.set:
                name, _, value = item.partition("=")
                if not _:
                    raise SystemExit(f"error: --set expects NAME=VALUE, got {item!r}")
                values[name] = s.addr(value)
            print_json(s.mon.set_registers(values))
        else:
            print_json(s.where())
    return 0


def cmd_mem(args) -> int:
    with _session(args) as s:
        start = s.addr(args.address)
        data = s.mon.read_memory(start, args.len, side_effects=args.side_effects)
        if args.raw:
            sys.stdout.buffer.write(data)
        elif args.ascii:
            print(petscii_to_ascii(data))
        else:
            print(hexdump(data, start))
    return 0


def cmd_poke(args) -> int:
    with _session(args) as s:
        start = s.addr(args.address)
        if args.file:
            data = Path(args.file).read_bytes()
        elif args.text is not None:
            data = ascii_to_petscii(args.text)
        else:
            data = bytes(int(v, 16) if not v.startswith("#") else int(v[1:])
                         for v in args.bytes)
        written = s.mon.write_memory(start, data)
        print_json({"address": f"${start:04x}", "written": written})
    return 0


def cmd_brk(args) -> int:
    with _session(args) as s:
        op = 0
        if args.load:
            op |= OP_LOAD
        if args.store:
            op |= OP_STORE
        if args.exec_ or op == 0:
            op |= OP_EXEC
        cp = s.brk(args.address, args.end, operation=op, temporary=args.temp,
                   stop=not args.trace, condition=args.cond)
        print(cp.describe())
        print_json(cp.__dict__)
    return 0


def cmd_brks(args) -> int:
    with _session(args) as s:
        cps = s.mon.list_checkpoints()
        if not cps:
            print("no checkpoints set")
        for cp in cps:
            print(cp.describe())
    return 0


def cmd_delbrk(args) -> int:
    with _session(args) as s:
        if args.all:
            print_json({"deleted": s.mon.delete_all_checkpoints()})
        else:
            for number in args.numbers:
                s.mon.delete_checkpoint(int(number))
            print_json({"deleted": len(args.numbers)})
    return 0


def cmd_step(args) -> int:
    with _session(args) as s:
        s.mon.step(args.count, step_over=args.over)
        time.sleep(0.1)
        s.mon.pump(0.2)
        print_json(s.where())
    return 0


def cmd_ret(args) -> int:
    with _session(args) as s:
        s.mon.step_out()
        s.mon.wait_for_stop(args.timeout)
        print_json(s.where())
    return 0


def cmd_cont(args) -> int:
    with _session(args) as s:
        s.mon.resume()
        print_json({"resumed": True})
    return 0


def cmd_wait(args) -> int:
    with _session(args) as s:
        event = s.mon.wait_for_stop(args.timeout)
        if event is None:
            print_json({"stopped": False,
                        "note": f"still running after {args.timeout}s"})
            return 1
        info = s.where()
        info["event"] = event.name
        hit = [e for e in s.mon.events if e.name == "checkpoint"]
        if hit:
            from vice_monitor import _parse_checkpoint
            info["checkpoint"] = _parse_checkpoint(hit[-1].body).describe()
        print_json(info)
        if args.screen:
            print_screen(s.screen())
    return 0


def cmd_reset(args) -> int:
    with _session(args) as s:
        s.mon.reset(1 if args.hard else 0)
        time.sleep(args.wait)
        print_json({"reset": "hard" if args.hard else "soft"})
        if args.screen:
            print_screen(s.screen())
    return 0


def cmd_labels(args) -> int:
    """Purely host-side: no emulator connection needed to load a symbol table."""
    if args.file:
        labels = load_labels(args.file)
        write_state(labels=str(Path(args.file).resolve()))
        print_json({"loaded": len(labels), "file": args.file})
    else:
        labels = current_labels()
        print_json({"file": read_state().get("labels"),
                    "count": len(labels),
                    "labels": {k: f"${v:04x}" for k, v in sorted(
                        labels.items(), key=lambda kv: kv[1])}})
    return 0


def cmd_cycle(args) -> int:
    """build -> (launch if needed) -> reset -> autostart -> wait -> screen."""
    tc = discover(args.machine)
    if args.port:
        tc.monitor_port = args.port
    steps = {}
    result = build(args.source, toolchain=tc, debug=not args.release)
    steps["build"] = result.as_dict()
    if result.labels:
        write_state(labels=result.labels)

    if not port_open(tc.monitor_host, tc.monitor_port):
        steps["launch"] = launch_vice(tc, warp=args.warp)

    s = _session(args)          # shared, so `cycle` composes inside `batch`
    if not args.no_reset:
        s.mon.reset(0)
        time.sleep(1.5)
    s.mon.delete_all_checkpoints()
    s.autostart(result.prg, run=not args.no_run)
    time.sleep(args.wait)
    events = s.mon.pump(0.1)
    rows = s.screen()
    # Report what VICE actually told us. There is no "am I halted" query, and
    # autostart emits stop/resume events of its own, so a derived flag here
    # would be guesswork.
    steps["last_event"] = events[-1].name if events else None
    steps["registers"] = s.mon.registers()
    print_json(steps)
    print_screen(rows)
    return 0


def cmd_d64(args) -> int:
    tc = discover(args.machine)
    prgs = []
    for src in args.sources:
        prgs.append(src if Path(src).suffix.lower() == ".prg"
                    else build(src, toolchain=tc).prg)
    image = make_d64(prgs, args.output, toolchain=tc, disk_name=args.name)
    print_json({"image": image, "files": prgs})
    return 0


def cmd_batch(args) -> int:
    """Run several subcommands over ONE monitor connection.

        vice_debug.py batch "brk _bump" "autostart hello.prg" wait \\
                            regs "mem _counter -n 1" screen

    This is the only safe way to look at a halted machine from the CLI: each
    separate process would have to disconnect, and disconnecting while halted
    kills VICE's monitor listener. The session resumes automatically at the
    end of the batch.
    """
    lines: list[str] = []
    if args.file:
        for raw in Path(args.file).read_text(encoding="utf-8").splitlines():
            raw = raw.strip()
            if raw and not raw.startswith("#"):
                lines.append(raw)
    lines.extend(args.commands or [])
    if not lines:
        print("error: no commands given", file=sys.stderr)
        return 2

    parser = build_parser()
    status = 0
    for line in lines:
        argv = split_command(line)
        if not argv:
            continue
        if argv[0] == "batch":
            print("error: batch cannot nest", file=sys.stderr)
            return 2
        print(f"\n$ {line}")
        try:
            sub = parser.parse_args(argv)
        except SystemExit:
            status = 2
            if args.stop_on_error:
                return status
            continue
        try:
            rc = sub.func(sub)
        except (ToolchainError, ViceMonitorError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            rc = 2
        if rc:
            status = rc
            if args.stop_on_error:
                return status
    return status


def cmd_diag(args) -> int:
    tc = discover(args.machine)
    out = {"toolchain": tc.as_dict(),
           "monitor_reachable": port_open(tc.monitor_host, tc.monitor_port)}
    missing = [k for k in ("emulator", "cl65", "petcat") if not getattr(tc, k)]
    out["missing_tools"] = missing
    if out["monitor_reachable"]:
        try:
            with ViceSession(tc) as s:
                out["vice"] = s.mon.vice_info()
                out["registers"] = s.mon.registers()
                out["checkpoints"] = [c.describe() for c in s.mon.list_checkpoints()]
                out["screen_geometry"] = s.mon.screen_geometry()
                out["screen_preview"] = " | ".join(
                    r.strip() for r in s.screen() if r.strip())[:200]
        except (ViceMonitorError, OSError) as exc:
            out["error"] = str(exc)
    else:
        out["hint"] = ("VICE is not listening. Run: python vice_debug.py launch")
    print_json(out)
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="vice_debug.py",
        description="Drive VICE's binary monitor for unattended C64 debugging.")
    p.add_argument("--machine", help="c64 (default), c128, vic20, plus4, pet")
    p.add_argument("--port", type=int, help="binary monitor port (default 6502)")
    sub = p.add_subparsers(dest="command", required=True)

    def add(name, fn, help_text):
        sp = sub.add_parser(name, help=help_text)
        sp.set_defaults(func=fn)
        return sp

    add("config", cmd_config, "show discovered tools and monitor state")
    add("diag", cmd_diag, "full health check of toolchain and emulator")

    sp = add("launch", cmd_launch, "start VICE with the binary monitor enabled")
    sp.add_argument("--prg", help="file to autostart (source files are built first)")
    sp.add_argument("--warp", action="store_true", help="run at maximum speed")
    sp.add_argument("--no-run", action="store_true", help="load but do not RUN")
    sp.add_argument("--extra", nargs=argparse.REMAINDER,
                    help="extra arguments passed straight to VICE")

    add("kill", cmd_kill, "quit the running emulator")
    add("ping", cmd_ping, "check the monitor connection")
    add("info", cmd_info, "registers, banks, screen geometry, VICE version")

    sp = add("build", cmd_build, "compile .bas/.c/.s to .prg")
    sp.add_argument("source")
    sp.add_argument("-o", "--output")
    sp.add_argument("--release", action="store_true", help="optimise instead of -g")
    sp.add_argument("--extra", nargs=argparse.REMAINDER)

    sp = add("autostart", cmd_autostart, "let VICE load and RUN a file")
    sp.add_argument("file")
    sp.add_argument("--no-run", action="store_true")
    sp.add_argument("--wait", type=float, default=0.0, help="seconds, then show screen")

    sp = add("load", cmd_load, "write a .prg into RAM without running it")
    sp.add_argument("file")
    sp.add_argument("-a", "--address", help="override the load address")
    sp.add_argument("--run", action="store_true", help="type RUN afterwards")
    sp.add_argument("--basic", action="store_true", help="force BASIC pointer fixup")
    sp.add_argument("--no-basic", action="store_true", help="never touch $2D-$32")
    sp.add_argument("--wait", type=float, default=3.0,
                    help="seconds to let RUN take effect, then show the screen")

    sp = add("type", cmd_type, "feed text to the keyboard buffer")
    sp.add_argument("text", nargs="+")
    sp.add_argument("--no-return", action="store_true")
    sp.add_argument("--wait", type=float, default=0.0)

    sp = add("screen", cmd_screen, "read and decode screen memory")
    sp.add_argument("--raw", action="store_true", help="hex dump of screen RAM")
    sp.add_argument("--json", action="store_true")

    sp = add("regs", cmd_regs, "read or write CPU registers")
    sp.add_argument("--set", nargs="+", metavar="NAME=VALUE")

    sp = add("mem", cmd_mem, "read memory")
    sp.add_argument("address")
    sp.add_argument("-n", "--len", type=int, default=256)
    sp.add_argument("--raw", action="store_true", help="binary to stdout")
    sp.add_argument("--ascii", action="store_true", help="decode as PETSCII text")
    sp.add_argument("--side-effects", action="store_true",
                    help="allow I/O reads to have side effects (default off)")

    sp = add("poke", cmd_poke, "write memory")
    sp.add_argument("address")
    sp.add_argument("bytes", nargs="*", help="hex byte values")
    sp.add_argument("--file", help="write the contents of this file instead")
    sp.add_argument("--text", help="write this string as PETSCII")

    sp = add("brk", cmd_brk, "set a checkpoint (breakpoint/watchpoint)")
    sp.add_argument("address", help="hex address or a symbol from the label file")
    sp.add_argument("end", nargs="?", help="end of an address range")
    sp.add_argument("--exec", dest="exec_", action="store_true", help="break on execute (default)")
    sp.add_argument("--load", action="store_true", help="break on read")
    sp.add_argument("--store", action="store_true", help="break on write")
    sp.add_argument("--temp", action="store_true", help="delete after first hit")
    sp.add_argument("--trace", action="store_true", help="log hits without stopping")
    sp.add_argument("--cond", help="VICE condition expression, e.g. 'A == $ff'")

    add("brks", cmd_brks, "list checkpoints")

    sp = add("delbrk", cmd_delbrk, "delete checkpoints")
    sp.add_argument("numbers", nargs="*")
    sp.add_argument("--all", action="store_true")

    sp = add("step", cmd_step, "advance N instructions")
    sp.add_argument("count", nargs="?", type=int, default=1)
    sp.add_argument("--over", action="store_true", help="step over JSR")

    sp = add("ret", cmd_ret, "run until the current subroutine returns")
    sp.add_argument("--timeout", type=float, default=15.0)

    add("cont", cmd_cont, "resume execution")

    sp = add("wait", cmd_wait, "block until the emulator stops")
    sp.add_argument("--timeout", type=float, default=30.0)
    sp.add_argument("--screen", action="store_true")

    sp = add("reset", cmd_reset, "reset the machine")
    sp.add_argument("--hard", action="store_true", help="power cycle")
    sp.add_argument("--wait", type=float, default=2.0)
    sp.add_argument("--screen", action="store_true")

    sp = add("labels", cmd_labels, "load or show the VICE label file")
    sp.add_argument("file", nargs="?")

    sp = add("cycle", cmd_cycle, "build, launch if needed, run, show the screen")
    sp.add_argument("source")
    sp.add_argument("--wait", type=float, default=3.0)
    sp.add_argument("--warp", action="store_true")
    sp.add_argument("--no-reset", action="store_true")
    sp.add_argument("--no-run", action="store_true")
    sp.add_argument("--release", action="store_true")

    sp = add("d64", cmd_d64, "build a .d64 image containing the given programs")
    sp.add_argument("sources", nargs="+")
    sp.add_argument("-o", "--output", required=True)
    sp.add_argument("--name", default="debug")

    sp = add("batch", cmd_batch,
             "run several subcommands on one connection (use this at a breakpoint)")
    sp.add_argument("commands", nargs="*", help="quoted subcommand lines")
    sp.add_argument("-f", "--file", help="read subcommands from a file, one per line")
    sp.add_argument("--stop-on-error", action="store_true")

    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (ToolchainError, ViceMonitorError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130
    finally:
        # Always leave the emulator running: a disconnect while halted would
        # cost VICE its monitor listener for the rest of its life.
        _close_shared_session()


if __name__ == "__main__":
    raise SystemExit(main())
