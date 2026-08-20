# Console file I/O on the selected drive

Date: 2026-08-19
Branch: `console-file-io`

## Problem

`use <id>` picks a drive and `exec {DOS command}` sends it a command-channel
string, but there is no way to drive the *file* channels from the console. Every
test of a read or a write inside a disk image, an archive or a network path has
to be run from a real C64, which makes the IEC layer the slowest part of the
codebase to iterate on.

The console already reaches the same media through `cat`, `hex` and `cp`, but
those go through `MFSOwner` directly. They do not exercise `iecDrive::open()`,
the channel handlers, the CBM name parsing (`,S,R` / `@` / `0:`), or the status
reporting that a C64 actually sees — which is exactly the layer that breaks.

## Goal

Four console commands that drive the selected device's file channels the way a
C64 would, so a read or a write inside any mounted media can be exercised, and
its status read, without a Commodore attached.

## Commands

All operate on the device chosen by `use`. Channel numbers are secondary
addresses, 0-15, matching `OPEN <lfn>,<dev>,<sa>,"<name>"`.

| Command | Behavior |
|---|---|
| `open <ch> <name>` | Opens `name` on channel `ch`. The name is PETSCII-encoded by the same `encodeDosCommand()` that `exec` uses, so it is typed in lowercase and `0xNN` runs are available. `ch 15` is routed to `consoleExecDos()`, because that is what `OPEN 15,8,15,"I0"` does on a C64. Prints the resulting status. |
| `read <ch> [count]` | Reads in 255-byte chunks until the drive returns 0 (the documented end-of-file signal) or `count` bytes have been read. Each chunk is dumped as it arrives — offset, hex, ASCII — so nothing is accumulated and a 170 KB file costs no RAM. Prints the total and the status. |
| `write <ch> <data>` | Same encoding as `exec`. Sends in chunks of at most 255 bytes, with EOI set on the last one, and stops early on a short write (which the device contract defines as "cannot receive more data"). Prints bytes accepted and the status. |
| `close [ch]` | Closes one channel, or every open channel when no argument is given. Prints the status — a write error surfaces here, via `iecChannelHandlerFile`'s destructor. |

### Semantics deliberately preserved

- Channel 0 opens read-only (LOAD), channel 1 write (SAVE), 2-14 data, per
  `iecDrive::open()`'s existing mode selection. The console does not
  second-guess it.
- A read returning 0 on the **first** call after open is an error, not an empty
  file — that is the `IECFileDevice` contract, and it is what produces
  `62,FILE NOT FOUND`.
- Status is printed after every command, since half the value of simulating the
  C64 is seeing what the C64 would see on channel 15.

## Drive wrappers

`open`/`read`/`write`/`close` are protected virtuals on `iecDrive` and stay that
way. Five public wrappers are added, following `consoleSetCwd()` and
`consoleExecDos()`:

```cpp
bool        consoleOpen  (uint8_t ch, const std::string &name);
uint8_t     consoleRead  (uint8_t ch, uint8_t *buf, uint8_t len);
uint8_t     consoleWrite (uint8_t ch, const uint8_t *buf, uint8_t len, bool eoi);
void        consoleClose (uint8_t ch);
std::string consoleStatus(bool *isError = nullptr);
```

Each calls the **virtual** method, so device 30 still reaches `iecMeatloaf`'s
overrides.

`consoleStatus()` is the status-consuming tail of `consoleExecDos()` factored
out — peek the buffer, fall through to `getStatusData()` when empty, strip the
trailing CR. `consoleExecDos()` then calls it. The new commands need it because
`open`/`read`/`write`/`close` do not return a status string of their own.

## The `exec` busy guard is removed

`exec` currently refuses when `getNumOpenChannels() > 0`, on the grounds that a
C64 owns the drive while it has channels open. With console-opened channels that
would block `exec` immediately after the first `open`, and checking status
mid-transfer is most of the point.

The guard comes out. Stated plainly: the console task can now step on the IEC
task if a real C64 is mid-transfer on the same drive. That is the same exposure
`mount` and `partition` already carry.

## ESC cancel

