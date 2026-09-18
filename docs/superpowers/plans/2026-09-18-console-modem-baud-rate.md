# Console and Modem Baud Rate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the serial rate be changed at runtime from the shell (`baud <n>`) and from modem mode (`AT+IPR=<n>`), shared by both modes, persisted, and refused on boards whose console is USB.

**Architecture:** One rate lives in `preferences.baud`. A new `lib/console/console_baud.*` owns every UART reconfiguration. The shell command applies it inline, because the task that printed the notice is the task running the command. The AT command records a pending rate that `modem_shell_pump()` applies after writing its reply, because the modem task does not own the console fd. At boot the persisted rate is applied after `mlConfig.load()`.

**Tech Stack:** ESP-IDF via PlatformIO, C++17 (`-fno-exceptions`, no RTTI), Unity for native tests.

Design: `docs/superpowers/specs/2026-09-18-console-modem-baud-rate-design.md`

## Global Constraints

- ESP-IDF builds `-fno-exceptions`. Never use `std::stoi`/`std::stof` on user or network input; use `strtol` and validate `*end == '\0'`.
- Never read a config node with nlohmann `value()`. Use `json_int()`/`json_str()` from `lib/config-ml/mlConfig.h`, which check `is_object()` and then the field's own type. A type mismatch is an `abort()`, not a throw.
- The accepted rate range is **300 to 4000000** inclusive, in both the shell command and the AT command.
- `preferences.baud` of `0` means "use the build's `DEBUG_SPEED`". That is what every existing installation has.
- Boards whose console is `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` or `CONFIG_ESP_CONSOLE_USB_CDC` have no UART. Both commands must refuse there, never silently succeed.
- `pio` is not on PATH. Use `~/.platformio/penv/Scripts/pio.exe`.
- Never pipe `pio` through `| tail` or `| head`; redirect to a file and grep it.
- `lib/console/*.cpp` is a cached CMake glob. After adding `console_baud.cpp`, delete `.pio/build/<env>/CMakeCache.txt` or the new file is not compiled and the link fails with an undefined reference.
- Do not add attribution lines to commit messages.

---

### Task 1: Parser accepts `AT+NAME=<n>` and `AT+NAME?`

`at_parser.cpp`'s `+` branch reads the name and then `continue`s, so it accepts no argument: `AT+IPR=9600` fails the whole line at the `=`. Two changes: give `read_number()` a per-call ceiling (its current hard clamp of 1000000 would turn `AT+IPR=2000000` into 1000000), and parse the `=` and `?` forms.

**Files:**
- Modify: `lib/modem/at_parser.cpp:79-92` (`read_number`), `lib/modem/at_parser.cpp:147-161` (the `+` branch)
- Test: `test/native/test_modem_at/test_modem_at.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: an `AtCommand` for a `+` command now carries `assign` (bool), `value` (long, -1 when absent) and `query` (bool), exactly as an S-register command already does. `prefix` and `name` are unchanged.

- [ ] **Step 1: Write the failing tests**

Add these five functions to `test/native/test_modem_at/test_modem_at.cpp`, next to `test_parse_ampersand_and_plus_prefixed_commands` (around line 145):

```cpp
void test_parse_plus_command_takes_an_assignment(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT+IPR=9600", line, nullptr));
    TEST_ASSERT_EQUAL_CHAR('+', line[0].prefix);
    TEST_ASSERT_EQUAL_STRING("IPR", line[0].name.c_str());
    TEST_ASSERT_TRUE(line[0].assign);
    TEST_ASSERT_FALSE(line[0].query);
    TEST_ASSERT_EQUAL_INT(9600, (int)line[0].value);
}

// read_number() clamped at 1000000, which would have turned the project's own
// console rate into a different number and still answered OK.
void test_parse_plus_assignment_survives_a_two_million_baud_value(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT+IPR=2000000", line, nullptr));
    TEST_ASSERT_EQUAL_INT(2000000, (int)line[0].value);
}

void test_parse_plus_command_takes_a_query(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT+IPR?", line, nullptr));
    TEST_ASSERT_TRUE(line[0].query);
    TEST_ASSERT_FALSE(line[0].assign);
    TEST_ASSERT_EQUAL_INT(-1, (int)line[0].value);
}

