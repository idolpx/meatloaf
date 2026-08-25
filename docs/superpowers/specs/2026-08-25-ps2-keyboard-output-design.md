# PS/2 Keyboard Output — Design

**Date:** 2026-08-25
**Status:** Approved, not implemented
**Scope:** PS/2 keyboard output driven from the console and the web server.
Bluetooth keyboard input is explicitly **out of scope** and gets its own cycle.

## Purpose

Let Meatloaf act as a PS/2 keyboard, sending keystrokes to an attached host on
demand. The host in the target setup is a **Commodore DTV64, which decodes PS/2
scancodes itself** — there is no intermediate PC-to-C64 converter, so all
scancode-to-C64-key mapping happens on the DTV side and Meatloaf only has to put
correct set-2 scancodes on the wire.

Commands arrive from the serial or TCP console, and — because
`WsCommandExecutor::dispatch` already falls through to `console.execute()` — from
the WebSocket with no new web code.

## Non-goals

- Bluetooth HID input. Deferred. Note `PS2Keyboard::keyHid_send()` already maps
  HID usage codes to PS/2 scancodes with no `esp_hid` dependency, so the later
  phase is mostly sourcing HID codes and calling it.
- PS/2 mouse. `ps2_mouse.*` stays in the tree untouched and unreferenced.
- A REST endpoint or a web keyboard UI. WebSocket only.
- Reconstructing C64 key semantics. The DTV owns that mapping.

## Approach

Keep the vendored `components/ps2/` component and patch it; add a thin
Meatloaf-side device class. This is the established vendor-and-patch pattern
(tapclean, libarchive), not a rewrite. The timing-critical bit-banging,
`reply_to_host`, `scan_codes_set_2.h` and `type()` all stay in the component.

## File layout

```
components/ps2/                   [vendored, patched — see Patches]
  src/ps2_device.{h,cpp}          P1, P4, P6, A2, A3, A4, A6, interrupt support
  src/ps2_keyboard.{h,cpp}        P4, P6, A1, A5, A8, interrupt task
  src/scan_codes_set_2.h          untouched — scancodes::Key is the vocabulary
  src/ps2_mouse.{h,cpp}           untouched, unreferenced, zero flash
  src/ps2keyboard.{h,cpp}         DELETED (P2)
  CMakeLists.txt                  P3

lib/device/ps2/ps2.{h,cpp}                 [new] PS2KeyboardDevice + global ps2Keyboard
lib/device/ps2/ps2_keynames.{h,cpp}        [new] key-name table, natively testable
lib/console/dos_encode.{h,cpp}             [mod] encodeAsciiCommand — shares the
                                                 0xNN escape walk, no PETSCII
lib/console/Commands/PS2Commands.{h,cpp}   [new] thin `ps2` command front-end

include/pinmap_defaults.h                  [fix] DELETE the PS/2 block entirely
include/pinmap/adafruit_feather_esp32s3_tft.h  [fix] drop its GPIO_NUM_NC PS/2 lines
platformio.ini.sample                      [fix] remove the ENABLE_PS2 flag
src/main.cpp                      global `keyboard` object removed; ps2Keyboard.start()
src/CMakeLists.txt                add lib/device/ps2 to INCLUDES and SOURCES
lib/console/Console.cpp           registerPS2Commands()
lib/device/iec/meatloaf.h         reloadAllConfig() calls ps2Keyboard.reloadConfig()
test/native/test_ps2_keys/        [new] native suite for the pure logic
```

## Build guard

The guard is **`PIN_KB_CLK`**. `ENABLE_PS2` is removed entirely — a board that
wires the pins has the hardware, and that is the whole condition. This is exactly
the `PIN_TFT_MOSI` pattern in `lib/display/lcd.h`.

Making it work requires removing the PS/2 fallback so the pin is defined only
where the hardware exists — `PIN_TFT_MOSI` appears in no pinmap default, which is
why its guard works:

- **`include/pinmap_defaults.h`: delete the PS/2 block.** It currently defines
  `PIN_KB_CLK`/`PIN_KB_DATA` as `GPIO_NUM_NC` for every board, which would make
  `#ifdef PIN_KB_CLK` universally true. Deleting it also removes the trailing-
  semicolon bug (`#define PIN_KB_CLK GPIO_NUM_NC;`) rather than fixing it.
- **`include/pinmap/adafruit_feather_esp32s3_tft.h`: drop its two
  `PIN_KB_CLK`/`PIN_KB_DATA GPIO_NUM_NC` lines**, which would otherwise satisfy
  the `#ifdef` on a board with no PS/2 hardware.
