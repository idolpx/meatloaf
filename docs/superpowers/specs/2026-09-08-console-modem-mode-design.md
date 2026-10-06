# Console modem mode — design

**Date:** 2026-09-08
**Status:** Approved for phase 1 implementation
**Reference:** `.reference/Zimodem/zimodem` (Zimodem 4.0, Bo Zimmerman, GPL)

## Summary

Add a Hayes-style virtual modem to the Meatloaf console. Typing `at` at the console
prompt switches that shell into modem mode, where AT commands dial outbound TCP,
telnet and SSH connections, accept inbound calls, and run file transfers against the
remote host. `AT+SHELL` returns to the normal Meatloaf prompt, leaving the modem and
its connections alive.

Zimodem is the functional reference. Its transport is a C64 on a serial line; ours is
the Meatloaf console (UART or TCP), so the far end is a PC terminal speaking ASCII/ANSI.
That single difference removes PETSCII translation, the `P` command modifiers, Comet64,
HostCM and the 1650/1660/1670 emulation modes from scope.

Nothing modem-related exists in the tree today. `lib/device/iec/modem.h` is a dead stub
commented out of `device.h`, and AGENTS.md's "WiFi modem for telnet BBS access" refers to
the `N:TELNET://` protocol handler, not Hayes emulation.

## Goals

- Dial outbound: raw TCP, telnet with option negotiation, SSH shell.
- Accept inbound: port listeners, RING, auto-answer.
- Hold several connections at once and switch between them from command mode.
- Run X/Y/ZMODEM, Kermit and Punter transfers between the remote host and Meatloaf's
  filesystem.
- Persist settings, S-registers and a phonebook.
- Reuse `MFile` / `MStream` / `MSession` for every connection and every transfer endpoint.

## Non-goals

- PETSCII translation and the `P` modifiers. The far end is a PC terminal.
- SLIP and PPP. Both need at least 57600 baud and a raw lwIP netif.
- Comet64 (CSIP), HostCM, and 1650/1660/1670 emulation.
- IPP printing (`AT+PRINT`), IRC (`AT+IRC`), and Zimodem's `zbrowser`.
- Terminal emulation. Data passes through untouched apart from telnet negotiation.
- A C64-facing front end over IEC or the userport. The engine is written so one could
  be added later, but `lib/bus/userport/serial.cpp` is a 783-byte stub and that work is
  out of scope here.

## Phasing

The approved scope is three to four implementation plans' worth of work. It ships as a
sequence, not a reduced feature set. Each phase is independently testable and useful.

| Phase | Contents |
|---|---|
| **1** | AT parser, settings + S-registers + `AT&W`, single dial-out, `+++`/`ATO`/`ATH`, result codes, telnet negotiation, phonebook |
| **2** | Connection registry — `ATC"host:port"`, `ATC0`, `ATCn`, `ATDn`, `ATHn`, backgrounding and backpressure |
| **3** | Listeners — `ATAn`, RING, `ATA`, `ATS0` auto-answer, `ATS41` auto-stream |
| **3.5** | Extract `SSHMSession` out of `SFTPMSession`. No modem code. Ships and is verified on its own |
| **4** | SSH dialing `ATDS"user:pass@host:port"` — the deep-stack tier |
| **5a–5d** | ZMODEM, XMODEM/YMODEM, Punter, Kermit — one plan and one build gate each |

This document specifies phase 1 in full and fixes the architecture every phase shares.
Later phases get their own spec against this architecture.

## Architecture

### Every endpoint is an MStream

The AT layer never touches a socket. A dial resolves a URL through `MFSOwner` and gets
back an `MStream`; a transfer moves bytes between two `MStream`s.

| Modem concept | Meatloaf class | Status |
|---|---|---|
| `ATD"host:port"` | `MFSOwner::File("tcp://host:port")->getSourceStream(in\|out)` | exists |
| `ATDT"host:port"` | `telnet://` — `TelnetMStream` decorating `TCPMStream`, running `lib/telnet/libtelnet.c` | fills the 3-line stub in `network/telnet.h` |
| `ATDS"user:pass@host:port"` | `ssh://` — `SSHMSession` extracted out of `SFTPMSession`, plus a shell-channel `SSHMStream` | fills the empty `network/ssh.h` (phases 3.5 and 4) |
| Connection lifetime | `MSession` + `SessionBroker` — refcounting, idle disposal, `addIO()`/`releaseIO()` | exists |
| Transfer file end | `MFSOwner::File("/sd/...")->getSourceStream(out)` | exists |
| Listener | `MeatSocketServer`, generalized off its `iecPort` assumption | partial (phase 3) |

