# Changing the serial baud rate from the console and from modem mode

Date: 2026-09-18
Status: design approved, not implemented

## Problem

There is no way to change the serial rate at runtime. It is fixed at build time
by `DEBUG_SPEED`, which `platformio.ini` ties to `monitor_speed` (2000000), and
which `main_setup()` passes to `console.begin()` at `src/main.cpp:193`.

Two different users need it to change.

A terminal or a real Commodore talking to **modem mode** runs at 300-19200 baud.
It cannot talk to a port fixed at 2 Mbps, so the feature is unreachable for the
hardware it exists to serve.

A **debug console** on a board whose USB-serial bridge cannot reach 2 Mbps has
the same problem from the other end: AGENTS.md already records that a CP2102
maxes out near 1 Mbps and such boards need 921600.

## What the hardware actually allows

The console is not a UART on every board. Surveyed across all 28 board
sdkconfigs:

| console transport | boards | has a baud rate |
|---|---|---|
| `CONFIG_ESP_CONSOLE_UART_CUSTOM` (UART0) | 22 | yes |
| `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` | esp32-s3-devkitc-1, esp32-s3-makemagazin, freenove-esp32-s3-wroom-1, pocket-dongle-s3 | no |
| `CONFIG_ESP_CONSOLE_USB_CDC` | esp32-s3-super-mini | no |

On the five USB boards `CONFIG_ESP_CONSOLE_UART_NUM` is -1 and there is no UART
to configure. A rate set there is discarded by the USB stack, which is exactly
why a pyserial session opens freenove-esp32-s3-wroom-1 at 2000000 and works: the
number is never used.

**The feature must therefore refuse on those boards rather than appear to
succeed.** A setting that accepts a value and reports it back while doing
nothing is the failure mode the S7 finding in AGENTS.md already established as
worse than a refusal.

## Decisions

**One rate, shared by both modes.** There is one physical UART, so a separate
"console rate" and "modem rate" would be a fiction. Whichever mode sets it, both
use it.

**Applied immediately, and persisted.** Changing a rate that cannot be tested
until the next reboot is not usable for tuning a terminal. The usual objection --
locking yourself out -- does not apply here, because the TCP console on port 23
and WebDAV editing of config.json both remain reachable over WiFi.

**Any rate in 300-4000000.** A range check only; the UART driver selects the
closest achievable divisor. A typo is recoverable over the TCP console, so
refusing a legitimate odd rate costs more than accepting a silly one.

**Both commands persist immediately rather than waiting for `AT&W`.** This is
inconsistent with how the modem's other settings work, and it is deliberate: a
rate change whose result cannot be observed without a second command is worse
than an inconsistency.

## Architecture

### Single source of truth

`preferences.baud` in config.json, an integer. Zero means "use the build's
`DEBUG_SPEED`", which is what an existing installation has and what a fresh one
gets.

### lib/console/console_baud.h / .cpp (new)

| function | contract |
|---|---|
| `bool consoleBaudSupported()` | Compile-time. True only when `CONFIG_ESP_CONSOLE_UART_DEFAULT` or `CONFIG_ESP_CONSOLE_UART_CUSTOM` is defined. |
| `int consoleBaudGet()` | The rate now in effect, read from `uart_get_baudrate()` rather than a shadow copy that could desync. |
| `esp_err_t consoleBaudSet(int baud)` | Validate the range, then `fflush(stdout)`, `fsync`, `uart_wait_tx_done()`, `uart_set_baudrate()`. **Only on success** write `preferences.baud` and `mlConfig.save()`. On failure restore the previous rate and persist nothing. |
| `void consoleBaudSetPending(int baud)` | Validate and record a pending rate. Touches neither the UART nor the config. |
| `void consoleBaudApplyPending()` | If a rate is pending, call `consoleBaudSet()` on it and clear it. |

**Apply before persisting, never the other way round.** `uart_set_baudrate()`
returns an `esp_err_t`. Persisting first means a rate the driver rejects is
still the rate the next boot reads, which is the one failure this design must
not create.