void test_parse_plus_command_with_no_argument_is_still_valid(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT+SHELL", line, nullptr));
    TEST_ASSERT_EQUAL_STRING("SHELL", line[0].name.c_str());
    TEST_ASSERT_FALSE(line[0].assign);
    TEST_ASSERT_FALSE(line[0].query);
}

void test_parse_plus_command_rejects_a_malformed_assignment(void)
{
    AtLine line;
    size_t pos = 0;
    TEST_ASSERT_FALSE(at_parse("AT+IPR=", line, &pos));
    TEST_ASSERT_FALSE(at_parse("AT+IPR=abc", line, &pos));

    // A plus command still composes with the rest of a line.
    TEST_ASSERT_TRUE(at_parse("ATE0+IPR=9600", line, nullptr));
    TEST_ASSERT_EQUAL_INT(2, (int)line.size());
    TEST_ASSERT_EQUAL_CHAR('E', line[0].verb);
    TEST_ASSERT_EQUAL_INT(9600, (int)line[1].value);
}
```

Register them in the runner next to line 1504:

```cpp
    RUN_TEST(test_parse_plus_command_takes_an_assignment);
    RUN_TEST(test_parse_plus_assignment_survives_a_two_million_baud_value);
    RUN_TEST(test_parse_plus_command_takes_a_query);
    RUN_TEST(test_parse_plus_command_with_no_argument_is_still_valid);
    RUN_TEST(test_parse_plus_command_rejects_a_malformed_assignment);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at > /tmp/t.log 2>&1; grep -E "FAIL|Tests" /tmp/t.log`

Expected: the four argument cases FAIL (`AT+IPR=9600` does not parse at all).
`test_parse_plus_command_with_no_argument_is_still_valid` PASSES already — that is the point, it is the regression guard for `AT+SHELL`.

- [ ] **Step 3: Give `read_number()` a per-call ceiling**

In `lib/modem/at_parser.cpp`, change the definition at line 79. Keep the default at the existing 1000000 so every current caller behaves exactly as before:

```cpp
// Reads consecutive digits starting at i, advancing i. Returns -1 when there
// are none, so the caller can tell "absent" from a literal 0.
//
// `limit` is a CLAMP, not a validity test -- the caller range-checks. It
// defaults to the value every S-register caller has always used; AT+IPR passes
// a larger one, because a baud rate is the first number here that legitimately
// exceeds a million, and clamping 2000000 down to 1000000 would set a rate the
// user never asked for and then report success.
long read_number(const std::string &s, size_t &i, long limit = 1000000)
{
    size_t start = i;
    long n = 0;
    while (i < s.size() && is_digit(s[i]))
    {
        n = n * 10 + (s[i] - '0');
        ++i;
        // A modem's numbers are all small. Clamp rather than overflow.
        if (n > limit)
            n = limit;
    }
    return (i == start) ? -1 : n;
}
```

- [ ] **Step 4: Parse `=` and `?` in the `+` branch**

Replace the `+` branch body at `lib/modem/at_parser.cpp:147-161`:

```cpp
        // ---- AT+NAME, AT+NAME=<n>, AT+NAME? --------------------------------
        if (line[i] == '+')
        {
            cmd.prefix = '+';
            ++i;
            size_t name_start = i;
            while (i < line.size() && std::isalnum((unsigned char)line[i]))
            {
                cmd.name.push_back(upper(line[i]));
                ++i;
            }
            if (i == name_start)
                return fail(cmd_start);

            // The same three forms an S-register takes. A bare name stays
            // valid -- AT+SHELL has no argument and must keep parsing exactly
            // as it always did.
            if (i < line.size() && line[i] == '=')
            {
                ++i;
                cmd.assign = true;
                // 10000000 comfortably clears the 4000000 ceiling the baud
                // commands enforce, so the range check happens in one place
                // rather than being half-done here by a clamp.
                long v = read_number(line, i, 10000000);
                if (v < 0)
                    return fail(i);
                cmd.value = v;
            }
            else if (i < line.size() && line[i] == '?')
            {
                ++i;
                cmd.query = true;
            }

            out.push_back(cmd);
            continue;
        }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at > /tmp/t.log 2>&1; grep -E "FAIL|Tests" /tmp/t.log`

Expected: all cases PASS, count is the previous 83 plus 5 = 88.

- [ ] **Step 6: Mutation check**

Temporarily delete the `=`/`?` block added in Step 4, re-run, and confirm exactly the four argument cases fail while `test_parse_plus_command_with_no_argument_is_still_valid` still passes. Restore the block. This proves the new tests reach the change rather than passing for unrelated reasons.

- [ ] **Step 7: Commit**

```bash
git add lib/modem/at_parser.cpp test/native/test_modem_at/test_modem_at.cpp
git commit -m "feat(modem): parse AT+NAME=<n> and AT+NAME? so a plus command can take an argument"
```

---

### Task 2: `console_baud` module

The one place that reconfigures the UART. No command or task calls `uart_set_baudrate()` anywhere else.

**Files:**
- Create: `lib/console/console_baud.h`, `lib/console/console_baud.cpp`

**Interfaces:**
- Consumes: `mlConfig` (`lib/config-ml/mlConfig.h`), `driver/uart.h`.
- Produces, all in `namespace ESP32Console`:
  - `constexpr int BAUD_MIN = 300;`
  - `constexpr int BAUD_MAX = 4000000;`
  - `bool consoleBaudSupported()`
  - `const char *consoleBaudTransportName()`
  - `int consoleBaudGet()`
  - `esp_err_t consoleBaudSet(int baud)`
  - `void consoleBaudSetPending(int baud)`
  - `void consoleBaudApplyPending()`
  - `void consoleBaudRestore()`

- [ ] **Step 1: Create the header**

`lib/console/console_baud.h`:

```cpp
// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// The one owner of the console UART's rate.
//
// Both the "baud" shell command and the modem's AT+IPR end here, but they
// reach it by different routes, and that difference is the reason this module
// exists. The shell command runs on the same task that printed its notice, so
// it applies the change itself. AT+IPR runs on the modem task, which never
// touches a console file descriptor (see lib/modem/modem_port.h) -- its "OK"
// is still sitting in a ModemPort StreamBuffer when the handler returns, so
// switching the line there would clock that reply out at the new rate as
// garbage. It records a pending rate instead, and modem_shell_pump() applies
// it once those bytes are actually on the wire.