- **`platformio.ini.sample`: remove the commented `-D ENABLE_PS2` flag.**

`lib/device/ps2/ps2.h` then puts the real class inside `#ifdef PIN_KB_CLK` and a
no-op class with identical signatures in the `#else`, so call sites compile on all
boards with no `#ifdef` at the call site. No `#error` guard is needed: after the
above, defined implies wired.

Boards that get PS/2 compiled in: `fujiloaf-rev0` (GPIO 16/17) and
`esp32-s3-super-mini` (16/17). Note this is a semantic change from the previous
opt-in flag — both boards now build it unconditionally, matching how a board with
`PIN_TFT_MOSI` always builds HAGL. Runtime cost stays zero until
`devices.ps2.enabled` is set, since `start()` allocates nothing.

## Component patches

### P1 — eliminate the Arduino shims

`ps2_device.h` carries a block of Arduino-compatibility functions defined
**non-`inline` in a header** included by three translation units — duplicate
symbols at link. (It has gone unnoticed because nothing references `ps2dev::`
today, so archive semantics pull no object out of `libps2.a`.)

They are **deleted**, not `inline`d, and replaced with the ESP-IDF calls they were
imitating. This removes the link failure at the root and drops a class of 32-bit
wraparound bug with it.

| Shim | Replacement | Sites |
|---|---|---|
| `delayMicroseconds(us)` | `esp_rom_delay_us(us)` (`esp_rom_sys.h`) | 41 |
| `delay(ms)` | `vTaskDelay(...)` — see the tick warning below | 21 |
| `millis()` | `esp_timer_get_time() / 1000` | 2 |
| `micros()` | `esp_timer_get_time()` — `int64_t` us | 2 |
| `xTaskCreateUniversal(...)` | `xTaskCreatePinnedToCore(...)` | 3 |
| `HIGH` / `LOW` | literal `1` / `0` | 6 |
| `NOP()` | deleted — only the old `delayMicroseconds` used it | 1 |

**`esp_rom_delay_us()` is strictly better than what it replaces.** The shim spins
on `esp_timer_get_time()` cast to a 32-bit `unsigned long`, which wraps every
~71.6 minutes; its `if (m > e)` overflow branch is a partial mitigation that still
fails when the target crosses the wrap. `esp_rom_delay_us` is a calibrated
cycle-count wait with no such failure, and it is IRAM-resident and safe inside a
critical section — which matters because A3 keeps per-bit critical sections and
every one of those 41 calls runs inside one.

**Tick warning — a naive `delay(n)` → `vTaskDelay(pdMS_TO_TICKS(n))` substitution
preserves the A1 bug.** At `CONFIG_FREERTOS_HZ=100`, `pdMS_TO_TICKS(9)` is
`(9 * 100) / 1000` = **0**, exactly like the shim's integer division. The sub-tick
problem belongs to the 10 ms tick, not to the shim. Therefore:

- the 9 ms poll site is **deleted** by P7 (interrupt-driven detection);
- the A8 retry loops use `vTaskDelay(1)` — one tick, 10 ms — written explicitly,
  **never** `pdMS_TO_TICKS(1)`, which is 0;
- `read()`'s internal wait loop switches to an `esp_timer_get_time()` microsecond
  deadline with `esp_rom_delay_us`, since it is a sub-millisecond wait that a tick
  delay cannot express.

Using `int64_t` microseconds from `esp_timer_get_time()` throughout removes the
wraparound class of bug entirely. `write_wait_idle(uint8_t, uint64_t
timeout_micros)` already takes microseconds and needs no signature change.

**Removing `HIGH`/`LOW` is a hygiene fix, not only style.** `#define HIGH 0x1` in
a component header is a classic collision source for any other library defining
the same names.

### P2 — delete `ps2keyboard.{h,cpp}`
A rival host-side implementation (reads *from* a keyboard — the opposite
direction), reusing the include guard `PS2_KEYBOARD_H` so it collides with
`ps2_keyboard.h`, and calling `gpio_install_isr_service(0)` a second time when
`src/main.cpp:238` already installs it. Unreferenced.

### P3 — trim `REQUIRES` and the unused includes
`esp_hid nvs_flash driver bt` → `driver esp_timer esp_rom`. No PS/2 source uses
Bluetooth or NVS.