`TelnetMStream` as a decorator over `TCPMStream` is the same pattern the media layer uses
for decoder streams. It also means `ATD`, `ATDT` and `ATDS` are one dial path with three
URL schemes rather than three code paths.

The payoff is largest for transfers. A ZMODEM engine written as `MStream* remote` to
`MStream* file` contains no socket, no filesystem and no console, so it can be driven
natively against a mock `MStream` scripted from a recorded peer exchange. For a protocol
of ZMODEM's complexity that is the difference between a testable component and
hardware-only guesswork.

### The SSH quartet, and where it comes from

`ssh://` needs all four classes, because `MFSOwner` resolves a scheme by asking each
registered `MFileSystem::handles()` and hands back an `MFile*`. Three of the four are
small or already written.

`SSHMFile` and `SSHMFileSystem` take the **`TCPMFile` shape, not the `SFTPMFile` shape**.
An SSH shell channel is not a filesystem, so there is no `isDirectory`, `rewindDirectory`,
`getNextFileInDir`, `mkDir`, `remove` or `rename` — `SSHMFile` is a connection handle
whose `getSourceStream()` returns `createStream()` and whose `getDecodedStream()` returns
its argument unwrapped. About 40 lines for the pair.

`SSHMStream` wraps one `ssh_channel` and is largely a port of `lib/network-protocol/SSH.cpp`
lines 177-280, which already runs the sequence: `ssh_channel_new`,
`ssh_channel_open_session`, `ssh_channel_request_pty_size`, `ssh_channel_request_shell`,
then `ssh_channel_read` / `ssh_channel_write`. `seek()` returns false — a shell has no
position — and `waitReadable()` maps to `ssh_channel_read_nonblocking`.

`SSHMSession` is **extracted from `SFTPMSession`, which then derives from it** (phase 3.5).
SFTP genuinely runs as a subsystem over an SSH connection — `sftp_new()` takes the
`ssh_session` — so the inheritance is not a convenience, it is the protocol relationship.
The SSH transport is moved rather than duplicated, so this makes phase 4 *smaller*, not
larger.

| Stays in `SFTPMSession` | Moves to `SSHMSession` |
|---|---|
| `sftp_handle`, `getSFTPSession()` | `ssh_handle`, `getSSHSession()` |
| `sftp_new` / `sftp_init` in a `connect()` override | `ssh_new`, options, `ssh_connect`, auth |
| `sftp_free` in a `disconnect()` override | `ssh_disconnect` / `ssh_free` |
| `getScheme()` returning `"sftp"` | `keep_alive()`, `setCredentials`, `setPrivateKey`, both auth methods |

The critical constraint: **`SSHMSession` is pure transport.** The pty-and-shell channel
lives in `SSHMStream`, never in the session. Putting it in the session would make every
SFTP mount open a shell channel it never uses and spawn a remote shell process. That split
is also the one the codebase already uses everywhere — session is the connection to the
server, stream is one open thing on it, exactly as `TCPMSession`/`TCPMStream` and
`SFTPMSession`/`SFTPMStream` are arranged.

Three details that will bite if left unstated, each of which this repo has hit before:

1. **`SSHMSession` needs a scheme-taking constructor.** `MSession::key` must be spelled
   exactly as `SessionBroker::obtain()` spells it. This is the `FTPMSession` precedent
   verbatim — its scheme-taking constructor exists for this reason, with `FTPSMSession`
   beside it so `obtain<T>()` keys the two apart. Static `getScheme()` hiding in
   `SFTPMSession` covers the rest.
2. **The base destructor must not call virtual `disconnect()`.** `MSession`'s own comment
   warns about this. Both `disconnect()` overrides call a non-virtual `closeSSH()` helper,
   and `~SSHMSession()` calls that helper directly.