`mlConfig.save()` is called directly rather than through a new helper, because
that is exactly what `Modem::saveConfig()` already does for `AT&W` and that path
is hardware-proven to survive a power cycle.

### Why there are two apply paths, not one

This is the load-bearing part of the design, and the reason the obvious
implementation is wrong. **The two modes genuinely differ and a single call site
cannot serve both.**

**Shell mode has no pump.** `console_repl` sits in `readLine()`, and while a
command runs it is blocked inside `console.execute()`. There is no loop polling
anything, so a pending flag set by `baud 9600` would never be applied. The
notice is written by `console_exec` -- the same task running the command -- so
that command applies the change **inline, at its end**, and needs no pending
state at all.

**Modem mode does have a pump**, `modem_shell_pump()` at `Console.cpp:124`,
whose loop pops from `ModemPort::tx_` and writes to the fd. `AT+IPR` runs on the
modem task, whose `OK` is still in `tx_` when the handler returns. That path
records a pending rate and the pump applies it after its write, which is the
only point at which the reply is known to be on the wire.

So: `baud` calls `consoleBaudSet()`; `AT+IPR=` calls `consoleBaudSetPending()`
and the pump calls `consoleBaudApplyPending()`.

`lib/modem/modem_port.h` documents that **the modem task must never touch a
console file descriptor** -- two tasks on one fd is the condition behind the NFS
0x6400 heap corruption, and the ESC-cancel design depends on the shell task
being the only reader. So the modem task pushes its bytes into a `ModemPort`
StreamBuffer and the shell task drains them to the fd afterwards.

An `AT+IPR=2400` handler that called `uart_set_baudrate()` itself would switch
the line while its own `OK` was still sitting in `tx_`, unsent. The reply would
go out at the new rate and arrive as garbage -- the user would see a failure and
would not know whether the command had taken effect. Deferring to the pump is
what avoids that.

The two entry points differ, but they converge on one function: both
`consoleBaudSet()` and `consoleBaudApplyPending()` end in the same validate,
drain, switch, persist sequence, so there is still exactly one piece of code
that reconfigures the UART.

`uart_wait_tx_done()` is required as well as `fflush`/`fsync`: flushing moves
bytes into the driver, not onto the wire, and the last character would otherwise
be clocked out at the new rate.

### Shell command: `baud`

- `baud` -- prints the rate in effect and whether this board supports changing it.
- `baud <n>` -- validates, prints its notice **at the old rate** so the user can
  read it, then calls `consoleBaudSet()` inline.
- On a USB board -- prints `this console is USB-Serial-JTAG, it has no baud rate`
  and returns non-zero.

**The wording depends on `console.execOrigin()`, and so does the urgency.** From
a serial console the caller's own link is the one being retuned, so the notice
is `console baud 2000000 -> 9600, reconnect now`. From the TCP console on port
23 the reply never crosses the UART at all -- there is nothing to drain and
nothing to reconnect -- so it reads `serial console baud 2000000 -> 9600`. This
matters more than cosmetics: **the TCP console is the documented recovery route,
so it has to be correct there**, and telling a remote caller to "reconnect now"
would be simply false.

Registered with the other core commands.

### Modem command: AT+IPR=<rate> and AT+IPR?

`+IPR` is the standard DTE-rate command (3GPP TS 27.007) and fits the grammar
`at_parser.cpp` already has for `AT+SHELL`. Zimodem's `AT$SB` is the other
convention in this space, but it would require a new `$` prefix in the parser
for no gain.

**A parser extension is needed.** The `+` branch at `at_parser.cpp:148` reads the
name and then continues, so it accepts no argument at all: `AT+IPR=9600` fails
the whole line at the `=`. The branch gains the `=<number>` and `?` forms,
reusing the existing `read_number()` and setting the `assign`, `value` and
`query` fields the `AtCommand` struct already carries for S-registers.
`AT+SHELL` keeps parsing exactly as it does now.