**Coupled with P1's header cleanup — order matters.** `ps2_device.h` and
`ps2_keyboard.h` both `#include <nvs_flash.h>`. `REQUIRES` is what supplies a
component's include paths, so trimming it without first removing those includes
fails the build with `nvs_flash.h: No such file or directory`. The unused
`<stack>`, `<string>`, `<functional>` and `<initializer_list>` includes go at the
same time; `esp_rom_sys.h` is added for `esp_rom_delay_us`.

### P4 — stop the send task busy-spinning
`ps2_keyboard.cpp:38` reads the packet queue with a **zero timeout** inside
`while(true) { ...; portYIELD(); }`, so the task never blocks. It runs at
**priority 9 on core 0** — the core hosting WiFi, lwIP, httpd, the console and
SessionBroker. A ready-but-never-blocking task at priority 9 starves everything
below it; `httpd` defaults to priority 5.

Fix: `xQueueReceive(..., pdMS_TO_TICKS(250))`. Not `portMAX_DELAY` — the task
must wake periodically to observe the `_running` teardown flag, and a 250 ms
bounded wait avoids a sentinel packet and its queue-full edge case. Four no-op
wakeups a second, versus a continuous spin.

### P5 — `send_packet` applies backpressure
`ps2_device.cpp:271` is `xQueueSend(_queue_packet, packet, 0)` against a 20-deep
queue, and `keydown`/`keyup`/`type()` all discard the return value. Each character
is two packets and the wire drains at roughly 1 ms per packet, so `ps2 type` of
anything past ~10 characters **silently drops keystrokes**.

Fix: `xQueueSend(..., pdMS_TO_TICKS(500))`, so the caller blocks until the wire
catches up. 500 ms comfortably exceeds the ~20 ms a full 20-deep queue takes to
drain, so it only expires if the wire is genuinely stuck.
Safe against a disconnected port because `write_wait_idle` already carries a
1500 us timeout — the queue drains (failing per byte) rather than wedging.
Also add `if (!_queue_packet) return -1;` so a send racing a teardown fails
cleanly instead of writing to a deleted handle.

### P6 — cooperative teardown, `PS2Device::end()`

`begin()` creates: GPIO config (OUTPUT_OD) on CLK and DATA, `_mutex_bus` (~80 B),
`_queue_packet` (20 x `PS2Packet`, ~360 B), and two 4096-byte tasks (8 KB internal
DRAM). The ISR service and `ps2_task` in that file are commented-out dead code.

Both tasks hold `_mutex_bus` across their critical sections, so `vTaskDelete` on
either would orphan the mutex and leave CLK/DATA pulled low, wedging the host.
Teardown must be cooperative.

Add `volatile bool _running`, written **only** by `begin()` (true, last statement)
and `end()` (false, first statement) — the discipline AGENTS.md records for
`IECBusHandler::m_enabled`, and for the same reason: surrounding code
read-modify-writes its other state and would clobber a shared sentinel. Both task
loops become `while (ps2dev->running())`, and each task NULLs its own handle and
calls `vTaskDelete(NULL)` only after releasing the mutex.

```cpp
void PS2Device::end()
{
    if (!_running) return;
    _running = false;

    xTaskNotifyGive(_task_process_host_request);   // wake it from portMAX_DELAY

    for (int i = 0; i < 100 && (_task_send_packet || _task_process_host_request); i++)
        delay(10);                                  // bounded wait, <= 1 s

    if (_task_send_packet || _task_process_host_request) {
        Debug_printv("ps2: tasks did not exit; leaking mutex/queue deliberately");
        return;
    }

    gpio_isr_handler_remove(_ps2clk);
    gohi(_ps2clk); gohi(_ps2data);
    gpio_reset_pin(_ps2clk); gpio_reset_pin(_ps2data);

    vQueueDelete(_queue_packet);  _queue_packet = nullptr;
    vSemaphoreDelete(_mutex_bus); _mutex_bus  = nullptr;
}
```

The refusal branch is deliberate: freeing a mutex a live task may touch is a
guaranteed crash, while leaking ~440 bytes is survivable and leaves a log line
naming the fault.

### P7 — interrupt-driven host request detection

Replaces polling entirely. A host request is `CLK high && DATA low`, exactly what
`get_bus_state()` tests. The host sequence is CLK low (inhibit) → DATA low (start
bit) → CLK released, so the edge that creates the condition is **CLK rising**.

```cpp
static void IRAM_ATTR ps2_clk_isr(void *arg)
{
    PS2Device *dev = static_cast<PS2Device *>(arg);
    if (gpio_get_level(dev->clkPin()) == HIGH &&
        gpio_get_level(dev->dataPin()) == LOW) {
        BaseType_t hpw = pdFALSE;
        vTaskNotifyGiveFromISR(dev->hostRequestTask(), &hpw);
        if (hpw) portYIELD_FROM_ISR();
    }
}
```