#ifndef MEATLOAF_CONSOLE_BAUD
#define MEATLOAF_CONSOLE_BAUD

#include "esp_err.h"

namespace ESP32Console
{
    // A range check only. The driver picks the closest achievable divisor, and
    // a silly-but-legal rate is recoverable over the TCP console, so refusing a
    // legitimate odd rate would cost more than accepting one.
    static constexpr int BAUD_MIN = 300;
    static constexpr int BAUD_MAX = 4000000;

    // False on the five boards whose console is USB-Serial-JTAG or USB-CDC.
    // There is no UART there and a rate is discarded by the USB stack, so the
    // commands refuse rather than appearing to succeed.
    bool consoleBaudSupported();

    // "UART", "USB-Serial-JTAG" or "USB-CDC", for the refusal message.
    const char *consoleBaudTransportName();

    // The rate in effect, read from the driver rather than from a shadow copy
    // that could drift. 0 when this console has no UART.
    int consoleBaudGet();

    // Validate, drain, switch, and only then persist. Safe to call from a task
    // that owns the console fd, or from a TCP-origin command, whose reply never
    // crosses the UART at all.
    esp_err_t consoleBaudSet(int baud);

    // Record a rate for modem_shell_pump() to apply after its next write.
    void consoleBaudSetPending(int baud);
    void consoleBaudApplyPending();

    // Boot: apply preferences.baud if it is set. Called after mlConfig.load().
    void consoleBaudRestore();
}

#endif // MEATLOAF_CONSOLE_BAUD
```

- [ ] **Step 2: Create the implementation**

`lib/console/console_baud.cpp`:

```cpp
// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#include "console_baud.h"

#include <stdio.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#include "mlConfig.h"

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#define CONSOLE_HAS_UART 1
#else
#define CONSOLE_HAS_UART 0
#endif