On a USB board `AT+IPR` answers `ERROR`.

### Boot

The persisted rate is applied immediately after `mlConfig.load()`
(`src/main.cpp:298`), not at `console.begin()` (`src/main.cpp:193`). Config lives
on flash or SD and those are not mounted until line 267, so it cannot be read any
earlier without new plumbing.

**The consequence is a feature, not a compromise: the boot log up to that point
is always emitted at `DEBUG_SPEED`.** A wrong persisted rate can therefore never
hide early boot, which is the last-resort recovery path on a board with no
network. The cost is that a terminal set to a slow rate sees roughly a second of
garbage before the switch.

### Nothing else configures UART0 on a console build

Worth stating because it looks like a conflict and is not. `FN_UART_DEBUG` is
`UART_NUM_0` -- the same peripheral the console uses -- and `main.cpp:208` calls
`Serial.begin(DEBUG_SPEED)`. But that line sits in the `#else` of
`#ifdef ENABLE_CONSOLE`, so it never runs on a build that has a console, and
`include/debug.h:27` maps `Serial` to `console` on those builds anyway.
`fnUartDebug` owns UART0 only on builds with no console -- which are exactly the
builds with no `baud` command. There is one owner of the rate in every
configuration.

## Recovery

In order of convenience: the TCP console on port 23; WebDAV editing of
config.json; reading the boot log at `DEBUG_SPEED` before the switch happens.

## Testing

**Native** (`test/native/test_modem_at`), covering the parser change only, since
that is the part with no ESP-IDF dependency:

- `AT+IPR=9600` parses with `assign` true and `value` 9600
- `AT+IPR?` parses with `query` true
- `AT+IPR` bare still parses, as `AT+SHELL` does
- `AT+IPR=` and `AT+IPR=abc` are rejected, with the error position reported
- `AT+SHELL` is unchanged, and `ATE0+IPR=9600` applies both commands
- Mutation check: the new cases must fail if the `=`/`?` handling is removed

There is no native coverage of `console_baud` itself: `lib/console` is not
compiled in the native environment, and the apply path needs a real UART. That
half is hardware-verified only, exactly as the console file-channel work is.

Both boards were connected on 2026-09-18 and the two legs can run back to back:

| port | board | console | what it proves |
|---|---|---|---|
| COM13 | lolin-d32-pro | UART0 via a CH340 bridge (1A86:7523) | the rate really changes |
| COM12 | freenove-esp32-s3-wroom-1 | USB-Serial-JTAG (303A:1001) | it refuses where there is no UART |

Note the CH340 on COM13 is itself an example of the problem: that bridge is
marginal at 2 Mbps, which is the case the feature exists to relieve. Note also
that DTR/RTS auto-reset behaves normally on COM13 and does NOT on COM12 -- see
the native-USB entry in AGENTS.md before driving either.

**Hardware, on a UART board** -- lolin-d32-pro on COM13. This cannot be verified
on freenove-esp32-s3-wroom-1, which has no UART console:

- `baud` reports the current rate
- `baud 9600` prints its notice at 2000000, and the prompt is readable after
  reconnecting at 9600
- `at` then `AT+IPR?` reports 9600; `AT+IPR=2400` switches again and the modem
  answers at 2400
- the rate survives a reboot, with the boot log at `DEBUG_SPEED` before the switch
- a dial still works at the new rate, proving the modem path is intact
- `baud 50` and `baud 9000000` are refused
- the TCP console on port 23 can still set the rate back

**Hardware, on freenove-esp32-s3-wroom-1 (COM12)** -- `baud` and `AT+IPR` both refuse
with the USB message, and nothing else regresses.

## Out of scope

- A second, independent rate for modem mode. One UART, one rate.
- Data bits, parity and stop bits. Nothing has asked for 7E1, and the CBM world
  is 8N1.
- Flow control. `AT&K` is already accepted and ignored, and a socket needs none.
- `CONNECT` carrying a speed, which AGENTS.md lists as its own open finding.