`GPIO_INTR_POSEDGE` on CLK. Handler added with `gpio_isr_handler_add` in
`begin()`, removed in `end()` before the pins are reset. **The service is NOT
installed here** — `src/main.cpp:238` already calls `gpio_install_isr_service(0)`.

**Self-trigger trap:** we drive CLK ourselves — 11 rising edges per byte — and
DATA is low on every `0` bit, so our own traffic would fire the ISR repeatedly and
each hit would look like a host request. `write()` and `read()` therefore call
`gpio_intr_disable(_ps2clk)` on entry and `gpio_intr_enable` on exit. Both already
run under `_mutex_bus`, so this is serialized for free.

The host-request task then blocks indefinitely and **nothing polls the PS/2 lines
at all**:

```cpp
while (dev->running()) {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) continue;
    xSemaphoreTake(dev->get_bus_mutex_handle(), portMAX_DELAY);
    gpio_intr_disable(clk);
    if (dev->get_bus_state() == BusState::HOST_REQUEST_TO_SEND) {
        uint8_t cmd;
        int r = dev->read(&cmd);
        if      (r == 0)  dev->reply_to_host(cmd);
        else if (r == -2) dev->write(0xFE);      // A5 — Resend
    }
    gpio_intr_enable(clk);
    xSemaphoreGive(dev->get_bus_mutex_handle());
}
```

### P8 — correctness audit fixes

| # | Finding | Fix |
|---|---|---|
| A1 | `delay(n)` is a no-op for n < 10. `CONFIG_FREERTOS_HZ=100` so `portTICK_PERIOD_MS` is 10 and `delay()` integer-divides. `delay(9)` and `delay(1)` are `vTaskDelay(0)`. `process_host_request` therefore busy-spins at priority 10. | Superseded by P7 for the host-request path; remaining sub-tick sites handled per P1's tick warning |
| A2 | `write()`/`read()` declare `portMUX_TYPE mux` as a **stack local**. A spinlock in a stack frame excludes nothing between tasks or cores — only the incidental interrupt-disable does the work. | File-scope `static portMUX_TYPE` |
| A3 | ~900 us (`write`) / ~1.2 ms (`read`) with **interrupts disabled**, on the core running WiFi and lwIP. Unnecessary: in device-to-host transfers the device owns the clock, so preemption between bits merely stretches the clock, which hosts tolerate by design. | Per-bit critical sections — same total time, eight windows for interrupts to breathe |
| A4 | `write()` checks for host inhibit once at entry and never again, so a host pulling CLK low mid-byte loses the byte silently. | Re-check CLK per bit; abort with `-1` for the caller to retry |
| A5 | Parity errors silently swallowed. `read()` returns `-2` on mismatch; the caller tests `== 0` only. | Answer `0xFE` (Resend) — folded into P7's task |
| A6 | `write_wait_idle()` tight-spins up to 1500 us with no yield. | `esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS)` between polls against an `esp_timer_get_time()` deadline |
| A7 | Send-path return values discarded end to end: `send_packet` -> `keydown`/`keyup` -> `type()`. A dropped packet is invisible to the caller. | Covered by P5; `typeText()` propagates failure to the command's exit code |
| A8 | `while (write(...) != 0) delay(1);` at four sites in `reply_to_host` (RESET, GET_DEVICE_ID x2, SET_RESET_LEDS x2). `write()` returns `-1` whenever the bus is not IDLE and `delay(1)` is `vTaskDelay(0)`, so a host holding CLK low spins **forever at priority 10 with `_mutex_bus` held**, and `end()` can never complete. | Bounded retry with real `vTaskDelay(1)` and a failure return |

`if (!read(&val))` at three sites was checked for inverted polarity and is
**correct** — `read()` returns 0 on success.

A1 and A8 are arithmetic and certain. A2 through A6 are read off the code and
unverified on hardware.

## Device class

