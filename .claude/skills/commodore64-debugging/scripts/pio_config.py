#!/usr/bin/env python3
"""Resolve Meatloaf build settings from the project's platformio.ini.

Single source of truth for the active build environment, serial port and
baud rate. Nothing in this skill should hardcode those values — a user
who edits platformio.ini (the documented way to select a board and port)
must not also have to edit the skill.

Used by meatloaf_debug.py and serial_capture.py. Standalone — imports
nothing outside the standard library, so serial_capture.py stays
self-contained.

CLI:
    python3 pio_config.py            # resolved settings as JSON
"""

import configparser
import json
import os
import re
from pathlib import Path

INI_NAMES = ("platformio.ini", "platformio.ini.sample")

_VAR_RE = re.compile(r"\$\{([^}]+)\}")
_DEFINE_RE = re.compile(r"-D\s*([A-Za-z_][A-Za-z0-9_]*)(?:=(\S+))?")


# ── Project discovery ────────────────────────────────────────────────

def find_project_root(start=None):
    """Walk up from `start` (default: this file) looking for platformio.ini."""
    explicit = os.environ.get("MEATLOAF_DIR")
    if explicit:
        root = Path(explicit).expanduser()
        if any((root / n).exists() for n in INI_NAMES):
            return root

    p = Path(start or __file__).expanduser().resolve()
    for d in ([p] if p.is_dir() else []) + list(p.parents):
        if any((d / n).exists() for n in INI_NAMES):
            return d
    return None


# ── ini parsing ──────────────────────────────────────────────────────

def _read_ini(root):
    """Return (ConfigParser, path). Falls back to platformio.ini.sample."""
    for name in INI_NAMES:
        path = root / name
        if path.exists():
            cp = configparser.ConfigParser(
                inline_comment_prefixes=(";",),
                strict=False,
                interpolation=None,
            )
            cp.read(str(path))
            return cp, path
    return None, None


def _expand(cp, value, depth=0):
    """Resolve PlatformIO's ${section.key} interpolation."""
    if depth > 8 or "${" not in value:
        return value

    def sub(m):
        section, _, key = m.group(1).partition(".")
        if cp.has_option(section, key):
            return _expand(cp, cp.get(section, key), depth + 1)
        return m.group(0)

    return _VAR_RE.sub(sub, value)


def _get(cp, sections, key, default=""):
    """First section that defines `key` wins (most specific listed first)."""
    for section in sections:
        if cp.has_option(section, key):
            return _expand(cp, cp.get(section, key)).strip()
    return default


def _parse_defines(build_flags):
    """Extract active `-D NAME[=VALUE]` defines. Commented-out flags are
    already dropped by configparser, so what is left is what gets compiled."""
    defines = {}
    for m in _DEFINE_RE.finditer(build_flags):
        defines[m.group(1)] = (m.group(2) or "").strip('\\"')
    return defines


def _flash_size(root, board):
    """Flash size for a board, from boards/<board>.json when it is a
    project-local board definition."""
    if not board:
        return ""
    path = root / "boards" / f"{board}.json"
    if path.exists():
        try:
            data = json.loads(path.read_text())
            size = data.get("upload", {}).get("flash_size", "")
            return size.replace("MB", "m").lower()
        except (ValueError, OSError):
            pass
    m = re.search(r"(\d+)m\b", board.lower())
    return f"{m.group(1)}m" if m else ""


# ── Public API ───────────────────────────────────────────────────────

def load(start=None):
    """Resolved build settings. Every value comes from platformio.ini;
    missing keys yield empty strings rather than invented defaults."""
    root = find_project_root(start)
    settings = {
        "project_root": str(root) if root else "",
        "ini_path": "",
        "environment": "",
        "board": "",
        "mcu": "",
        "flash_size": "",
        "upload_port": "",
        "monitor_port": "",
        "upload_speed": "",
        "monitor_speed": "",
        "build_platform": "",
        "u64_ip_address": "",
        "defines": {},
        "verbose_flags": [],
    }
    if root is None:
        return settings

    cp, ini_path = _read_ini(root)
    if cp is None:
        return settings
    settings["ini_path"] = str(ini_path)

    # MEATLOAF_BUILD_ENV overrides here too, so board/flash_size/defines all
    # describe the environment that will actually be built.
    env = os.environ.get("MEATLOAF_BUILD_ENV", "").strip()
    if not env:
        env = _get(cp, ["meatloaf"], "environment")
    if not env:
        # [platformio] default_envs may list several; the first is active.
        default_envs = _get(cp, ["platformio"], "default_envs")
        env = re.split(r"[,\s]+", default_envs)[0] if default_envs else ""
    settings["environment"] = env

    # Env-specific section overrides the shared [env] section.
    sections = ([f"env:{env}"] if env else []) + ["env"]

    settings["board"] = _get(cp, [f"env:{env}"] if env else [], "board")
    settings["flash_size"] = _flash_size(root, settings["board"])
    settings["upload_port"] = _get(cp, sections, "upload_port")
    settings["monitor_port"] = _get(cp, sections, "monitor_port")
    settings["upload_speed"] = _get(cp, sections, "upload_speed")
    settings["monitor_speed"] = _get(cp, sections, "monitor_speed")
    settings["build_platform"] = _get(cp, ["meatloaf"], "build_platform")
    # Debug-tooling setting, not a firmware build flag.
    settings["u64_ip_address"] = _get(cp, ["meatloaf"], "u64_ip_address")

    defines = _parse_defines(_get(cp, sections, "build_flags"))
    settings["defines"] = defines
    settings["verbose_flags"] = sorted(
        k for k in defines
        if k.startswith("VERBOSE_") or k in ("DATA_STREAM", "DEBUG_TIMING")
    )

    board_json = root / "boards" / f"{settings['board']}.json"
    if board_json.exists():
        try:
            settings["mcu"] = json.loads(board_json.read_text()).get(
                "build", {}).get("mcu", "")
        except (ValueError, OSError):
            pass

    return settings


def serial_port(start=None):
    """Port to use for serial capture: monitor_port, else upload_port."""
    s = load(start)
    return s["monitor_port"] or s["upload_port"]


def monitor_speed(start=None):
    return load(start)["monitor_speed"]


if __name__ == "__main__":
    print(json.dumps(load(), indent=2))