A long `read` at 460800 baud floods the console with no way out. New shared
helper, `lib/console/console_cancel.h/.cpp`:

```cpp
namespace ESP32Console {
    void cancel_begin();      // clear the flag, drain stale input
    bool cancel_requested();  // non-blocking poll; true once ESC (0x1B) is seen
    void cancel_request();    // set the flag directly
}
```

Checked every 256 bytes by `read`, and by `cat` and `hex` in `VFSCommands.cpp`.
On cancel the command prints how far it got and returns; a channel opened by
`read` stays open.

Two transports, because the console has two:

- **Serial** — polls `stdin` with a zero timeout, the same
  `fcntl(O_NONBLOCK)` + `fgetc` idiom `console_read_byte()` in
  `XFERCommands.cpp` already uses from the executor task.
- **TCP** — `TCPServer::pollCancel()`, a non-blocking `recv(MSG_DONTWAIT)` on
  the client socket. This is safe from the executor task specifically because
  the session task is blocked inside `console.execute()` for the duration of the
  command and is not reading the socket itself.

`cancel_requested()` picks the transport from `console.execOrigin()`, so a
serial-origin command never drains an idle TCP client's socket.

**Accepted cost:** bytes that are not ESC are discarded by the poll, on both
transports. Type-ahead entered while a long read is running is lost. This is
already true of the serial `rx`/`tx` commands.

## Testability

`lib/console` is not compiled in the native test environment, so the two pieces
worth testing move into units that are:

- **`lib/console/dos_encode.h/.cpp`** — `encodeDosCommand()` lifted out of
  `IECCommands.cpp`. Pure, and the piece with a proven bug history.
- **`lib/console/dos_transfer.h`** — header-only templates, not `std::function`,
  so there is no firmware cost:

  ```cpp
  template<class Reader, class Sink, class Cancel>
  size_t dos_read_loop (size_t max_bytes, Reader, Sink, Cancel, bool *cancelled);
  template<class Writer, class Cancel>
  size_t dos_write_loop(const uint8_t*, size_t, Writer, Cancel, bool *cancelled);
  ```

  These hold the parts that are easy to get wrong: end-of-file on a zero
  return, first-read-zero meaning error rather than empty, the `count` cap,
  EOI set only on the final chunk, a short write stopping the transfer, and the
  cancel check landing on the 256-byte boundary.

### Tests

`test/native/test_console_dos/`:

| Area | Cases |
|---|---|
| encode | `0x000009` is three bytes · `0x000x030x01` is the same three · an odd trailing hex digit stays text · `0x00cd` swallows `cd` · lowercase text maps to `$41-$5A` · a line with no escape passes through |
| read loop | end of file stops it · `count` caps it · a first read of zero yields zero bytes and is flagged · cancel fires at 256 bytes and not at 255 · the sink sees correct running offsets across chunks |
| write loop | a full write · a short write stops early · EOI set only on the last chunk · cancel mid-transfer |

The channel layer itself cannot be tested off-device. It is verified on hardware
through the debug skill: `open 0 "$"` then `read 0`, a write and read-back of a
SEQ file inside a D64, `close`, and ESC during a long `read` on both serial and
TCP.

## Files

| File | Change |
|---|---|
| `lib/device/iec/drive.h` | five public `console*` wrappers |
| `lib/device/iec/drive.cpp` | their implementations; `consoleExecDos()` refactored onto `consoleStatus()` |
| `lib/console/dos_encode.h/.cpp` | new — `encodeDosCommand()` moved here |
| `lib/console/dos_transfer.h` | new — read/write loop templates |
| `lib/console/console_cancel.h/.cpp` | new — ESC poll, both transports |
| `lib/console/Commands/IECCommands.cpp` | four commands; busy guard removed; `printBytes()` extracted from `printStatus()` |
| `lib/console/Commands/IECCommands.h` | command getters |
| `lib/console/Commands/VFSCommands.cpp` | ESC check in `cat` and `hex` |
| `lib/console/Console.cpp` | register the four commands |
| `lib/server/tcpsvr.h/.cpp` | `pollCancel()` |
| `test/native/test_console_dos/` | new suite |