```cpp
// lib/device/ps2/ps2.h
#ifdef PIN_KB_CLK
class PS2KeyboardDevice {
public:
    void start();                 // boot: reads config only, touches no hardware
    bool isEnabled() const;
    bool isRunning() const;

    bool startDevice();           // lazy begin(); re-announces BAT if already up
    bool enable();                // enabled=1, persist
    bool disable();               // releaseAll() -> end() -> enabled=0, persist
    void persistConfig();         // writes devices.ps2.enabled
    void reloadConfig();          // applies config to live state

    bool typeText(const std::string &text);
    bool pressKey(scancodes::Key k);      // make + break
    bool holdKey(scancodes::Key k);       // make only, tracked
    bool releaseKey(scancodes::Key k);
    bool pressCombo(const std::vector<scancodes::Key> &keys);
    void releaseAll();
private:
    bool ensureStarted();
    bool _enabled = false, _started = false;
    std::vector<scancodes::Key> _held;
    SemaphoreHandle_t _api_mutex;
    ps2dev::PS2Keyboard _kb;
};
#else
class PS2KeyboardDevice { /* identical signatures, all no-op / return false */ };
#endif
extern PS2KeyboardDevice ps2Keyboard;
```

Follows the `DisplayLEDs` idiom so `config load` and
`iecMeatloaf::reloadAllConfig()` reach it the same way they reach the LED strip
and the drives. The device owns `ps2dev::PS2Keyboard` **by value**, so the global
object leaves `src/main.cpp` and the pins are named in exactly one place.

### Lifecycle

`devices.ps2.enabled` already exists in every
`data/BUILD_IEC.*/.sys/devices.json` as `{"enabled": 0}` and is currently read by
nothing.

- `start()` (boot) reads `enabled` into `_enabled` and **allocates nothing**.
- Every send routes through `ensureStarted()`: returns false if disabled; on
  first use calls `_kb.begin()` — two tasks, a 200 ms settle, and the `0xAA` BAT
  byte announcing the keyboard.
- `ps2 start` forces it up. On an already-running device it **re-announces**
  (re-sends BAT) rather than no-opping, giving a best-effort hot-plug hook.
- `disable()` takes `_api_mutex`, clears `_enabled`, calls `releaseAll()`, calls
  `_kb.end()`, clears `_started`, persists. Reclaims the full 8 KB.

**Accepted risk, decided deliberately:** lazy start means that after a Meatloaf
reboot with `enabled: 1`, PS/2 stays dormant until a `ps2` command runs. A host
that probes at power-on and finds nothing generally never re-probes. Boot-start
was proposed and declined in favour of keeping the minimum-footprint property.
Mitigations: `ps2 start` is the explicit way up, `ps2 start` re-announces, and
`ps2 status` reports whether the host has ever handshaken so the failure is
diagnosable rather than silent. This risk is expected to be small for the DTV,
which is unlikely to probe at power-on.

### Held-key tracking

`holdKey` pushes onto `_held`, `releaseKey` pops, `releaseAll()` sends break codes
in **reverse order**. This is a safety feature, not bookkeeping: leaving Ctrl or
Shift down modifies every keystroke typed on the host's *own* keyboard, so a
dropped connection could wedge the target machine. `disable()` calls
`releaseAll()`. No inactivity watchdog — explicit release plus release-on-disable
is sufficient and a timeout would be speculative.

### Concurrency

`_api_mutex` serializes `enable`/`disable`/sends. The console executor already
serializes console and WebSocket commands onto one task, so this is defence
rather than a known race — 80 bytes to stop having to re-derive that argument.

## Console commands

```
ps2                      alias for `ps2 status`
ps2 start                ensureStarted(); re-announce BAT if already running
ps2 enable               enabled=1, persist
ps2 disable              releaseAll() -> end() -> enabled=0, persist (frees 8 KB)
ps2 status               enabled / running / data-reporting / LEDs / held keys
ps2 type <text...>       typeText()
ps2 key <a>[+<b>...]     pressCombo()   e.g.  ps2 key ctrl+alt+del
ps2 down <name>          holdKey()
ps2 up <name>            releaseKey()
ps2 release              releaseAll()
ps2 keys                 list recognised key names
```

Registered by `registerPS2Commands()` in `Console.cpp` under `#ifdef PIN_KB_CLK`,
matching `registerDisplayCommands()`. WebSocket comes free.

All subcommands return non-zero on failure (disabled, unknown key name, not
started) so they compose under `run <script.sh>`.

### Encoding

`ps2 type` emits **set-2 scancodes for ASCII input, and is deliberately not
PETSCII-encoded.** Every other console command that emits characters (`exec`,
`write`) runs text through `mstr::toPETSCII2()` because its destination is the IEC
bus; PS/2 is a different boundary. Running text through the PETSCII map here would
silently produce garbage. This gets a comment at the call site.