3. **`connect()`'s stale-session check splits in two.** Today it tests
   `ssh_handle == nullptr || sftp_handle == nullptr || !ssh_is_connected(...)` as one
   condition. The base validates the transport; the override adds the subsystem check
   after calling it.

**Credentials in the dial URL are a documented trap.** `ATDS"user:pass@host:port"` carries
credentials in the URL, and AGENTS.md records that FTP credentials in a URL never reached
the client at all — the SessionBroker key did not carry them, so every connection silently
fell back to anonymous. The fix was `ftpSessionHost()` embedding `user[:password]@` in the
host key so the credentials reach the constructor before `connect()` runs, and so two users
of one server get two sessions. `SSHMSession` needs the same treatment, and it composes
with the per-connection-id keying described below.

### The modem task never touches a console fd

Two tasks on one socket is the condition behind the NFS `0x6400` heap corruption and the
reason `console.printf()` is banned from I/O hot paths. The ESC-cancel design note states
the invariant the existing code relies on: while a command runs, the shell task is blocked
inside `console.execute()` and is not reading the socket, so there is never a second
reader. A modem task writing to the console fd would break that.

```
 serial repl_task ──┐  owns stdin/stdout, sole reader and writer
                    ├── ModemPort { rx StreamBuffer, tx StreamBuffer } ──┐
 tcp session_task ──┘  owns client socket, sole reader and writer        │
                                                                         v
                                                              modem task (AT engine,
                                                              registry, listeners)
                                                                         │
                                          MStream per connection <───────┘
                                          (tcp:// telnet:// ssh://)
```

A shell in modem mode is a dumb pump: read a byte off its fd into `rx`, drain `tx` to its
fd. FreeRTOS StreamBuffers are single-writer/single-reader and need no mutex.

This also delivers the shared/observable behaviour for free. Command-mode responses are
written to every registered port's `tx`, so both consoles see the modem's state.
Stream-mode data goes only to the attached port's `tx`. The port that issued the dial
becomes the attached one.

### Two stack tiers

- **`modem`** — small, created once at boot when `ENABLE_MODEM` is set, whether or not
  anyone types `at`; it blocks on its input queue until then. Runs the AT parser, dial,
  the stream pump and listeners. Task stacks are internal-DRAM only with no PSRAM
  fallback and can fail from fragmentation, so the stack is claimed while contiguous
  internal RAM is still available rather than on first use. The cost of that choice is
  one idle task's stack on any board with the feature compiled in, which is why the
  feature is gated rather than always on.
- **`modem_work`** — deep, created on demand and held only while an SSH connection or a
  transfer is live. It copies `ensureExecTask()`'s choreography: delete before create,
  `vTaskDelay(20)` so the idle task reclaims the stack, and a fallback. When it cannot be
  created, `ATDS` and transfers answer `ERROR` while dial, stream and listen keep working.

Both sizes are **measured**, not estimated. The repo's reference high-water marks are
AFP 9332 bytes and an HTTPS GET including the TLS handshake at 4360.

### Three traps this reuse creates

1. **`SessionBroker` keys on `scheme://host:port`.** Two modem connections to the same
   host would silently share one socket. The connection id goes into the session key, the
   same way `ftpSessionHost()` embeds credentials so two users of one FTP server get two
   sessions.
2. **Keep-alive must be off** — `keep_alive_interval = 0` on every modem session. A
   keep-alive probe on a raw byte channel injects bytes into the user's session. Same
   reasoning as the NFS fix.
3. **`MStream` has no readiness concept.** The modem multiplexes console input against N
   connections. Add one virtual to the base class, `waitReadable(timeout_ms)`, defaulting
   to a polled `available()`. Reaching past `MStream` to the underlying fd would break the
   abstraction the whole design rests on.

### Entry and exit

`at` is intercepted as a **raw line** in `repl_task()` and `TCPServer::execute()` — the
same place `exit` and `reboot` are already intercepted, and before `esp_console_run()`'s
splitter. That splitter silently drops arguments past `CONSOLE_MAX_CMDLINE_ARGS` and only
strips a leading quote; `AT&S62=1DT"host:23"` would not survive it.

