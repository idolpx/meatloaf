#!/usr/bin/env python3
"""
Toolchain discovery and building for the VICE debug skill.

Finds VICE (x64sc/x128/...), the cc65 suite, petcat and c1541 without
hardcoding paths, builds BASIC/C/assembly sources into .prg files, and parses
the VICE label files cc65's linker emits so breakpoints can be set by symbol
name instead of raw address.

Every discovered path can be overridden with an environment variable -- see
`discover()`.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

IS_WINDOWS = os.name == "nt"

# Emulator binary per machine, in the order we would guess for a bare "c64".
EMULATORS = {
    "c64": "x64sc",
    "c64sc": "x64sc",
    "c64fast": "x64",
    "c128": "x128",
    "vic20": "xvic",
    "plus4": "xplus4",
    "pet": "xpet",
    "scpu64": "xscpu64",
    "c64dtv": "x64dtv",
}

# Directories worth checking when a tool is not on PATH.
_EXTRA_DIRS = [
    r"C:\vice\bin", r"C:\vice",
    r"C:\Program Files\VICE\bin", r"C:\Program Files\VICE",
    r"C:\Program Files (x86)\VICE\bin", r"C:\Program Files (x86)\VICE",
    "/usr/bin", "/usr/local/bin", "/opt/homebrew/bin",
    "/Applications/vice-arm64-gtk3/bin", "/Applications/VICE.app/Contents/Resources/bin",
]


class ToolchainError(RuntimeError):
    pass


def _which(name: str) -> str | None:
    found = shutil.which(name)
    if found:
        return found
    exts = [".exe", ".bat", ".cmd"] if IS_WINDOWS else [""]
    for directory in _EXTRA_DIRS:
        d = Path(directory)
        if not d.is_dir():
            continue
        for ext in exts:
            candidate = d / (name + ext)
            if candidate.is_file():
                return str(candidate)
    # cc65 often lives in a user tools dir
    for base in (Path.home() / "Tools" / "cc65" / "bin",
                 Path.home() / "cc65" / "bin",
                 Path("C:/cc65/bin")):
        if base.is_dir():
            for ext in exts:
                candidate = base / (name + ext)
                if candidate.is_file():
                    return str(candidate)
    return None


@dataclass
class Toolchain:
    machine: str = "c64"
    emulator: str | None = None
    petcat: str | None = None
    c1541: str | None = None
    cl65: str | None = None
    ca65: str | None = None
    ld65: str | None = None
    bc64: str | None = None
    monitor_host: str = "127.0.0.1"
    monitor_port: int = 6502
    overrides: dict = field(default_factory=dict)

    def as_dict(self) -> dict:
        d = {k: v for k, v in self.__dict__.items()}
        d["basic_compiler"] = "bc64" if self.bc64 else ("petcat" if self.petcat else None)
        return d

    def require(self, attr: str, hint: str) -> str:
        value = getattr(self, attr)
        if not value:
            raise ToolchainError(f"{attr} not found. {hint}")
        return value


def discover(machine: str | None = None) -> Toolchain:
    """Locate every tool. Environment overrides, in priority order:

    VICE_MACHINE        c64 (default), c128, vic20, plus4, pet, scpu64, c64dtv
    VICE_EMULATOR       full path to x64sc/x128/... (skips lookup)
    VICE_BIN_DIR        directory to search first for VICE tools
    VICE_MONITOR_HOST   default 127.0.0.1
    VICE_MONITOR_PORT   default 6502
    CC65_BIN_DIR        directory holding cl65/ca65/ld65
    BC64                full path to the bc64 BASIC compiler
    """
    overrides = {k: v for k, v in os.environ.items()
                 if k in ("VICE_MACHINE", "VICE_EMULATOR", "VICE_BIN_DIR",
                          "VICE_MONITOR_HOST", "VICE_MONITOR_PORT",
                          "CC65_BIN_DIR", "BC64")}

    for env_dir in (os.environ.get("VICE_BIN_DIR"), os.environ.get("CC65_BIN_DIR")):
        if env_dir and env_dir not in _EXTRA_DIRS:
            _EXTRA_DIRS.insert(0, env_dir)

    machine = (machine or os.environ.get("VICE_MACHINE") or "c64").lower()
    emu_name = EMULATORS.get(machine)
    if emu_name is None:
        raise ToolchainError(
            f"unknown machine {machine!r}; expected one of {sorted(EMULATORS)}"
        )

    tc = Toolchain(machine=machine, overrides=overrides)
    tc.emulator = os.environ.get("VICE_EMULATOR") or _which(emu_name)
    tc.petcat = _which("petcat")
    tc.c1541 = _which("c1541")
    tc.cl65 = _which("cl65")
    tc.ca65 = _which("ca65")
    tc.ld65 = _which("ld65")
    tc.bc64 = os.environ.get("BC64") or _which("bc64")
    tc.monitor_host = os.environ.get("VICE_MONITOR_HOST", "127.0.0.1")
    tc.monitor_port = int(os.environ.get("VICE_MONITOR_PORT", "6502"))
    return tc


# --------------------------------------------------------------------------
# Building
# --------------------------------------------------------------------------

@dataclass
class BuildResult:
    source: str
    prg: str
    kind: str                       # basic | c | asm | prebuilt
    load_address: int
    size: int
    labels: str | None = None       # VICE label file, when the linker made one
    mapfile: str | None = None
    command: list = field(default_factory=list)

    def as_dict(self) -> dict:
        d = dict(self.__dict__)
        d["load_address_hex"] = f"${self.load_address:04x}"
        return d


def _run(cmd: list[str], cwd: str | None = None) -> str:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise ToolchainError(
            "command failed: " + " ".join(cmd) +
            f"\nexit {proc.returncode}\n{proc.stdout}\n{proc.stderr}"
        )
    return (proc.stdout or "") + (proc.stderr or "")


def prg_load_address(path: str | Path) -> int:
    with open(path, "rb") as fh:
        header = fh.read(2)
    if len(header) < 2:
        raise ToolchainError(f"{path} is too short to be a .prg")
    return header[0] | (header[1] << 8)


def build(source: str | Path, output: str | Path | None = None, *,
          toolchain: Toolchain | None = None, debug: bool = True,
          extra_flags: list[str] | None = None) -> BuildResult:
    """Compile one source file to a .prg.

    Dispatch is by extension:
      .bas        BASIC v2 -- bc64 if available, else VICE's petcat
      .c          cc65 (cl65 -t <machine>)
      .s .asm .a  ca65 via cl65, same driver, assembler sources
      .prg        already built; returned as-is
    """
    tc = toolchain or discover()
    src = Path(source).resolve()
    if not src.is_file():
        raise ToolchainError(f"source not found: {src}")
    extra = list(extra_flags or [])
    suffix = src.suffix.lower()
    out = Path(output).resolve() if output else src.with_suffix(".prg")
    out.parent.mkdir(parents=True, exist_ok=True)

    if suffix == ".prg":
        return BuildResult(str(src), str(src), "prebuilt",
                           prg_load_address(src), src.stat().st_size)

    if suffix == ".bas":
        return _build_basic(src, out, tc, extra)
    if suffix in (".c",):
        return _build_cc65(src, out, tc, "c", debug, extra)
    if suffix in (".s", ".asm", ".a65", ".src"):
        return _build_cc65(src, out, tc, "asm", debug, extra)
    raise ToolchainError(
        f"don't know how to build {src.name}; expected .bas, .c, .s/.asm or .prg"
    )


def _build_basic(src: Path, out: Path, tc: Toolchain, extra: list[str]) -> BuildResult:
    if tc.bc64:
        # bc64 supports labels, @alias long variable names and auto-numbering.
        cmd = [tc.bc64, "-a", "-o", str(out)] + extra + [str(src)]
    elif tc.petcat:
        # -w2 tokenises with BASIC v2 keywords; petcat writes the $0801 header.
        cmd = [tc.petcat, "-w2", "-o", str(out)] + extra + ["--", str(src)]
    else:
        raise ToolchainError(
            "no BASIC compiler found. Install bc64, or use VICE's petcat "
            "(it ships in the VICE bin directory)."
        )
    log = _run(cmd)
    if not out.is_file():
        raise ToolchainError(f"BASIC build produced no output\n{log}")
    return BuildResult(str(src), str(out), "basic",
                       prg_load_address(out), out.stat().st_size, command=cmd)


def _build_cc65(src: Path, out: Path, tc: Toolchain, kind: str,
                debug: bool, extra: list[str]) -> BuildResult:
    cl65 = tc.require("cl65", "Install cc65 and put its bin directory on PATH "
                              "or set CC65_BIN_DIR.")
    labels = out.with_suffix(".lbl")
    mapfile = out.with_suffix(".map")
    cmd = [cl65, "-t", tc.machine]
    if debug:
        # -g keeps debug info; no -O so variables survive for inspection.
        cmd += ["-g"]
    else:
        cmd += ["-Oirs"]
    if kind == "asm" and not any(f in ("-C", "--config") for f in extra):
        # A standalone assembly program has none of the C runtime's segments,
        # so the default c64.cfg fails to link ("Start address of memory area
        # 'BSS' is not constant"). cc65 ships <machine>-asm.cfg for exactly
        # this case; __LOADADDR__ must be forced in to emit the .prg header.
        cmd += ["-C", f"{tc.machine}-asm.cfg", "-u", "__LOADADDR__"]
    # -Ln writes a VICE label file: the bridge from source symbols to addresses.
    cmd += ["-Ln", str(labels), "--mapfile", str(mapfile)]
    cmd += extra
    cmd += ["-o", str(out), str(src)]
    log = _run(cmd, cwd=str(src.parent))
    if not out.is_file():
        raise ToolchainError(f"cc65 build produced no output\n{log}")
    return BuildResult(
        str(src), str(out), kind, prg_load_address(out), out.stat().st_size,
        labels=str(labels) if labels.is_file() else None,
        mapfile=str(mapfile) if mapfile.is_file() else None,
        command=cmd,
    )


# --------------------------------------------------------------------------
# Symbols
# --------------------------------------------------------------------------

_LABEL_RE = re.compile(r"^\s*al\s+([0-9A-Fa-f]{1,6})\s+\.?(\S+)", re.MULTILINE)


def load_labels(path: str | Path) -> dict[str, int]:
    """Parse a VICE label file (ld65 -Ln, or acme/64tass equivalents).

    Format is one `al <hexaddr> .<name>` per line -- the same file you would
    feed to VICE's text monitor with `ll`.
    """
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    out: dict[str, int] = {}
    for addr, name in _LABEL_RE.findall(text):
        out[name] = int(addr, 16)
    if not out:
        raise ToolchainError(f"no labels parsed from {path}")
    return out


def resolve_address(token: str, labels: dict[str, int] | None = None) -> int:
    """Turn '$c000', 'c000', '0xc000', '49152' or a symbol name into an int.

    Bare hex is the default because that is how C64 addresses are always
    written; use a leading '0d' or pass a decimal-looking value with '#'
    if you really mean decimal.
    """
    token = token.strip()
    if not token:
        raise ToolchainError("empty address")
    if labels:
        if token in labels:
            return labels[token]
        stripped = token.lstrip(".")
        if stripped in labels:
            return labels[stripped]
        if "_" + stripped in labels:      # cc65 prefixes C symbols with _
            return labels["_" + stripped]
    if token.startswith("#"):
        return int(token[1:], 10)
    if token.lower().startswith("0d"):
        return int(token[2:], 10)
    if token.startswith("$"):
        return int(token[1:], 16)
    if token.lower().startswith("0x"):
        return int(token[2:], 16)
    try:
        return int(token, 16)
    except ValueError:
        pass
    raise ToolchainError(
        f"cannot resolve {token!r} to an address "
        f"(not hex, and not a known label{' in the loaded label file' if labels else '; none loaded'})"
    )


def make_d64(prg_paths: list[str], image: str | Path, *,
             toolchain: Toolchain | None = None,
             disk_name: str = "debug", disk_id: str = "01") -> str:
    """Build a .d64 containing the given PRGs, via VICE's c1541.

    Useful when a program needs true drive emulation or loads extra files at
    runtime -- autostarting a bare .prg gives it no disk to read from.
    """
    tc = toolchain or discover()
    c1541 = tc.require("c1541", "c1541 ships with VICE; check VICE_BIN_DIR.")
    image = str(Path(image).resolve())
    cmd = [c1541, "-format", f"{disk_name},{disk_id}", "d64", image]
    for prg in prg_paths:
        p = Path(prg).resolve()
        cbm_name = p.stem.upper()[:16]
        cmd += ["-attach", image, "-write", str(p), cbm_name]
    _run(cmd)
    return image


if __name__ == "__main__":  # pragma: no cover
    tc = discover()
    print(json.dumps(tc.as_dict(), indent=2))
    if len(sys.argv) > 1:
        print(json.dumps(build(sys.argv[1], toolchain=tc).as_dict(), indent=2))