namespace ESP32Console
{
    // Written by whichever task runs an AT+IPR, read by the shell pump. A plain
    // int is enough: it is a single aligned word, one writer at a time, and a
    // missed pending rate would simply be applied on the next pass.
    static volatile int s_pending = 0;

    bool consoleBaudSupported()
    {
        return CONSOLE_HAS_UART ? true : false;
    }

    const char *consoleBaudTransportName()
    {
#if CONSOLE_HAS_UART
        return "UART";
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
        return "USB-Serial-JTAG";
#else
        return "USB-CDC";
#endif
    }

    int consoleBaudGet()
    {
#if CONSOLE_HAS_UART
        uint32_t rate = 0;
        if (uart_get_baudrate((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, &rate) == ESP_OK)
            return (int)rate;
#endif
        return 0;
    }

    esp_err_t consoleBaudSet(int baud)
    {
#if !CONSOLE_HAS_UART
        (void)baud;
        return ESP_ERR_NOT_SUPPORTED;
#else
        if (baud < BAUD_MIN || baud > BAUD_MAX)
            return ESP_ERR_INVALID_ARG;

        const uart_port_t port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
        const int previous = consoleBaudGet();

        // The notice the user is reading was written at the OLD rate. Flushing
        // only moves it into the driver; uart_wait_tx_done() is what puts it on
        // the wire. Switching before that clocks the last characters out at the
        // new rate, which is exactly the garbage this whole design avoids.
        fflush(stdout);
        fsync(fileno(stdout));
        uart_wait_tx_done(port, pdMS_TO_TICKS(200));

        esp_err_t err = uart_set_baudrate(port, baud);
        if (err != ESP_OK)
        {
            // Persist NOTHING. A rate the driver refuses must never become the
            // rate the next boot reads -- that is the one failure this design
            // exists to make impossible.
            if (previous > 0)
                uart_set_baudrate(port, previous);
            return err;
        }

        auto &root = mlConfig.data();
        if (!root.is_object())
            return ESP_OK;
        if (!root.contains("preferences") || !root["preferences"].is_object())
            root["preferences"] = psram_json::object();
        root["preferences"]["baud"] = baud;
        // The same call AT&W already makes through Modem::saveConfig(). save()
        // hashes the sections and writes nothing when nothing changed.
        mlConfig.save();
        return ESP_OK;
#endif
    }

    void consoleBaudSetPending(int baud)
    {
        if (baud >= BAUD_MIN && baud <= BAUD_MAX)
            s_pending = baud;
    }

    void consoleBaudApplyPending()
    {
        int baud = s_pending;
        if (baud == 0)
            return;
        s_pending = 0;
        consoleBaudSet(baud);
    }

    void consoleBaudRestore()
    {
        auto &root = mlConfig.data();
        if (!root.is_object() || !root.contains("preferences"))
            return;
        // json_int() checks is_object() and then the field's own type. value()
        // would abort() on a hand-edited config, and this runs too early in
        // boot for that to be recoverable.
        int baud = json_int(root.at("preferences"), "baud", 0);
        if (baud <= 0)
            return; // 0 means "use the build's DEBUG_SPEED"
        if (baud == consoleBaudGet())
            return;
        consoleBaudSet(baud);
    }
}
```

- [ ] **Step 3: Delete the cached CMake glob and build**

`lib/console/*.cpp` is a glob and the file list is cached, so a new source is otherwise not compiled.

```bash
rm -f .pio/build/lolin-d32-pro/CMakeCache.txt
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "exit=$?"; grep -iE "RAM:|Flash:|error:" /tmp/b.log
```

Expected: exit 0, no errors. This is a full rebuild and takes several minutes.

- [ ] **Step 4: Commit**

```bash
git add lib/console/console_baud.h lib/console/console_baud.cpp
git commit -m "feat(console): add console_baud, the one owner of the console UART rate"
```

---

### Task 3: `baud` shell command

**Files:**
- Modify: `lib/console/Commands/CoreCommands.cpp`, `lib/console/Commands/CoreCommands.h`, `lib/console/Console.cpp:284-290` (registration)

**Interfaces:**
- Consumes: `ESP32Console::consoleBaudSupported/TransportName/Get/Set`, `BAUD_MIN`, `BAUD_MAX` from Task 2; `console.execOrigin()`.
- Produces: `const ConsoleCommand getBaudCommand();` in `namespace ESP32Console::Commands`.

- [ ] **Step 1: Add the command body**

In `lib/console/Commands/CoreCommands.cpp`, add `#include "../console_baud.h"` with the other includes, then add this function above `getRebootCommand()`:

```cpp
static int baud(int argc, char **argv)
{
    if (!ESP32Console::consoleBaudSupported())
    {
        // Refuse rather than accept a value and report it back while doing
        // nothing -- the failure mode the S7 finding established as worse than
        // a refusal. This board's console is USB; there is no rate to set.
        printf("this console is %s, it has no baud rate\r\n",
               ESP32Console::consoleBaudTransportName());
        return EXIT_FAILURE;
    }

    if (argc < 2)
    {
        printf("console baud %d\r\n", ESP32Console::consoleBaudGet());
        return EXIT_SUCCESS;
    }

    // strtol, never std::stoi: ESP-IDF is -fno-exceptions, so a throw from a
    // malformed argument is std::terminate.
    char *end = nullptr;
    long want = strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0')
    {
        printf("baud: not a number: %s\r\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (want < ESP32Console::BAUD_MIN || want > ESP32Console::BAUD_MAX)
    {
        printf("baud: %ld out of range (%d-%d)\r\n",
               want, ESP32Console::BAUD_MIN, ESP32Console::BAUD_MAX);
        return EXIT_FAILURE;
    }

    const int now = ESP32Console::consoleBaudGet();

    // The notice goes out at the OLD rate, which is what makes it readable.
    // From the TCP console the reply never crosses the UART at all, so there is
    // nothing to reconnect -- and that path is the documented recovery route,
    // so telling a remote caller to reconnect would be plainly false.
    if (console.execOrigin() == Console::ORIGIN_REMOTE)
        printf("serial console baud %d -> %ld\r\n", now, want);
    else
        printf("console baud %d -> %ld, reconnect now\r\n", now, want);

    esp_err_t err = ESP32Console::consoleBaudSet((int)want);
    if (err != ESP_OK)
    {
        printf("baud: failed, still %d (%s)\r\n",
               ESP32Console::consoleBaudGet(), esp_err_to_name(err));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

And the accessor beside `getRebootCommand()`:

```cpp
    const ConsoleCommand getBaudCommand()
    {
        return ConsoleCommand("baud", &baud,
                              "Show or set the serial console baud rate (300-4000000)");
    }
```

- [ ] **Step 2: Declare and register it**

In `lib/console/Commands/CoreCommands.h`, add inside the namespace:

```cpp
    const ConsoleCommand getBaudCommand();
```

In `lib/console/Console.cpp`, in `registerCoreCommands()` beside line 289:

```cpp
        registerCommand(getBaudCommand());
```

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "exit=$?"; grep -iE "RAM:|Flash:|error:" /tmp/b.log
```

Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add lib/console/Commands/CoreCommands.cpp lib/console/Commands/CoreCommands.h lib/console/Console.cpp
git commit -m "feat(console): add the baud command"
```

---

### Task 4: `AT+IPR` and the pump apply

**Files:**
- Modify: `lib/modem/modem.cpp:411-427` (the `+` dispatch), `lib/console/Console.cpp:160-177` (the pump's write block)

**Interfaces:**
- Consumes: `AtCommand::assign/value/query` from Task 1; `consoleBaudSupported/Get/SetPending/ApplyPending` and `BAUD_MIN`/`BAUD_MAX` from Task 2.
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Handle `+IPR` in the modem**

In `lib/modem/modem.cpp`, add `#include "console_baud.h"` with the other includes. Then inside `Modem::executeCommand`, in the `if (cmd.prefix == '+')` block, add before its closing `return false;`:

```cpp
        if (cmd.name == "IPR")
        {
            // 3GPP TS 27.007's DTE-rate command. There is one physical UART, so
            // this is the same rate the shell's "baud" command sets.
            if (!ESP32Console::consoleBaudSupported())
                return false; // -> ERROR; this console is USB, it has no rate

            // A bare AT+IPR reports, the same as AT+IPR?. Only an assignment
            // sets anything.
            if (!cmd.assign)
            {
                broadcast("\r\n" + std::to_string(ESP32Console::consoleBaudGet()) + "\r\n");
                return true;
            }

            if (cmd.value < ESP32Console::BAUD_MIN || cmd.value > ESP32Console::BAUD_MAX)
                return false;

            // NOT applied here. This runs on the modem task, whose "OK" is
            // still in a ModemPort StreamBuffer -- switching the line now would
            // send that reply at the new rate as garbage. The shell pump
            // applies it once the bytes are on the wire.
            ESP32Console::consoleBaudSetPending((int)cmd.value);
            return true;
        }
```

- [ ] **Step 2: Apply it in the pump**

In `lib/console/Console.cpp`, add `#include "console_baud.h"` with the other includes. Then in `modem_shell_pump()`, immediately after the `if (n > 0) { ... }` block that ends around line 177, and before the "Terminal -> modem" comment:

```cpp
            // An AT+IPR could not switch the line itself -- its reply was still
            // in the port's buffer. It is on the wire now, so this is the first
            // safe moment. A no-op on every other pass.
            ESP32Console::consoleBaudApplyPending();
```

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "exit=$?"; grep -iE "RAM:|Flash:|error:" /tmp/b.log
```

Expected: exit 0. Note `lolin-d32-pro` does not define `ENABLE_MODEM` today, so this only proves it compiles; Task 6 adds the flag for the hardware run.

- [ ] **Step 4: Run the native suite for regressions**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at > /tmp/t.log 2>&1; grep -E "FAIL|Tests" /tmp/t.log`

Expected: 88 cases, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add lib/modem/modem.cpp lib/console/Console.cpp
git commit -m "feat(modem): AT+IPR sets the shared serial rate, applied by the shell pump"
```

---

### Task 5: Restore the persisted rate at boot

**Files:**
- Modify: `src/main.cpp:298` (immediately after `mlConfig.load()`)

**Interfaces:**
- Consumes: `ESP32Console::consoleBaudRestore()` from Task 2.
- Produces: nothing.

- [ ] **Step 1: Call it after the config loads**

In `src/main.cpp`, immediately after the `mlConfig.load();` line:

```cpp
#ifdef ENABLE_CONSOLE
    // The persisted rate can only be applied here: config lives on flash or SD
    // and neither is mounted at console.begin(). The consequence is deliberate
    // and is the last-resort recovery path -- everything printed above this
    // line goes out at DEBUG_SPEED, so a wrong persisted rate can never hide
    // early boot on a board with no network.
    ESP32Console::consoleBaudRestore();
#endif
```

Add `#include "console_baud.h"` to `src/main.cpp`'s includes if the console headers do not already pull it in.

- [ ] **Step 2: Build**

```bash
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro > /tmp/b.log 2>&1; echo "exit=$?"; grep -iE "RAM:|Flash:|error:" /tmp/b.log
```

Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(console): apply the persisted baud rate once config is loaded"
```

---

### Task 6: Hardware verification

Two boards, two halves. Neither substitutes for the other: `lolin-d32-pro` is the only connected board with a UART console, and `freenove-esp32-s3-wroom-1` is the only one that exercises the refusal.

**Files:**
- Modify: `platformio.ini` (add `-D ENABLE_MODEM` to `[env:lolin-d32-pro]`)
- Modify: `AGENTS.md` (record the result)

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: nothing.

- [ ] **Step 1: Compile modem mode for the UART board**

`[env:lolin-d32-pro]` at `platformio.ini:339-349` does **not** set `-D ENABLE_MODEM`, so `AT+IPR` cannot be reached on the only board that has a UART. Add it beside `-D ENABLE_CONSOLE_TCP`, then build and flash:

```bash
~/.platformio/penv/Scripts/pio.exe run -e lolin-d32-pro -t upload --upload-port COM13 > /tmp/u.log 2>&1; echo "exit=$?"; grep -iE "Hash of data verified|Leaving|error" /tmp/u.log
```

Watch the flash figure: this board is an ESP32-WROVER with the small `iram0_2_seg` window, and `ENABLE_MODEM` has never been linked into it. If it overflows, say so and stop rather than trading a feature away silently.

- [ ] **Step 2: Verify the shell half on COM13**

Drive it with pyserial. COM13 is a CH340 bridge, so DTR/RTS auto-reset behaves normally there — unlike COM12. In one open session at 2000000, check:

- `baud` prints `console baud 2000000`
- `baud 50` is refused with the range message, and `baud 9600x` with the not-a-number message
- `baud 9600` prints `console baud 2000000 -> 9600, reconnect now`, readable at 2000000

Then reopen at 9600 and confirm a prompt, and that `baud` reports 9600.

- [ ] **Step 3: Verify the modem half on COM13**

Still at 9600, write `b"at\r"` — the trailing CR is the modem's S3 terminator, and the debug skill's capture daemon strips it, so use pyserial directly:

- `AT` answers `OK`
- `AT+IPR?` answers `9600`
- `AT+IPR=2400` answers `OK`, and the `OK` is **readable at 9600** — the check the whole pending/apply design exists for. Reopen at 2400 and confirm `AT` still answers `OK`.
- `AT+IPR=50` answers `ERROR`
- A dial still works at the new rate, proving the modem path is intact.

- [ ] **Step 4: Verify persistence and the boot-log property**

Reboot the board. Confirm the rate is still 2400 after boot, and that the early boot log is unreadable at 2400 but readable at 2000000 — the `DEBUG_SPEED` window that is the no-network recovery path.

Then set it back: `baud 2000000`.

- [ ] **Step 5: Verify the refusal on COM12**

Flash `freenove-esp32-s3-wroom-1` and attach **without asserting DTR/RTS** — that board is native USB (`303A:1001`), and toggling those lines resets the chip, tears down USB and leaves a dead handle reading zero bytes forever:

```python
ser = serial.Serial()
ser.port, ser.baudrate, ser.timeout = "COM12", 2000000, 0.1
ser.dtr = False
ser.rts = False
ser.open()
```

Confirm `baud` prints `this console is USB-Serial-JTAG, it has no baud rate` and exits non-zero, and that `AT+IPR=9600` answers `ERROR`.

- [ ] **Step 6: Record it in AGENTS.md**

Add a dated entry under a new `## Recent Changes (September 18, 2026)` heading covering: the shared-rate decision; the two apply paths and why one call site cannot serve both; apply-before-persist; the `read_number` clamp that would have turned 2000000 into 1000000; that five boards have no UART and refuse; the `DEBUG_SPEED` boot window as the recovery path; that `lolin-d32-pro` gained `ENABLE_MODEM` and what that cost in flash; and exactly which board verified which half, with the measured evidence.

- [ ] **Step 7: Commit**

```bash
git add AGENTS.md platformio.ini
git commit -m "docs: record the baud rate feature and its hardware verification"
```

---

## Self-Review

**Spec coverage.** `preferences.baud` — Task 2. Refusal on USB boards — Tasks 2, 3 and 4, verified in Task 6 Step 5. Range 300-4000000 — Task 2's constants, enforced in Tasks 3 and 4. Apply before persist — Task 2 Step 2. Two apply paths — Tasks 3 and 4. `execOrigin()` wording — Task 3 Step 1. Parser extension — Task 1. Boot restore after `mlConfig.load()` — Task 5. Native tests — Task 1. Hardware on both boards — Task 6.

**Known gap, accepted and stated in the spec:** there is no native coverage of `console_baud` itself. `lib/console` is not compiled in the native environment and the apply path needs a real UART, so that half is hardware-verified only — the same limitation the console file-channel work carries.

**Type consistency.** `consoleBaudSet` returns `esp_err_t` in the header, the implementation and both call sites. `consoleBaudGet` returns `int` everywhere. `BAUD_MIN`/`BAUD_MAX` are `int`, compared against a `long` in Task 3 and against `cmd.value` (a `long`) in Task 4 — both safe widening comparisons. `read_number`'s new third parameter is defaulted, so no existing caller changes.