`AT+SHELL` returns to the normal Meatloaf prompt. The modem, its connections and its
listeners stay alive, which is what makes the shared/observable model meaningful.

## Phase 1 specification

### File layout

```
lib/modem/
  modem.h/.cpp         Modem class and global instance, task, state machine, port registry
  at_parser.h/.cpp     line -> command list. Pure string work, no I/O.
  at_settings.h/.cpp   S-registers, flags, phonebook, mlConfig load and save
  at_result.h/.cpp     result-code formatting
  modem_port.h         ModemPort: StreamBuffer pair and attach state
  escape.h/.cpp        +++ detector, injectable clock

lib/meatloaf/network/
  telnet.h/.cpp        TelnetMStream / TelnetMFile / TelnetMFileSystem, registered in MFSOwner
```

`lib/modem/` includes nothing from `lib/console` or `device/iec`. `lib/console` is not
compiled in the native environment, and that ban is what keeps the parser, settings,
result codes and escape detector host-testable. Any device-layer include is guarded
behind `TEST_NATIVE`, as `meat_media.h` already does.

### Command set

Dial and session:

| Command | Effect |
|---|---|
| `ATD"host:port"` | Dial raw TCP, enter stream mode |
| `ATDT"host:port"` | Dial telnet with option negotiation |
| `ATD<digits>` | Dial a phonebook entry |
| `ATH` | Hang up |
| `ATO` | Return online from ONLINE_COMMAND |
| `+++` | Escape to ONLINE_COMMAND, connection stays up |
| `ATZ` | Reset to saved configuration |
| `AT&F` | Restore factory defaults |
| `AT&W` | Save current settings |
| `A/` | Repeat the previous line |

Query: `ATI` banner, `ATI1` common settings, `ATI2` IP address, `ATI3` WiFi connection,
`ATI4` firmware version, `ATI5` all S-registers.

Phonebook: `ATP` list, `ATP<n>="host:port"[,modifiers]` store, `ATP<n>=` delete.

Settings: `ATE0/1` echo, `ATQ0/1` quiet, `ATV0/1` verbose or numeric, `ATX0-4` result
subset, `ATS<n>=<v>` set, `ATS<n>?` query.

Exit: `AT+SHELL`.

### State machine

```
        ATD/ATDT ──────────► DIALING ──CONNECT──► ONLINE
COMMAND <──────────────────────┘  timeout(S7)        │ +++  ^
   ^                            NO ANSWER            v      │ ATO
   │                                            ONLINE_CMD ─┘
   └────────── ATH / remote close (NO CARRIER) ──────┘
```

`+++` yields ONLINE_COMMAND with the connection still up, not OFFLINE. That distinction is
what makes `ATO` meaningful, and Zimodem does not draw it. `NO CARRIER` must fire in
ONLINE_COMMAND as well as ONLINE, so the modem task keeps watching a suspended connection.

### The escape detector

The rule is not "three plus signs". It is: at least S12 of silence, then exactly three S2
characters with less than S12 between each, then at least S12 of silence. S12 is in
fiftieths of a second, per Hayes convention, so the default of 50 is one second.

Until the trailing guard expires the three bytes are **held**. If more data arrives before
it expires they were ordinary data and must be forwarded intact — a user typing `+++`
inside a sentence must neither drop into command mode nor lose the characters.

The clock is injected, making this a pure function of (bytes, timestamps) and therefore
natively testable. That matters: escape-sequence defects are subtle and hardware-only
debugging of them is expensive.

### Result codes

The output is a five-way product: `V` (verbose or numeric) by `Q` (suppress) by `X`
(which subset — X0 emits a bare `CONNECT` and never `BUSY` or `NO DIALTONE`) by `S3` (CR
character) by `S4` (LF character). Implemented table-driven with native tests across the
combinations. Terminal programs do break on getting this wrong.

| Verbose | Numeric | Emitted when |
|---|---|---|
| `OK` | 0 | Command succeeded |
| `CONNECT` | 1 | Connection established |
| `RING` | 2 | Inbound call (phase 3) |
| `NO CARRIER` | 3 | Remote closed, or `ATH` |
| `ERROR` | 4 | Parse or command failure |
| `NO DIALTONE` | 6 | No network |
| `BUSY` | 7 | Dial while already connected |
| `NO ANSWER` | 8 | Connect timed out (S7) |