Note the DTV still *displays* PETSCII — that mapping happens on the DTV side, from
the scancodes we send. C64-specific glyphs are not reachable through `type()` at
all, only through named keys.

A non-ASCII UTF-8 character has no PS/2 scancode and is **rejected** with a clear
error rather than mangled.

`ps2 type` reuses the `0xNN` run escape from `write`/`exec`, so control bytes are
typeable: `ps2 type "dir0x0D"` sends `dir` then Enter. One `0x` introduces a *run*
of hex byte pairs, ending at the first non-hex-digit. The component's `type()`
already maps `\r`/`\n` to `K_RETURN`, `\t` to `K_TAB` and `\b` to `K_BACKSPACE`,
so the escape lands on the right keys with no extra table.

### Inherited splitter limitations

`esp_console_run()` caps a line at `CONSOLE_MAX_CMDLINE_ARGS` (32) and silently
drops the tail; `joinArgs()` collapses runs of whitespace to a single space. So
`ps2 type` cannot reproduce consecutive spaces and truncates past roughly 30
words. Both are traits `write` already has; the fix would be in the splitter.

## Key names

`lookupKey()` is a `static const` name-to-`scancodes::Key` array in flash,
linear-searched, case-insensitive. Roughly 60 entries: enter/return, tab, esc,
backspace, space, delete, insert, home, end, pgup, pgdn, up/down/left/right,
f1-f12, ctrl/lctrl/rctrl, shift/lshift/rshift, alt/lalt/ralt, gui/win, capslock,
numlock, scrolllock, printscreen, pause, menu.

C64 key names — `runstop`, `restore`, `commodore`, `clrhome`, `instdel`, `pound`,
`pi`, `uparrow`, `leftarrow` — are aliases onto whichever PS/2 keys the DTV maps
to them. **Those bindings are unknown until bench discovery** (see Verification)
and must not be guessed in code.

An optional `devices.ps2.keymap` object in `devices.json` overrides individual
name-to-key bindings, defaulting to the built-in table. This turns "reflash to try
a different RESTORE key" into "edit a file" during discovery.

## Verification

### Native — `test/native/test_ps2_keys/`

`lib/console` and `lib/device` are not compiled natively, so the pure logic lives
in units with no device includes — mirroring `test/native/test_console_dos/`,
which tests `encodeDosCommand` the same way. The key-name table lives in
`lib/device/ps2/ps2_keynames.{h,cpp}`; the escape decoder is **not cloned**
there but shared with `encodeDosCommand` as a second entry point,
`ESP32Console::encodeAsciiCommand()`, since that function already implements
exactly these semantics and only the PETSCII transform differs.

- `lookupKey()`: case-insensitivity, unknown-name rejection, keymap override
- the `0xNN` run-escape decoder: run semantics, odd-digit-count rejection,
  non-hex terminator
- held-key tracking: `holdKey`/`releaseKey`/`releaseAll` ordering, and that
  `releaseAll` emits break codes in reverse

### Hardware — `fujiloaf-rev0`, in order

1. **Does it link?** Build `fujiloaf-rev0`. Proves P1 — the duplicate-symbol
   failure fires as soon as anything references the component, which the device
   class now does unconditionally on a pinned board.
2. **Does core 0 survive it?** `ps2 start`, then exercise the web server and
   console. Pre-fix prediction is that both stall; post-fix both unaffected. This
   is the P4/A1 check and the most load-bearing prediction in the design.
3. **Teardown reclaims.** `meminfo` -> `ps2 start` -> `meminfo` -> `ps2 disable`
   -> `meminfo`. Expect ~8 KB internal down and back up. Also confirms `end()`
   does not hang on the A8 loops.
4. **First byte on the wire.** `ps2 type "a"` — scope on CLK/DATA, or an `A` on
   the DTV.
5. **Backpressure.** `ps2 type` a 200-character string. Pre-P5 this drops
   keystrokes past ~10 characters; post-fix it arrives complete in ~400 ms.

### Discovery — the actual DTV unknown

`ps2 keys` walks the set-2 table; sending each and observing what the DTV produces
yields the scancode-to-C64 mapping empirically. Two results fall out: the C64 key
aliases get bound to real PS/2 keys, and we learn whether the DTV sends anything
back at all — which tells us whether the P7 interrupt path ever fires in practice.

## Open questions

- The DTV's PS/2 scancode-to-C64-key table. Bench finding; blocks only the C64
  key aliases, not the rest of the feature.
- Whether the DTV sends any host-to-device commands. The P7 path handles either
  case at zero idle cost.