### Settings and persistence

A `uint8_t s[128]` array plus the flag bits, stored in `config.json` under a `modem` key.
`mlConfig.save()` detects dirtiness by hashing, so there are no flags to set — mutate
`mlConfig.data()` and call `save()`.

Phase-1 defaults: S0=0, S2=43, S3=13, S4=10, S5=8, S7=60, S12=50, S41=0, S62=0. S0, S41
and S62 are stored and reported in phase 1 and acted on in phase 3.

### Error handling

Commands within a line execute left to right and **stop at the first failure**, emitting
`ERROR`. This is real-modem behaviour and it prevents `AT&S62=1DT"bad"` from half-applying
silently.

- `NO ANSWER` when a connect exceeds S7 seconds.
- `BUSY` when dialing while already connected.
- `NO CARRIER` when the remote closes, from either ONLINE or ONLINE_COMMAND.
- `NO DIALTONE` when WiFi is down.

### Testing

Native suite `test/native/test_modem_at/`:

- Parser: multi-command lines (`AT&S62=1DT"host:23"`), quoting, `S<n>=<v>` and `S<n>?`
  forms, dial modifiers, `A/`, and malformed input.
- Result codes: the V/Q/X/S3/S4 product.
- Escape detection: scripted byte streams with timing, including the near-misses — three
  characters with no leading guard, and three characters followed immediately by data.
- Settings: round-trip through mlConfig.

Not reachable natively: the dial path, `TelnetMStream`, the port pumps and the task model.
`MFSOwner::File()` aborts under the native stubs and there are no sockets. These are
hardware-verified on `esp32-s3-devkitc-1`, and that limit is stated rather than implied.

Hardware verification for phase 1: dial a known telnet BBS, confirm negotiation leaves no
IAC bytes on screen, escape with `+++`, confirm the connection survives, `ATO` back,
`ATH`, and confirm `NO CARRIER`. Then `AT&W`, reboot, and confirm settings and phonebook
survive.

### Gating and budget

`ENABLE_MODEM` is opt-in and off by default, enabled first for `esp32-s3-devkitc-1`, which
has the large flash-text window. Each transfer protocol in phase 5 gets its own gate, so a
budget squeeze can drop Kermit (24 KB of Zimodem source, rare on BBSs) without losing
ZMODEM (55 KB, the one that matters).

Two rules from this repo's history apply:

- Measure with `xtensa-esp-elf-size -A` before trading any feature away. The fastloader
  work spent two rounds gating the wrong thing because source size is not flash text.
- Do not apply `#pragma GCC optimize("Os")` to this C++ code. It has made segments
  *bigger* three separate times here — fastloaders 629 to 977 bytes over, and again in the
  ARC and `-lh1-` work.

`fujiloaf-rev0` currently has 87,271 bytes of flash text free and roughly 4.8 KB of IRAM,
with the only IRAM lever already spent. Whether a WROVER board can carry this feature is a
measurement, not an assumption, and is answered per phase.

## Risks

- **Flash budget.** The full feature set will not fit on WROVER boards alongside the
  fastloaders. Mitigated by per-phase and per-protocol gates and by measuring each phase.
- **libssh stack depth (phase 4).** Unknown until measured. Mitigated by isolating SSH on
  `modem_work` so a failed allocation refuses one command instead of the feature.
- **Phase 3.5 edits working, hardware-verified SFTP code.** This is why it is its own
  phase rather than a step inside phase 4: it ships as one commit containing no modem code
  at all, and SFTP is re-verified on hardware — mount, list a directory, read a file
  byte-exact, and confirm session reuse and idle disposal — before anything builds on it.
  If SFTP regresses, the diff that caused it is small and isolated. `SFTPMSession` has
  callers beyond the modem, so the ripple check covers every one of them.
- **`MStream` readiness.** `waitReadable()` is a new virtual on a widely-derived base
  class. The default implementation polls `available()`, so no existing subclass changes
  behaviour.
- **Shared/observable fan-out.** Two consoles seeing one modem is a state-visibility
  feature, not a second control path for data. Exactly one port is attached for stream
  data at any time.
