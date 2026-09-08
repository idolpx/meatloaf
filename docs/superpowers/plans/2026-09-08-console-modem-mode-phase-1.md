# Console Modem Mode — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Hayes-style virtual modem to the Meatloaf console, entered by typing `at`, that dials one outbound TCP or telnet connection, streams it transparently, escapes back to command mode with `+++`, and persists its settings and phonebook.

**Architecture:** A dedicated `modem` FreeRTOS task runs the AT engine. It never touches a console file descriptor — the existing shell tasks (`repl_task` for serial, `tcp_session` for TCP) stay the sole reader and writer of their own fd and pump bytes through a pair of FreeRTOS StreamBuffers (`ModemPort`). Connections are `MStream`s obtained from `MFSOwner::File("tcp://…")` / `("telnet://…")`, so the AT layer touches no sockets. Everything that can be pure C++ — parser, settings, result codes, escape detector, phonebook, telnet filter — is lifted into units that depend on nothing but the standard library, so the native test suite can reach them.

**Tech Stack:** C++17, ESP-IDF 5.5.3 via PlatformIO, FreeRTOS StreamBuffers, `lib/telnet/libtelnet.c` (C), Unity for native tests, `nlohmann::json` via `mlConfig` for persistence.

**Spec:** `docs/superpowers/specs/2026-09-08-console-modem-mode-design.md`

## Global Constraints

- **Phase 1 only.** No connection registry (`ATC`/`ATCn`/`ATHn`), no listeners (`ATA`/`ATAn`/RING), no SSH, no file transfers. Those are phases 2–5 and must not appear in this work.
- **`lib/modem/` must not include anything from `lib/console/` or `lib/device/`.** `lib/console` is not compiled in the native environment. Any device-layer include is guarded behind `#ifndef TEST_NATIVE`, as `lib/meatloaf/meat_media.h` already does.
- **Build gate:** every new file's contents are wrapped in `#ifdef ENABLE_MODEM` … `#endif`, and the flag is added only to `[env:esp32-s3-devkitc-1]` in `platformio.ini.sample`. Everything under `lib/` links as a plain object and is never dropped by the linker, so gating a caller frees nothing — the whole translation unit must be inside the guard.
- **Never apply `#pragma GCC optimize("Os")` or `("Oz")` to this C++ code.** It has made segments *bigger* three separate times in this repo (fastloaders 629 → 977 bytes over, and again in the ARC and `-lh1-` work).
- **Do not add attribution lines** to commit messages.
- **`pio` is not on PATH.** Use `~/.platformio/penv/Scripts/pio.exe`. Never pipe it through `| tail` or `| head` — that discards the error text. Redirect to a file and grep it.
- **Adding a new source file requires deleting `.pio/build/<env>/CMakeCache.txt`** before the next firmware build — the source glob is cached, so a new file is silently not compiled.
- **PETSCII is out of scope.** The far end is a PC terminal. No `mstr::toPETSCII2()` anywhere in `lib/modem/`.
- **Native test command:** `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
- **Firmware build command:** `~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1`
- **Every new file carries the project's GPL header** — copy the 17-line block from the top of `lib/meatloaf/network/tcp.h` verbatim. The code blocks below omit it for brevity; it is not optional.

## File Structure

**New, pure (native-testable — no ESP-IDF, no MStream, no console):**

| File | Responsibility |
|---|---|
| `lib/modem/at_parser.h/.cpp` | One line of text → `std::vector<AtCommand>`. No I/O. |
| `lib/modem/at_settings.h/.cpp` | S-register array, E/Q/V/X flags, factory defaults, key-value serialization. |
| `lib/modem/at_result.h/.cpp` | `AtResult` → the exact bytes a terminal receives, honouring V/Q/X/S3/S4. |
| `lib/modem/escape.h/.cpp` | `+++` detector with an injected clock. |
| `lib/modem/phonebook.h/.cpp` | Numbered dial entries, parse and serialize. |
| `lib/modem/telnet_filter.h/.cpp` | libtelnet wrapper over two byte sinks. No MStream. |

**New, ESP-only (hardware-verified):**

| File | Responsibility |
|---|---|
| `lib/modem/modem_port.h/.cpp` | StreamBuffer pair joining one shell task to the modem task. |
| `lib/modem/modem.h/.cpp` | `Modem` class: state machine, dial, stream pump, task, mlConfig glue. |
| `lib/meatloaf/network/telnet.h/.cpp` | `TelnetMStream` / `TelnetMFile` / `TelnetMFileSystem` over `TelnetFilter`. |

**Modified:**

| File | Change |
|---|---|
| `lib/meatloaf/meatloaf.h` | Declare `virtual bool waitReadable(uint32_t timeout_ms);` on `MStream`. |
| `lib/meatloaf/meatloaf.cpp` | Default polled implementation; register `TelnetMFileSystem`. |
| `lib/console/Console.cpp` | `at` raw-line intercept in `repl_task()` and `Console::execute()`. |
| `lib/console/tcpsvr.cpp` | Feed received bytes to the modem while a remote session is in modem mode. |
| `src/main.cpp` | Create the modem task at boot under `ENABLE_MODEM`. |
| `platformio.ini.sample` | `-D ENABLE_MODEM` on `esp32-s3-devkitc-1`; native include paths. |

**New tests:**

| File | Responsibility |
|---|---|
| `test/native/test_modem_at/engine_sources.cpp` | Unity-build of the pure units, following `test_console_dos`'s pattern. |
| `test/native/test_modem_at/libtelnet_source.c` | libtelnet compiled for the host (it is C, so it cannot join the C++ unity build). |
| `test/native/test_modem_at/test_modem_at.cpp` | The Unity test cases. |

---

### Task 1: AT command parser

The parser is the foundation every later task consumes. It is pure string work with no I/O, which is why it goes first and gets the heaviest test coverage.

**Files:**
- Create: `lib/modem/at_parser.h`
- Create: `lib/modem/at_parser.cpp`
- Create: `test/native/test_modem_at/engine_sources.cpp`
- Create: `test/native/test_modem_at/test_modem_at.cpp`
- Modify: `platformio.ini.sample`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct AtCommand { char prefix; char verb; std::string name; std::string mods; long number; long value; bool query; std::string arg; };`
  - `using AtLine = std::vector<AtCommand>;`
  - `bool at_parse(const std::string &line, AtLine &out, size_t *err_pos);`
  - `bool at_is_repeat(const std::string &line);`

- [ ] **Step 1: Add the native include paths**

In `platformio.ini.sample`, inside `[env:native]`'s `build_flags`, after the existing `-I components/ps2/src` line, add:

```ini
    ; test_modem_at: lib/modem's headers, included by name from the test file,
    ; and libtelnet's header for the negotiation filter (Task 6).
    -I lib/modem
    -I lib/telnet
```

Mirror the same two lines into your local `platformio.ini` — it is gitignored, so editing the sample alone does not affect your build.

- [ ] **Step 2: Write the failing test**

Create `test/native/test_modem_at/test_modem_at.cpp`:

```cpp
// Modem AT-layer unit tests.
//
// lib/console and lib/device are not compiled in the native environment, and
// lib/modem deliberately depends on neither -- so the parser, settings, result
// codes, escape detector, phonebook and telnet filter are all reachable here.
// What is NOT reachable is anything touching MStream, sockets or FreeRTOS: the
// dial path, TelnetMStream, ModemPort and the modem task are verified on
// hardware.
//
//   pio test -e native -f native/test_modem_at

#include <unity.h>

#include "at_parser.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- at_parser

void test_parse_bare_at_yields_no_commands(void)
{
    AtLine line;
    size_t err = 0;
    TEST_ASSERT_TRUE(at_parse("AT", line, &err));
    TEST_ASSERT_EQUAL_UINT(0, line.size());
}

void test_parse_is_case_insensitive(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("at", line, nullptr));
    TEST_ASSERT_TRUE(at_parse("aT", line, nullptr));
    TEST_ASSERT_TRUE(at_parse("At", line, nullptr));
}

void test_parse_rejects_a_line_not_starting_with_at(void)
{
    AtLine line;
    size_t err = 99;
    TEST_ASSERT_FALSE(at_parse("HELLO", line, &err));
    TEST_ASSERT_EQUAL_UINT(0, err);
}

void test_parse_dial_with_quoted_host(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATD\"bbs.example.com:23\"", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('D', line[0].verb);
    TEST_ASSERT_EQUAL_CHAR(0, line[0].prefix);
    TEST_ASSERT_EQUAL_STRING("", line[0].mods.c_str());
    TEST_ASSERT_EQUAL_STRING("bbs.example.com:23", line[0].arg.c_str());
}

void test_parse_dial_modifier_is_separated_from_the_argument(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATDT\"host:23\"", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('D', line[0].verb);
    TEST_ASSERT_EQUAL_STRING("T", line[0].mods.c_str());
    TEST_ASSERT_EQUAL_STRING("host:23", line[0].arg.c_str());
}

void test_parse_dial_by_phonebook_number(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATD5", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('D', line[0].verb);
    TEST_ASSERT_EQUAL_STRING("5", line[0].arg.c_str());
}

// The reason the parser exists rather than reusing esp_console_run()'s
// splitter: that splitter drops arguments past CONSOLE_MAX_CMDLINE_ARGS and
// only strips a leading quote, so this line does not survive it.
void test_parse_multiple_commands_on_one_line(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT&S62=1DT\"coffeemud.net:23\"", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(2, line.size());

    TEST_ASSERT_EQUAL_CHAR('&', line[0].prefix);
    TEST_ASSERT_EQUAL_CHAR('S', line[0].verb);
    TEST_ASSERT_EQUAL_INT(62, line[0].number);
    TEST_ASSERT_EQUAL_INT(1, line[0].value);

    TEST_ASSERT_EQUAL_CHAR('D', line[1].verb);
    TEST_ASSERT_EQUAL_STRING("T", line[1].mods.c_str());
    TEST_ASSERT_EQUAL_STRING("coffeemud.net:23", line[1].arg.c_str());
}

void test_parse_s_register_assignment(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATS12=50", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('S', line[0].verb);
    TEST_ASSERT_EQUAL_INT(12, line[0].number);
    TEST_ASSERT_EQUAL_INT(50, line[0].value);
    TEST_ASSERT_FALSE(line[0].query);
}

void test_parse_s_register_query(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATS12?", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('S', line[0].verb);
    TEST_ASSERT_EQUAL_INT(12, line[0].number);
    TEST_ASSERT_TRUE(line[0].query);
    TEST_ASSERT_EQUAL_INT(-1, line[0].value);
}

void test_parse_ampersand_and_plus_prefixed_commands(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT&W", line, nullptr));
    TEST_ASSERT_EQUAL_CHAR('&', line[0].prefix);
    TEST_ASSERT_EQUAL_CHAR('W', line[0].verb);

    TEST_ASSERT_TRUE(at_parse("AT+SHELL", line, nullptr));
    TEST_ASSERT_EQUAL_CHAR('+', line[0].prefix);
    TEST_ASSERT_EQUAL_STRING("SHELL", line[0].name.c_str());
}

void test_parse_plus_command_name_is_uppercased(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("at+shell", line, nullptr));
    TEST_ASSERT_EQUAL_STRING("SHELL", line[0].name.c_str());
}

void test_parse_numeric_suffix_defaults_to_zero_when_absent(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATE", line, nullptr));
    TEST_ASSERT_EQUAL_CHAR('E', line[0].verb);
    TEST_ASSERT_EQUAL_INT(0, line[0].number);

    TEST_ASSERT_TRUE(at_parse("ATE1", line, nullptr));
    TEST_ASSERT_EQUAL_INT(1, line[0].number);
}

void test_parse_whitespace_between_commands_is_ignored(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT E0 Q0 V1", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(3, line.size());
    TEST_ASSERT_EQUAL_CHAR('E', line[0].verb);
    TEST_ASSERT_EQUAL_CHAR('Q', line[1].verb);
    TEST_ASSERT_EQUAL_CHAR('V', line[2].verb);
}

// The phonebook store form: ATP1="host:23",T -- the modifiers follow the
// quoted argument, which is how a user expects to type it.
void test_parse_phonebook_store_with_trailing_modifiers(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATP1=\"bbs.example.com:23\",T", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('P', line[0].verb);
    TEST_ASSERT_EQUAL_INT(1, line[0].number);
    TEST_ASSERT_EQUAL_STRING("bbs.example.com:23", line[0].arg.c_str());
    TEST_ASSERT_EQUAL_STRING("T", line[0].mods.c_str());
}

void test_parse_phonebook_store_without_modifiers(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATP1=\"bbs.example.com:23\"", line, nullptr));
    TEST_ASSERT_EQUAL_STRING("bbs.example.com:23", line[0].arg.c_str());
    TEST_ASSERT_EQUAL_STRING("", line[0].mods.c_str());
}

// ATP1= with nothing after it deletes entry 1. The empty argument must be
// distinguishable from ATP1 (which is not a valid form at all).
void test_parse_phonebook_delete_form(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATP1=", line, nullptr));
    TEST_ASSERT_EQUAL_CHAR('P', line[0].verb);
    TEST_ASSERT_EQUAL_INT(1, line[0].number);
    TEST_ASSERT_TRUE(line[0].arg.empty());
    TEST_ASSERT_TRUE(line[0].assign);
}

// Bare ATP lists. It has no '=' at all, which is what tells it apart from the
// delete form above.
void test_parse_phonebook_list_form(void)
{
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATP", line, nullptr));
    TEST_ASSERT_EQUAL_CHAR('P', line[0].verb);
    TEST_ASSERT_FALSE(line[0].assign);
}

void test_parse_unterminated_quote_is_an_error_at_the_quote(void)
{
    AtLine line;
    size_t err = 0;
    TEST_ASSERT_FALSE(at_parse("ATD\"host:23", line, &err));
    TEST_ASSERT_EQUAL_UINT(3, err);
}

void test_parse_unknown_verb_is_an_error_at_the_verb(void)
{
    AtLine line;
    size_t err = 0;
    TEST_ASSERT_FALSE(at_parse("ATE0@", line, &err));
    TEST_ASSERT_EQUAL_UINT(4, err);
}

void test_repeat_line_is_recognised_without_an_at_prefix(void)
{
    TEST_ASSERT_TRUE(at_is_repeat("A/"));
    TEST_ASSERT_TRUE(at_is_repeat("a/"));
    TEST_ASSERT_FALSE(at_is_repeat("AT"));
    TEST_ASSERT_FALSE(at_is_repeat("A"));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_parse_bare_at_yields_no_commands);
    RUN_TEST(test_parse_is_case_insensitive);
    RUN_TEST(test_parse_rejects_a_line_not_starting_with_at);
    RUN_TEST(test_parse_dial_with_quoted_host);
    RUN_TEST(test_parse_dial_modifier_is_separated_from_the_argument);
    RUN_TEST(test_parse_dial_by_phonebook_number);
    RUN_TEST(test_parse_multiple_commands_on_one_line);
    RUN_TEST(test_parse_s_register_assignment);
    RUN_TEST(test_parse_s_register_query);
    RUN_TEST(test_parse_ampersand_and_plus_prefixed_commands);
    RUN_TEST(test_parse_plus_command_name_is_uppercased);
    RUN_TEST(test_parse_numeric_suffix_defaults_to_zero_when_absent);
    RUN_TEST(test_parse_whitespace_between_commands_is_ignored);
    RUN_TEST(test_parse_phonebook_store_with_trailing_modifiers);
    RUN_TEST(test_parse_phonebook_store_without_modifiers);
    RUN_TEST(test_parse_phonebook_delete_form);
    RUN_TEST(test_parse_phonebook_list_form);
    RUN_TEST(test_parse_unterminated_quote_is_an_error_at_the_quote);
    RUN_TEST(test_parse_unknown_verb_is_an_error_at_the_verb);
    RUN_TEST(test_repeat_line_is_recognised_without_an_at_prefix);

    return UNITY_END();
}
```

Create `test/native/test_modem_at/engine_sources.cpp`:

```cpp
// Pulls in the exact translation units the modem AT tests need, by
// #include-ing the real .cpp files by relative path. Same approach as
// test/native/test_console_dos/engine_sources.cpp -- see that file for why
// PlatformIO's library dependency finder cannot be used here.
//
// Everything included below is deliberately free of ESP-IDF, MStream and
// lib/console dependencies. If a future edit makes one of these files pull in
// FreeRTOS or MFSOwner, this suite stops building -- which is the point.

// ENABLE_MODEM gates the firmware build. The native suite always wants these
// units, so define it here rather than in the native env's build_flags, which
// would also switch on code paths that need ESP-IDF.
#define ENABLE_MODEM 1

#include "../../../lib/modem/at_parser.cpp"
```

- [ ] **Step 3: Run test to verify it fails**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: FAIL — compilation error, `at_parser.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `lib/modem/at_parser.h` (GPL header first, then):

```cpp
// AT command line parser.
//
// One line may carry several commands: "AT&S62=1DT\"host:23\"" is an
// S-register assignment followed by a dial. This is exactly why the modem
// cannot reuse esp_console_run()'s splitter, which drops arguments past
// CONSOLE_MAX_CMDLINE_ARGS and only strips a leading quote.
//
// Pure string work. No I/O, no ESP-IDF, no MStream -- so it is reachable from
// test/native/test_modem_at.

#ifndef MEATLOAF_MODEM_AT_PARSER
#define MEATLOAF_MODEM_AT_PARSER

#ifdef ENABLE_MODEM

#include <string>
#include <vector>

struct AtCommand
{
    // 0 for a plain command, '&' for AT&W, '+' for AT+SHELL.
    char prefix = 0;

    // The command letter, uppercased. Unused ('\0') for '+' commands, which
    // carry a word rather than a letter.
    char verb = 0;

    // For '+' commands only: the word after the '+', uppercased. "SHELL".
    std::string name;

    // Modifier letters. For ATDT these sit between the verb and the argument;
    // for ATP they follow it as ",T". Uppercased, in the order written.
    std::string mods;

    // Numeric suffix (ATE1 -> 1, ATS12=50 -> 12). 0 when a suffix is allowed
    // but absent, which is what a real modem assumes.
    long number = 0;

    // Right-hand side of an S-register assignment. -1 when there is none.
    long value = -1;

    // True for the "ATS12?" query form.
    bool query = false;

    // True when an '=' was present. Distinguishes "ATP1=" (delete entry 1)
    // from "ATP" (list), which otherwise both have an empty arg.
    bool assign = false;

    // Contents of a quoted argument, or the digits of a phonebook dial.
    std::string arg;
};

using AtLine = std::vector<AtCommand>;

// Parses one line into its commands. The line must begin with "AT" in any
// case; a bare "AT" is valid and yields an empty list.
//
// On failure returns false and, when err_pos is non-null, writes the offset of
// the offending character. The caller reports ERROR; it does not need to say
// where, but the offset makes the failure debuggable from a log.
bool at_parse(const std::string &line, AtLine &out, size_t *err_pos);

// True for the "A/" repeat-last-line form, which carries no AT prefix and so
// cannot go through at_parse().
bool at_is_repeat(const std::string &line);

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_AT_PARSER
```

- [ ] **Step 5: Write the implementation**

Create `lib/modem/at_parser.cpp` (GPL header first, then):

```cpp
#include "at_parser.h"

#ifdef ENABLE_MODEM

#include <cctype>

namespace
{

char upper(char c)
{
    return (char)std::toupper((unsigned char)c);
}

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

// Verbs that take an optional numeric suffix and nothing else.
bool verb_takes_number(char prefix, char v)
{
    if (prefix == '&')
        return v == 'W' || v == 'F';
    return v == 'E' || v == 'Q' || v == 'V' || v == 'X' || v == 'Z' ||
           v == 'O' || v == 'H' || v == 'I' || v == 'A';
}

// Reads consecutive digits starting at i, advancing i. Returns -1 when there
// are none, so the caller can tell "absent" from a literal 0.
long read_number(const std::string &s, size_t &i)
{
    size_t start = i;
    long n = 0;
    while (i < s.size() && is_digit(s[i]))
    {
        n = n * 10 + (s[i] - '0');
        ++i;
        // A modem's numbers are all small. Clamp rather than overflow.
        if (n > 1000000)
            n = 1000000;
    }
    return (i == start) ? -1 : n;
}

// Reads a "..." argument starting at the opening quote. Returns false when it
// is never closed, leaving i at the opening quote for the error report.
bool read_quoted(const std::string &s, size_t &i, std::string &out)
{
    size_t quote_at = i;
    ++i;
    while (i < s.size())
    {
        if (s[i] == '"')
        {
            ++i;
            return true;
        }
        out.push_back(s[i]);
        ++i;
    }
    i = quote_at;
    return false;
}

} // namespace

bool at_is_repeat(const std::string &line)
{
    return line.size() == 2 && upper(line[0]) == 'A' && line[1] == '/';
}

bool at_parse(const std::string &line, AtLine &out, size_t *err_pos)
{
    out.clear();

    auto fail = [&](size_t pos) {
        if (err_pos)
            *err_pos = pos;
        out.clear();
        return false;
    };

    if (line.size() < 2 || upper(line[0]) != 'A' || upper(line[1]) != 'T')
        return fail(0);

    size_t i = 2;
    while (i < line.size())
    {
        if (std::isspace((unsigned char)line[i]))
        {
            ++i;
            continue;
        }

        AtCommand cmd;
        size_t cmd_start = i;

        // ---- AT+NAME -------------------------------------------------------
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
            out.push_back(cmd);
            continue;
        }

        // ---- AT&X ----------------------------------------------------------
        if (line[i] == '&')
        {
            cmd.prefix = '&';
            ++i;
            if (i >= line.size() || !std::isalpha((unsigned char)line[i]))
                return fail(cmd_start);
            cmd.verb = upper(line[i]);
            ++i;
        }
        else
        {
            if (!std::isalpha((unsigned char)line[i]))
                return fail(i);
            cmd.verb = upper(line[i]);
            ++i;
        }

        // ---- S<n>=<v> and S<n>? -------------------------------------------
        // AT&Snn=v is the same command with an ampersand prefix; Zimodem
        // writes telnet mode that way and terminal programs copy it, so it
        // must parse identically to ATSnn=v.
        if (cmd.verb == 'S')
        {
            long n = read_number(line, i);
            if (n < 0)
                return fail(cmd_start);
            cmd.number = n;

            if (i < line.size() && line[i] == '=')
            {
                ++i;
                cmd.assign = true;
                long v = read_number(line, i);
                if (v < 0)
                    return fail(i);
                cmd.value = v;
            }
            else if (i < line.size() && line[i] == '?')
            {
                ++i;
                cmd.query = true;
            }
            else
            {
                return fail(i);
            }

            out.push_back(cmd);
            continue;
        }

        // ---- ATP, ATPn="host:port"[,mods] ---------------------------------
        if (cmd.prefix == 0 && cmd.verb == 'P')
        {
            long n = read_number(line, i);
            cmd.number = (n < 0) ? 0 : n;

            if (i < line.size() && line[i] == '=')
            {
                ++i;
                cmd.assign = true;

                if (i < line.size() && line[i] == '"')
                {
                    if (!read_quoted(line, i, cmd.arg))
                        return fail(i);

                    // Modifiers follow the quoted argument here, unlike ATD
                    // where they precede it -- ATP1="host:23",T is how a user
                    // expects to type it.
                    if (i < line.size() && line[i] == ',')
                    {
                        ++i;
                        while (i < line.size() && std::isalpha((unsigned char)line[i]))
                        {
                            char m = upper(line[i]);
                            if (m != 'T' && m != 'E')
                                return fail(i);
                            cmd.mods.push_back(m);
                            ++i;
                        }
                    }
                }
                // An '=' with nothing after it is the delete form; arg stays
                // empty and assign distinguishes it from a bare ATP.
            }

            out.push_back(cmd);
            continue;
        }

        // ---- ATD[mods]"host:port" or ATD<digits> ---------------------------
        if (cmd.prefix == 0 && cmd.verb == 'D')
        {
            // Phase 1 accepts T (telnet) and E (local echo). P, X and S are
            // phase 2 and later; they are rejected rather than silently
            // ignored, so a user is told rather than surprised.
            while (i < line.size() && std::isalpha((unsigned char)line[i]))
            {
                char m = upper(line[i]);
                if (m != 'T' && m != 'E')
                    return fail(i);
                cmd.mods.push_back(m);
                ++i;
            }

            if (i < line.size() && line[i] == '"')
            {
                if (!read_quoted(line, i, cmd.arg))
                    return fail(i);
            }
            else
            {
                // Bare digits are a phonebook entry. Kept as a string because
                // leading zeros are significant to the lookup.
                while (i < line.size() && is_digit(line[i]))
                {
                    cmd.arg.push_back(line[i]);
                    ++i;
                }
            }

            out.push_back(cmd);
            continue;
        }

        // ---- everything else: an optional numeric suffix --------------------
        if (!verb_takes_number(cmd.prefix, cmd.verb))
            return fail(cmd_start);

        long n = read_number(line, i);
        cmd.number = (n < 0) ? 0 : n;
        out.push_back(cmd);
    }

    return true;
}

#endif // ENABLE_MODEM
```

- [ ] **Step 6: Run test to verify it passes**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: PASS, 20 cases, 0 failures.

- [ ] **Step 7: Commit**

```bash
git add lib/modem/at_parser.h lib/modem/at_parser.cpp \
        test/native/test_modem_at/engine_sources.cpp \
        test/native/test_modem_at/test_modem_at.cpp \
        platformio.ini.sample
git commit -m "feat(modem): AT command line parser

One line may carry several commands (AT&S62=1DT\"host:23\"), which is
why the modem cannot reuse esp_console_run()'s splitter - that drops
arguments past CONSOLE_MAX_CMDLINE_ARGS and only strips a leading quote.

Pure string work with no I/O, so the native suite reaches it. ATD takes
its modifiers before the argument and ATP after it, matching how each is
naturally typed. Phase 1 rejects the P, X and S dial modifiers rather
than ignoring them; they arrive in later phases."
```

---

### Task 2: S-registers and settings

**Files:**
- Create: `lib/modem/at_settings.h`
- Create: `lib/modem/at_settings.cpp`
- Modify: `test/native/test_modem_at/engine_sources.cpp`
- Modify: `test/native/test_modem_at/test_modem_at.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct AtSettings` with public fields `uint8_t s[128]; bool echo; bool quiet; bool verbose; uint8_t xlevel;`
  - `void factory();`
  - `bool setRegister(long n, long v);`
  - `long getRegister(long n) const;`
  - `std::map<std::string, long> toKeyValues() const;`
  - `void fromKeyValues(const std::map<std::string, long> &kv);`
  - Constants `AT_S_AUTOANSWER=0, AT_S_ESCCHAR=2, AT_S_CR=3, AT_S_LF=4, AT_S_BS=5, AT_S_CONNTIMEOUT=7, AT_S_GUARD=12, AT_S_AUTOSTREAM=41, AT_S_TELNET=62`

- [ ] **Step 1: Write the failing test**

Append to `test/native/test_modem_at/test_modem_at.cpp`, before `main`:

```cpp
#include "at_settings.h"

// --------------------------------------------------------------- at_settings

void test_settings_factory_defaults(void)
{
    AtSettings s;
    s.factory();

    TEST_ASSERT_EQUAL_INT(0,  s.getRegister(AT_S_AUTOANSWER));
    TEST_ASSERT_EQUAL_INT(43, s.getRegister(AT_S_ESCCHAR));   // '+'
    TEST_ASSERT_EQUAL_INT(13, s.getRegister(AT_S_CR));
    TEST_ASSERT_EQUAL_INT(10, s.getRegister(AT_S_LF));
    TEST_ASSERT_EQUAL_INT(8,  s.getRegister(AT_S_BS));
    TEST_ASSERT_EQUAL_INT(60, s.getRegister(AT_S_CONNTIMEOUT));
    TEST_ASSERT_EQUAL_INT(50, s.getRegister(AT_S_GUARD));
    TEST_ASSERT_EQUAL_INT(0,  s.getRegister(AT_S_AUTOSTREAM));
    TEST_ASSERT_EQUAL_INT(0,  s.getRegister(AT_S_TELNET));

    TEST_ASSERT_TRUE(s.echo);
    TEST_ASSERT_FALSE(s.quiet);
    TEST_ASSERT_TRUE(s.verbose);
    TEST_ASSERT_EQUAL_UINT(4, s.xlevel);
}

void test_settings_register_round_trip(void)
{
    AtSettings s;
    s.factory();
    TEST_ASSERT_TRUE(s.setRegister(12, 25));
    TEST_ASSERT_EQUAL_INT(25, s.getRegister(12));
}

// The array is 128 entries. An out-of-range index must be refused rather than
// written, which would corrupt whatever follows the struct.
void test_settings_rejects_out_of_range_register(void)
{
    AtSettings s;
    s.factory();
    TEST_ASSERT_FALSE(s.setRegister(128, 1));
    TEST_ASSERT_FALSE(s.setRegister(-1, 1));
    TEST_ASSERT_EQUAL_INT(-1, s.getRegister(128));
    TEST_ASSERT_EQUAL_INT(-1, s.getRegister(-1));
}

// Registers are one byte. A value past 255 is a user error, not something to
// silently truncate into a different setting.
void test_settings_rejects_out_of_range_value(void)
{
    AtSettings s;
    s.factory();
    TEST_ASSERT_FALSE(s.setRegister(12, 256));
    TEST_ASSERT_FALSE(s.setRegister(12, -1));
    TEST_ASSERT_EQUAL_INT(50, s.getRegister(12));
}

void test_settings_serialize_round_trip(void)
{
    AtSettings a;
    a.factory();
    a.setRegister(AT_S_GUARD, 25);
    a.setRegister(AT_S_TELNET, 1);
    a.echo = false;
    a.verbose = false;
    a.xlevel = 1;

    AtSettings b;
    b.factory();
    b.fromKeyValues(a.toKeyValues());

    TEST_ASSERT_EQUAL_INT(25, b.getRegister(AT_S_GUARD));
    TEST_ASSERT_EQUAL_INT(1,  b.getRegister(AT_S_TELNET));
    TEST_ASSERT_FALSE(b.echo);
    TEST_ASSERT_FALSE(b.verbose);
    TEST_ASSERT_EQUAL_UINT(1, b.xlevel);
}

// Only registers that differ from the factory default are serialized, so a
// saved config stays small and a later change of default is picked up.
void test_settings_serialization_omits_defaults(void)
{
    AtSettings s;
    s.factory();
    auto kv = s.toKeyValues();

    TEST_ASSERT_EQUAL_UINT(0, kv.count("s12"));
    TEST_ASSERT_EQUAL_UINT(0, kv.count("s3"));

    s.setRegister(AT_S_GUARD, 25);
    kv = s.toKeyValues();
    TEST_ASSERT_EQUAL_UINT(1, kv.count("s12"));
    TEST_ASSERT_EQUAL_INT(25, kv["s12"]);
}

// A config file written by a future version, or hand-edited, must not be able
// to put the modem into a state its own setters would refuse.
void test_settings_deserialization_ignores_bad_values(void)
{
    AtSettings s;
    s.factory();

    std::map<std::string, long> kv;
    kv["s12"] = 999;      // out of byte range
    kv["s999"] = 1;       // out of array range
    kv["xlevel"] = 47;    // out of 0-4 range
    kv["nonsense"] = 1;   // unknown key
    s.fromKeyValues(kv);

    TEST_ASSERT_EQUAL_INT(50, s.getRegister(AT_S_GUARD));
    TEST_ASSERT_EQUAL_UINT(4, s.xlevel);
}
```

Add to `main`, before `return UNITY_END();`:

```cpp
    RUN_TEST(test_settings_factory_defaults);
    RUN_TEST(test_settings_register_round_trip);
    RUN_TEST(test_settings_rejects_out_of_range_register);
    RUN_TEST(test_settings_rejects_out_of_range_value);
    RUN_TEST(test_settings_serialize_round_trip);
    RUN_TEST(test_settings_serialization_omits_defaults);
    RUN_TEST(test_settings_deserialization_ignores_bad_values);
```

Append to `engine_sources.cpp`:

```cpp
#include "../../../lib/modem/at_settings.cpp"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: FAIL — `at_settings.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `lib/modem/at_settings.h` (GPL header first, then):

```cpp
// Modem settings: the S-register array and the E/Q/V/X flags.
//
// Serialization is to a plain key-value map rather than to JSON, so this unit
// stays free of nlohmann and reachable from the native suite. modem.cpp does
// the mlConfig read and write.

#ifndef MEATLOAF_MODEM_AT_SETTINGS
#define MEATLOAF_MODEM_AT_SETTINGS

#ifdef ENABLE_MODEM

#include <cstdint>
#include <map>
#include <string>

// The S-registers phase 1 knows by name. Others are storable and reportable
// but have no behaviour attached.
enum : long
{
    AT_S_AUTOANSWER  = 0,   // rings before auto-answer (acted on in phase 3)
    AT_S_ESCCHAR     = 2,   // escape character, default '+'
    AT_S_CR          = 3,   // carriage-return character
    AT_S_LF          = 4,   // line-feed character
    AT_S_BS          = 5,   // backspace character
    AT_S_CONNTIMEOUT = 7,   // seconds to wait for a connection
    AT_S_GUARD       = 12,  // escape guard time, in fiftieths of a second
    AT_S_AUTOSTREAM  = 41,  // auto-stream incoming calls (phase 3)
    AT_S_TELNET      = 62,  // negotiate telnet on a plain ATD (Zimodem's S62)
};

struct AtSettings
{
    static constexpr long REGISTER_COUNT = 128;

    uint8_t s[REGISTER_COUNT];

    bool    echo    = true;   // ATE
    bool    quiet   = false;  // ATQ
    bool    verbose = true;   // ATV
    uint8_t xlevel  = 4;      // ATX

    // Restores every documented default. AT&F.
    void factory();

    // Refuses an index outside [0, REGISTER_COUNT) and a value outside
    // [0, 255]. Returning false is what makes the caller emit ERROR instead of
    // silently applying a different setting than the user asked for.
    bool setRegister(long n, long v);

    // Returns -1 for an out-of-range index.
    long getRegister(long n) const;

    // Only values differing from the factory default are emitted, so the saved
    // config stays small and a later change of default is inherited.
    std::map<std::string, long> toKeyValues() const;

    // Applies what it recognises and ignores the rest. A config written by a
    // future version, or hand-edited, must not be able to reach a state the
    // setters would refuse.
    void fromKeyValues(const std::map<std::string, long> &kv);
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_AT_SETTINGS
```

- [ ] **Step 4: Write the implementation**

Create `lib/modem/at_settings.cpp` (GPL header first, then):

```cpp
#include "at_settings.h"

#ifdef ENABLE_MODEM

#include <cstdlib>

namespace
{

// The one place a default lives. factory() and toKeyValues() both read it, so
// they cannot drift apart.
uint8_t default_register(long n)
{
    switch (n)
    {
    case AT_S_AUTOANSWER:  return 0;
    case AT_S_ESCCHAR:     return 43;   // '+'
    case AT_S_CR:          return 13;
    case AT_S_LF:          return 10;
    case AT_S_BS:          return 8;
    case AT_S_CONNTIMEOUT: return 60;
    case AT_S_GUARD:       return 50;   // fiftieths of a second == 1 s
    case AT_S_AUTOSTREAM:  return 0;
    case AT_S_TELNET:      return 0;
    default:               return 0;
    }
}

} // namespace

void AtSettings::factory()
{
    for (long n = 0; n < REGISTER_COUNT; ++n)
        s[n] = default_register(n);

    echo    = true;
    quiet   = false;
    verbose = true;
    xlevel  = 4;
}

bool AtSettings::setRegister(long n, long v)
{
    if (n < 0 || n >= REGISTER_COUNT)
        return false;
    if (v < 0 || v > 255)
        return false;
    s[n] = (uint8_t)v;
    return true;
}

long AtSettings::getRegister(long n) const
{
    if (n < 0 || n >= REGISTER_COUNT)
        return -1;
    return (long)s[n];
}

std::map<std::string, long> AtSettings::toKeyValues() const
{
    std::map<std::string, long> kv;

    for (long n = 0; n < REGISTER_COUNT; ++n)
    {
        if (s[n] != default_register(n))
            kv["s" + std::to_string(n)] = (long)s[n];
    }

    if (!echo)       kv["echo"]    = 0;
    if (quiet)       kv["quiet"]   = 1;
    if (!verbose)    kv["verbose"] = 0;
    if (xlevel != 4) kv["xlevel"]  = (long)xlevel;

    return kv;
}

void AtSettings::fromKeyValues(const std::map<std::string, long> &kv)
{
    for (const auto &pair : kv)
    {
        const std::string &k = pair.first;
        long v = pair.second;

        if (k.size() > 1 && k[0] == 's')
        {
            // strtol rather than std::stoi: ESP-IDF builds -fno-exceptions, so
            // a throwing conversion on malformed input becomes std::terminate.
            const char *start = k.c_str() + 1;
            char *end = nullptr;
            long n = std::strtol(start, &end, 10);
            if (end == start || *end != '\0')
                continue;

            setRegister(n, v);  // refuses out-of-range itself
            continue;
        }

        if (k == "echo")    { echo    = (v != 0); continue; }
        if (k == "quiet")   { quiet   = (v != 0); continue; }
        if (k == "verbose") { verbose = (v != 0); continue; }
        if (k == "xlevel")  { if (v >= 0 && v <= 4) xlevel = (uint8_t)v; continue; }
    }
}

#endif // ENABLE_MODEM
```

- [ ] **Step 5: Run test to verify it passes**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: PASS, 27 cases, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add lib/modem/at_settings.h lib/modem/at_settings.cpp \
        test/native/test_modem_at/engine_sources.cpp \
        test/native/test_modem_at/test_modem_at.cpp
git commit -m "feat(modem): S-registers and E/Q/V/X settings

Serializes to a plain key-value map rather than JSON so the unit stays
free of nlohmann and reachable from the native suite; modem.cpp does the
mlConfig glue.

Only non-default registers are emitted, so a saved config stays small
and a later change of default is inherited. Deserialization applies what
it recognises and ignores the rest - a hand-edited config must not reach
a state the setters would refuse. strtol, not std::stoi: ESP-IDF builds
-fno-exceptions, so a throw on malformed input is std::terminate."
```

---

### Task 3: Result codes

**Files:**
- Create: `lib/modem/at_result.h`
- Create: `lib/modem/at_result.cpp`
- Modify: `test/native/test_modem_at/engine_sources.cpp`
- Modify: `test/native/test_modem_at/test_modem_at.cpp`

**Interfaces:**
- Consumes: `AtSettings` from Task 2.
- Produces:
  - `enum class AtResult { OK=0, CONNECT=1, RING=2, NO_CARRIER=3, ERROR=4, NO_DIALTONE=6, BUSY=7, NO_ANSWER=8 };`
  - `std::string at_format_result(AtResult r, const AtSettings &s);`

- [ ] **Step 1: Write the failing test**

Append to `test/native/test_modem_at/test_modem_at.cpp`, before `main`:

```cpp
#include "at_result.h"

// ----------------------------------------------------------------- at_result

void test_result_verbose_is_wrapped_in_terminators(void)
{
    AtSettings s;
    s.factory();
    // Verbose form is <CR><LF>TEXT<CR><LF>. Terminal programs parse the
    // leading pair; omitting it makes responses run into the previous line.
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", at_format_result(AtResult::OK, s).c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n",
                             at_format_result(AtResult::NO_CARRIER, s).c_str());
}

void test_result_numeric_form(void)
{
    AtSettings s;
    s.factory();
    s.verbose = false;
    // Numeric form is CODE<CR> with no leading pair and no linefeed.
    TEST_ASSERT_EQUAL_STRING("0\r", at_format_result(AtResult::OK, s).c_str());
    TEST_ASSERT_EQUAL_STRING("3\r", at_format_result(AtResult::NO_CARRIER, s).c_str());
}

void test_result_quiet_suppresses_everything(void)
{
    AtSettings s;
    s.factory();
    s.quiet = true;
    TEST_ASSERT_EQUAL_STRING("", at_format_result(AtResult::OK, s).c_str());
    TEST_ASSERT_EQUAL_STRING("", at_format_result(AtResult::ERROR, s).c_str());
}

// S3 and S4 are the actual bytes sent, not decoration. A terminal set to
// carriage-return only needs S4 removed from the stream entirely.
void test_result_honours_s3_and_s4(void)
{
    AtSettings s;
    s.factory();
    s.setRegister(AT_S_LF, 0);   // 0 means "send nothing for LF"
    TEST_ASSERT_EQUAL_STRING("\rOK\r", at_format_result(AtResult::OK, s).c_str());

    s.factory();
    s.setRegister(AT_S_CR, 30);
    TEST_ASSERT_EQUAL_STRING("\x1E\nOK\x1E\n", at_format_result(AtResult::OK, s).c_str());
}

// X0 reports only the basic five. A suppressed code degrades to NO CARRIER
// rather than vanishing, which is what a real modem does and what a dialer
// script expects to see.
void test_result_x0_degrades_extended_codes_to_no_carrier(void)
{
    AtSettings s;
    s.factory();
    s.xlevel = 0;
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n",
                             at_format_result(AtResult::BUSY, s).c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n",
                             at_format_result(AtResult::NO_DIALTONE, s).c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n",
                             at_format_result(AtResult::NO_ANSWER, s).c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", at_format_result(AtResult::OK, s).c_str());
}

void test_result_x_levels_gate_each_extended_code(void)
{
    AtSettings s;
    s.factory();

    s.xlevel = 2;  // NO DIALTONE allowed, BUSY and NO ANSWER not
    TEST_ASSERT_EQUAL_STRING("\r\nNO DIALTONE\r\n",
                             at_format_result(AtResult::NO_DIALTONE, s).c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n",
                             at_format_result(AtResult::BUSY, s).c_str());

    s.xlevel = 3;  // BUSY now allowed, NO ANSWER still not
    TEST_ASSERT_EQUAL_STRING("\r\nBUSY\r\n",
                             at_format_result(AtResult::BUSY, s).c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n",
                             at_format_result(AtResult::NO_ANSWER, s).c_str());

    s.xlevel = 4;
    TEST_ASSERT_EQUAL_STRING("\r\nNO ANSWER\r\n",
                             at_format_result(AtResult::NO_ANSWER, s).c_str());
}

// The degradation must also apply in numeric mode, or a script reading codes
// gets a 7 the X level said it would never see.
void test_result_x_degradation_applies_in_numeric_mode(void)
{
    AtSettings s;
    s.factory();
    s.verbose = false;
    s.xlevel = 0;
    TEST_ASSERT_EQUAL_STRING("3\r", at_format_result(AtResult::BUSY, s).c_str());
}
```

Add to `main`:

```cpp
    RUN_TEST(test_result_verbose_is_wrapped_in_terminators);
    RUN_TEST(test_result_numeric_form);
    RUN_TEST(test_result_quiet_suppresses_everything);
    RUN_TEST(test_result_honours_s3_and_s4);
    RUN_TEST(test_result_x0_degrades_extended_codes_to_no_carrier);
    RUN_TEST(test_result_x_levels_gate_each_extended_code);
    RUN_TEST(test_result_x_degradation_applies_in_numeric_mode);
```

Append to `engine_sources.cpp`:

```cpp
#include "../../../lib/modem/at_result.cpp"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: FAIL — `at_result.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `lib/modem/at_result.h` (GPL header first, then):

```cpp
// Modem result codes.
//
// What reaches the terminal is a five-way product: V (verbose or numeric),
// Q (suppress), X (which subset is reportable), S3 (CR byte) and S4 (LF byte).
// Terminal programs do break on getting this wrong, which is why it is a
// separate unit with its own tests rather than a printf at each call site.

#ifndef MEATLOAF_MODEM_AT_RESULT
#define MEATLOAF_MODEM_AT_RESULT

#ifdef ENABLE_MODEM

#include <string>

#include "at_settings.h"

// Values are the numeric codes emitted under ATV0 and must not be renumbered.
enum class AtResult
{
    OK          = 0,
    CONNECT     = 1,
    RING        = 2,
    NO_CARRIER  = 3,
    ERROR       = 4,
    NO_DIALTONE = 6,
    BUSY        = 7,
    NO_ANSWER   = 8,
};

// Returns the exact bytes to send, or an empty string under ATQ1.
//
// A code the current X level does not report degrades to NO CARRIER rather
// than disappearing -- that is what a real modem does, and a dialer script
// waiting for any terminal result would otherwise hang.
std::string at_format_result(AtResult r, const AtSettings &s);

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_AT_RESULT
```

- [ ] **Step 4: Write the implementation**

Create `lib/modem/at_result.cpp` (GPL header first, then):

```cpp
#include "at_result.h"

#ifdef ENABLE_MODEM

namespace
{

const char *verbose_text(AtResult r)
{
    switch (r)
    {
    case AtResult::OK:          return "OK";
    case AtResult::CONNECT:     return "CONNECT";
    case AtResult::RING:        return "RING";
    case AtResult::NO_CARRIER:  return "NO CARRIER";
    case AtResult::ERROR:       return "ERROR";
    case AtResult::NO_DIALTONE: return "NO DIALTONE";
    case AtResult::BUSY:        return "BUSY";
    case AtResult::NO_ANSWER:   return "NO ANSWER";
    }
    return "ERROR";
}

// The minimum X level at which each code is reportable. Below it the code
// degrades to NO CARRIER.
uint8_t min_xlevel(AtResult r)
{
    switch (r)
    {
    case AtResult::NO_DIALTONE: return 2;
    case AtResult::BUSY:        return 3;
    case AtResult::NO_ANSWER:   return 4;
    default:                    return 0;
    }
}

// S3 and S4 are the bytes themselves. A register set to 0 means "emit
// nothing", which is how a carriage-return-only terminal is served.
void append_eol(std::string &out, const AtSettings &s)
{
    long cr = s.getRegister(AT_S_CR);
    long lf = s.getRegister(AT_S_LF);
    if (cr > 0) out.push_back((char)cr);
    if (lf > 0) out.push_back((char)lf);
}

} // namespace

std::string at_format_result(AtResult r, const AtSettings &s)
{
    if (s.quiet)
        return std::string();

    if (s.xlevel < min_xlevel(r))
        r = AtResult::NO_CARRIER;

    std::string out;

    if (s.verbose)
    {
        append_eol(out, s);
        out += verbose_text(r);
        append_eol(out, s);
        return out;
    }

    out += std::to_string((int)r);
    long cr = s.getRegister(AT_S_CR);
    if (cr > 0)
        out.push_back((char)cr);
    return out;
}

#endif // ENABLE_MODEM
```

- [ ] **Step 5: Run test to verify it passes**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: PASS, 34 cases, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add lib/modem/at_result.h lib/modem/at_result.cpp \
        test/native/test_modem_at/engine_sources.cpp \
        test/native/test_modem_at/test_modem_at.cpp
git commit -m "feat(modem): result code formatting

The bytes a terminal receives are a five-way product of V, Q, X, S3 and
S4. A code the current X level does not report degrades to NO CARRIER
rather than vanishing, in both verbose and numeric mode - a dialer
script waiting for any terminal result would otherwise hang.

S3/S4 set to 0 emit nothing, which is how a carriage-return-only
terminal is served."
```

---

### Task 4: Escape sequence detector

The `+++` rule is not "see three plus signs", and getting it wrong is a class of bug that is expensive to debug on hardware. The clock is injected so the whole thing is a pure function of bytes and timestamps.

**Files:**
- Create: `lib/modem/escape.h`
- Create: `lib/modem/escape.cpp`
- Modify: `test/native/test_modem_at/engine_sources.cpp`
- Modify: `test/native/test_modem_at/test_modem_at.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `class EscapeDetector` with `void configure(uint8_t esc_char, uint16_t guard_units);`, `void reset();`, `Verdict feed(uint8_t b, uint32_t now_ms, std::string &forward);`, `Verdict tick(uint32_t now_ms, std::string &forward);`
  - `enum class EscapeDetector::Verdict { NONE, ESCAPED };`

- [ ] **Step 1: Write the failing test**

Append to `test/native/test_modem_at/test_modem_at.cpp`, before `main`:

```cpp
#include "escape.h"

// -------------------------------------------------------------------- escape

// Guard is in fiftieths of a second, so the default 50 is 1000 ms.
static EscapeDetector make_detector()
{
    EscapeDetector d;
    d.configure('+', 50);
    d.reset();
    return d;
}

void test_escape_plain_data_passes_straight_through(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.feed('h', 5000, fwd));
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.feed('i', 5010, fwd));
    TEST_ASSERT_EQUAL_STRING("hi", fwd.c_str());
}

void test_escape_full_sequence_is_detected(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    // Data, then one second of silence, then +++, then another second.
    d.feed('x', 1000, fwd);
    fwd.clear();

    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.feed('+', 3000, fwd));
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.feed('+', 3100, fwd));
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.feed('+', 3200, fwd));

    // Nothing forwarded yet -- the three bytes are held until the trailing
    // guard expires, because they are still potentially data.
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());

    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.tick(3900, fwd));
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::ESCAPED == d.tick(4300, fwd));
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());
}

// The near-miss that matters most: a user typing "+++" inside a sentence must
// neither escape nor lose the characters.
void test_escape_data_after_the_sequence_releases_the_held_bytes(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    d.feed('x', 1000, fwd);
    fwd.clear();

    d.feed('+', 3000, fwd);
    d.feed('+', 3100, fwd);
    d.feed('+', 3200, fwd);
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());

    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.feed('y', 3300, fwd));
    TEST_ASSERT_EQUAL_STRING("+++y", fwd.c_str());
}

// Without a leading guard the run is data, not an escape.
void test_escape_needs_a_leading_guard(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    d.feed('x', 3000, fwd);
    fwd.clear();

    d.feed('+', 3050, fwd);   // only 50 ms after data
    d.feed('+', 3100, fwd);
    d.feed('+', 3150, fwd);
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.tick(5000, fwd));
    TEST_ASSERT_EQUAL_STRING("+++", fwd.c_str());
}

// Too slow between the escape characters: they are data.
void test_escape_characters_spaced_wider_than_the_guard_are_data(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    d.feed('+', 3000, fwd);
    d.feed('+', 4500, fwd);   // 1500 ms > 1000 ms guard
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.tick(6000, fwd));
    // The first + timed out as data; the second started a fresh candidate and
    // then timed out too. Both must reach the far end, in order.
    TEST_ASSERT_EQUAL_STRING("++", fwd.c_str());
}

void test_escape_two_characters_then_silence_are_released_as_data(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    d.feed('+', 3000, fwd);
    d.feed('+', 3100, fwd);
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());

    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.tick(4500, fwd));
    TEST_ASSERT_EQUAL_STRING("++", fwd.c_str());
}

void test_escape_four_characters_are_not_an_escape(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    d.feed('x', 1000, fwd);
    fwd.clear();

    d.feed('+', 3000, fwd);
    d.feed('+', 3100, fwd);
    d.feed('+', 3200, fwd);
    d.feed('+', 3300, fwd);
    TEST_ASSERT_EQUAL_STRING("++++", fwd.c_str());
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.tick(5000, fwd));
}

// S2 is configurable, so the detector must not hard-code '+'.
void test_escape_uses_the_configured_character(void)
{
    EscapeDetector d;
    d.configure('#', 50);
    d.reset();

    std::string fwd;
    d.feed('#', 3000, fwd);
    d.feed('#', 3100, fwd);
    d.feed('#', 3200, fwd);
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::ESCAPED == d.tick(4300, fwd));
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());
}

// S2 above 127 disables the escape entirely, per the Hayes convention.
void test_escape_disabled_when_character_is_out_of_range(void)
{
    EscapeDetector d;
    d.configure(200, 50);
    d.reset();

    std::string fwd;
    d.feed(200, 3000, fwd);
    d.feed(200, 3100, fwd);
    d.feed(200, 3200, fwd);
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.tick(4300, fwd));
    TEST_ASSERT_EQUAL_UINT(3, fwd.size());
}

// reset() is called on connect and on return to stream mode, so a sequence
// cannot span two sessions.
void test_escape_reset_drops_held_bytes(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    d.feed('x', 1000, fwd);
    fwd.clear();
    d.feed('+', 3000, fwd);
    d.feed('+', 3100, fwd);

    d.reset();
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.tick(9000, fwd));
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());
}
```

Add to `main`:

```cpp
    RUN_TEST(test_escape_plain_data_passes_straight_through);
    RUN_TEST(test_escape_full_sequence_is_detected);
    RUN_TEST(test_escape_data_after_the_sequence_releases_the_held_bytes);
    RUN_TEST(test_escape_needs_a_leading_guard);
    RUN_TEST(test_escape_characters_spaced_wider_than_the_guard_are_data);
    RUN_TEST(test_escape_two_characters_then_silence_are_released_as_data);
    RUN_TEST(test_escape_four_characters_are_not_an_escape);
    RUN_TEST(test_escape_uses_the_configured_character);
    RUN_TEST(test_escape_disabled_when_character_is_out_of_range);
    RUN_TEST(test_escape_reset_drops_held_bytes);
```

Append to `engine_sources.cpp`:

```cpp
#include "../../../lib/modem/escape.cpp"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: FAIL — `escape.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `lib/modem/escape.h` (GPL header first, then):

```cpp
// The +++ escape detector.
//
// The rule is not "three escape characters". It is: at least S12 of silence,
// then exactly three S2 characters each less than S12 apart, then at least S12
// of silence. Until that trailing guard expires the three bytes are HELD --
// if data follows, they were ordinary data and must be forwarded intact, in
// order. A user typing "+++" mid-sentence must neither drop into command mode
// nor lose the characters.
//
// S12 is in fiftieths of a second, per Hayes convention, so the default of 50
// is one second.
//
// The clock is injected, so this is a pure function of (bytes, timestamps) and
// is fully covered by test/native/test_modem_at.

#ifndef MEATLOAF_MODEM_ESCAPE
#define MEATLOAF_MODEM_ESCAPE

#ifdef ENABLE_MODEM

#include <cstdint>
#include <string>

class EscapeDetector
{
public:
    enum class Verdict
    {
        NONE,     // nothing to report; check `forward` for bytes to send on
        ESCAPED,  // a complete escape sequence completed its trailing guard
    };

    // guard_units is S12, in fiftieths of a second. An esc_char above 127
    // disables escape detection entirely, per the Hayes convention.
    void configure(uint8_t esc_char, uint16_t guard_units);

    // Clears held bytes and returns to the idle state. Call on connect and on
    // return to stream mode, so a sequence cannot span two sessions.
    void reset();

    // One byte arrived from the terminal at now_ms. Appends any bytes that
    // should reach the remote to `forward` (which is never cleared here, so a
    // caller may batch).
    Verdict feed(uint8_t b, uint32_t now_ms, std::string &forward);

    // No byte arrived. Call this regularly -- it is what completes the
    // trailing guard and what releases held bytes that turned out to be data.
    Verdict tick(uint32_t now_ms, std::string &forward);

private:
    enum class State
    {
        IDLE,      // no candidate in progress
        COUNTING,  // one or two escape characters held
        TRAILING,  // three held, waiting out the trailing guard
    };

    void release(std::string &forward);

    uint8_t  esc_char_     = 43;   // '+'
    uint32_t guard_ms_     = 1000; // S12 default of 50 fiftieths
    State    state_        = State::IDLE;
    uint8_t  held_         = 0;    // escape characters currently held (0-3)
    uint32_t last_byte_ms_ = 0;    // when the last byte of ANY kind arrived
    bool     seen_any_     = false;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_ESCAPE
```

- [ ] **Step 4: Write the implementation**

Create `lib/modem/escape.cpp` (GPL header first, then):

```cpp
#include "escape.h"

#ifdef ENABLE_MODEM

void EscapeDetector::configure(uint8_t esc_char, uint16_t guard_units)
{
    esc_char_ = esc_char;
    // One fiftieth of a second is 20 ms.
    guard_ms_ = (uint32_t)guard_units * 20u;
}

void EscapeDetector::reset()
{
    state_ = State::IDLE;
    held_ = 0;
    last_byte_ms_ = 0;
    seen_any_ = false;
}

void EscapeDetector::release(std::string &forward)
{
    for (uint8_t i = 0; i < held_; ++i)
        forward.push_back((char)esc_char_);
    held_ = 0;
    state_ = State::IDLE;
}

EscapeDetector::Verdict EscapeDetector::feed(uint8_t b, uint32_t now_ms,
                                             std::string &forward)
{
    // S2 above 127 disables the escape. Everything is data.
    if (esc_char_ > 127)
    {
        forward.push_back((char)b);
        last_byte_ms_ = now_ms;
        seen_any_ = true;
        return Verdict::NONE;
    }

    // Before any byte at all, treat the line as having been silent forever --
    // otherwise an escape typed as the very first thing after connect could
    // never start a candidate.
    uint32_t gap = seen_any_ ? (now_ms - last_byte_ms_) : guard_ms_;

    // A gap at or past the guard retires whatever candidate was in progress:
    // those bytes were data after all. Do this before classifying the new byte
    // so their order is preserved.
    if (state_ != State::IDLE && gap >= guard_ms_)
        release(forward);

    if (b == esc_char_)
    {
        switch (state_)
        {
        case State::IDLE:
            // A candidate can only START after a full guard of silence.
            if (gap >= guard_ms_)
            {
                held_ = 1;
                state_ = State::COUNTING;
            }
            else
            {
                forward.push_back((char)b);
            }
            break;

        case State::COUNTING:
            ++held_;
            if (held_ == 3)
                state_ = State::TRAILING;
            break;

        case State::TRAILING:
            // A fourth character means this was never an escape.
            release(forward);
            forward.push_back((char)b);
            break;
        }
    }
    else
    {
        // Any non-escape byte ends a candidate; the held bytes were data.
        if (state_ != State::IDLE)
            release(forward);
        forward.push_back((char)b);
    }

    last_byte_ms_ = now_ms;
    seen_any_ = true;
    return Verdict::NONE;
}

EscapeDetector::Verdict EscapeDetector::tick(uint32_t now_ms,
                                             std::string &forward)
{
    if (state_ == State::IDLE || !seen_any_)
        return Verdict::NONE;

    if (now_ms - last_byte_ms_ < guard_ms_)
        return Verdict::NONE;

    if (state_ == State::TRAILING)
    {
        // The trailing guard completed: a real escape. The held bytes are
        // consumed, never forwarded.
        held_ = 0;
        state_ = State::IDLE;
        return Verdict::ESCAPED;
    }

    // One or two characters that timed out. They were data.
    release(forward);
    return Verdict::NONE;
}

#endif // ENABLE_MODEM
```

- [ ] **Step 5: Run test to verify it passes**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: PASS, 44 cases, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add lib/modem/escape.h lib/modem/escape.cpp \
        test/native/test_modem_at/engine_sources.cpp \
        test/native/test_modem_at/test_modem_at.cpp
git commit -m "feat(modem): +++ escape detector with injected clock

The rule is a leading guard, exactly three escape characters each under
the guard apart, then a trailing guard. The three bytes are HELD until
that trailing guard expires - if data follows they were ordinary data
and must be forwarded intact and in order, so a user typing +++ inside
a sentence neither escapes nor loses characters.

The clock is a parameter, making this a pure function of bytes and
timestamps and fully covered natively. Escape-sequence defects are
subtle and hardware-only debugging of them is expensive."
```

---

### Task 5: Phonebook

**Files:**
- Create: `lib/modem/phonebook.h`
- Create: `lib/modem/phonebook.cpp`
- Modify: `test/native/test_modem_at/engine_sources.cpp`
- Modify: `test/native/test_modem_at/test_modem_at.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct PhonebookEntry { std::string number; std::string host; uint16_t port; std::string mods; };`
  - `class Phonebook` with `bool store(const std::string &number, const std::string &hostport, const std::string &mods);`, `const PhonebookEntry *find(const std::string &number) const;`, `bool erase(const std::string &number);`, `const std::vector<PhonebookEntry> &all() const;`, `void clear();`
  - `bool phonebook_split_hostport(const std::string &in, std::string &host, uint16_t &port);`

- [ ] **Step 1: Write the failing test**

Append to `test/native/test_modem_at/test_modem_at.cpp`, before `main`:

```cpp
#include "phonebook.h"

// ----------------------------------------------------------------- phonebook

void test_hostport_split(void)
{
    std::string host;
    uint16_t port = 0;

    TEST_ASSERT_TRUE(phonebook_split_hostport("bbs.example.com:23", host, port));
    TEST_ASSERT_EQUAL_STRING("bbs.example.com", host.c_str());
    TEST_ASSERT_EQUAL_UINT(23, port);
}

// A bare host is port 23. Every phase-1 dial target is a BBS, and typing the
// port every time is the kind of friction that makes a feature go unused.
void test_hostport_defaults_to_telnet_port(void)
{
    std::string host;
    uint16_t port = 0;

    TEST_ASSERT_TRUE(phonebook_split_hostport("bbs.example.com", host, port));
    TEST_ASSERT_EQUAL_STRING("bbs.example.com", host.c_str());
    TEST_ASSERT_EQUAL_UINT(23, port);
}

void test_hostport_rejects_malformed_input(void)
{
    std::string host;
    uint16_t port = 0;

    TEST_ASSERT_FALSE(phonebook_split_hostport("", host, port));
    TEST_ASSERT_FALSE(phonebook_split_hostport(":23", host, port));
    TEST_ASSERT_FALSE(phonebook_split_hostport("host:", host, port));
    TEST_ASSERT_FALSE(phonebook_split_hostport("host:abc", host, port));
    TEST_ASSERT_FALSE(phonebook_split_hostport("host:0", host, port));
    TEST_ASSERT_FALSE(phonebook_split_hostport("host:65536", host, port));
}

void test_phonebook_store_and_find(void)
{
    Phonebook pb;
    TEST_ASSERT_TRUE(pb.store("1", "bbs.example.com:23", "T"));

    const PhonebookEntry *e = pb.find("1");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("bbs.example.com", e->host.c_str());
    TEST_ASSERT_EQUAL_UINT(23, e->port);
    TEST_ASSERT_EQUAL_STRING("T", e->mods.c_str());
}

void test_phonebook_find_misses_return_null(void)
{
    Phonebook pb;
    pb.store("1", "a.example.com:23", "");
    TEST_ASSERT_NULL(pb.find("2"));
}

// Leading zeros are significant: a user who stored 007 dials 007, not 7.
void test_phonebook_numbers_are_compared_as_strings(void)
{
    Phonebook pb;
    pb.store("007", "a.example.com:23", "");
    TEST_ASSERT_NOT_NULL(pb.find("007"));
    TEST_ASSERT_NULL(pb.find("7"));
}

void test_phonebook_store_replaces_an_existing_number(void)
{
    Phonebook pb;
    pb.store("1", "a.example.com:23", "");
    pb.store("1", "b.example.com:2323", "T");

    TEST_ASSERT_EQUAL_UINT(1, pb.all().size());
    const PhonebookEntry *e = pb.find("1");
    TEST_ASSERT_EQUAL_STRING("b.example.com", e->host.c_str());
    TEST_ASSERT_EQUAL_UINT(2323, e->port);
}

void test_phonebook_erase(void)
{
    Phonebook pb;
    pb.store("1", "a.example.com:23", "");
    TEST_ASSERT_TRUE(pb.erase("1"));
    TEST_ASSERT_NULL(pb.find("1"));
    TEST_ASSERT_FALSE(pb.erase("1"));
}

void test_phonebook_rejects_a_bad_hostport(void)
{
    Phonebook pb;
    TEST_ASSERT_FALSE(pb.store("1", "host:99999", ""));
    TEST_ASSERT_EQUAL_UINT(0, pb.all().size());
}

void test_phonebook_rejects_a_non_numeric_number(void)
{
    Phonebook pb;
    TEST_ASSERT_FALSE(pb.store("abc", "a.example.com:23", ""));
    TEST_ASSERT_FALSE(pb.store("", "a.example.com:23", ""));
}

// The listing order is what ATP prints, so it must be stable and numeric
// rather than insertion-ordered or plain lexicographic.
void test_phonebook_lists_in_numeric_order(void)
{
    Phonebook pb;
    pb.store("10", "j.example.com", "");
    pb.store("2",  "b.example.com", "");
    pb.store("1",  "a.example.com", "");

    const auto &all = pb.all();
    TEST_ASSERT_EQUAL_UINT(3, all.size());
    TEST_ASSERT_EQUAL_STRING("1",  all[0].number.c_str());
    TEST_ASSERT_EQUAL_STRING("2",  all[1].number.c_str());
    TEST_ASSERT_EQUAL_STRING("10", all[2].number.c_str());
}
```

Add to `main`:

```cpp
    RUN_TEST(test_hostport_split);
    RUN_TEST(test_hostport_defaults_to_telnet_port);
    RUN_TEST(test_hostport_rejects_malformed_input);
    RUN_TEST(test_phonebook_store_and_find);
    RUN_TEST(test_phonebook_find_misses_return_null);
    RUN_TEST(test_phonebook_numbers_are_compared_as_strings);
    RUN_TEST(test_phonebook_store_replaces_an_existing_number);
    RUN_TEST(test_phonebook_erase);
    RUN_TEST(test_phonebook_rejects_a_bad_hostport);
    RUN_TEST(test_phonebook_rejects_a_non_numeric_number);
    RUN_TEST(test_phonebook_lists_in_numeric_order);
```

Append to `engine_sources.cpp`:

```cpp
#include "../../../lib/modem/phonebook.cpp"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: FAIL — `phonebook.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `lib/modem/phonebook.h` (GPL header first, then):

```cpp
// Numbered dial entries, so ATD5 reaches a stored host.
//
// Numbers are compared as strings because leading zeros are significant to a
// user who stored 007. Listing order is numeric, since that is what ATP prints.

#ifndef MEATLOAF_MODEM_PHONEBOOK
#define MEATLOAF_MODEM_PHONEBOOK

#ifdef ENABLE_MODEM

#include <cstdint>
#include <string>
#include <vector>

struct PhonebookEntry
{
    std::string number;  // the digits the user dials
    std::string host;
    uint16_t    port = 23;
    std::string mods;    // dial modifiers to apply, e.g. "T"
};

// Splits "host" or "host:port" into its parts. A bare host is port 23 -- every
// phase-1 target is a BBS and typing the port each time is friction that makes
// a feature go unused. Returns false for an empty host, a non-numeric port, or
// a port outside 1-65535.
bool phonebook_split_hostport(const std::string &in, std::string &host,
                              uint16_t &port);

class Phonebook
{
public:
    // Refuses a non-numeric or empty number, and a host/port that
    // phonebook_split_hostport() rejects. Replaces an existing number.
    bool store(const std::string &number, const std::string &hostport,
               const std::string &mods);

    // Null when the number is not stored.
    const PhonebookEntry *find(const std::string &number) const;

    bool erase(const std::string &number);

    // Sorted by number, shortest-then-lexicographic so 2 precedes 10.
    const std::vector<PhonebookEntry> &all() const { return entries_; }

    void clear() { entries_.clear(); }

private:
    void sort();

    std::vector<PhonebookEntry> entries_;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_PHONEBOOK
```

- [ ] **Step 4: Write the implementation**

Create `lib/modem/phonebook.cpp` (GPL header first, then):

```cpp
#include "phonebook.h"

#ifdef ENABLE_MODEM

#include <algorithm>
#include <cstdlib>

namespace
{

bool all_digits(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
    }
    return true;
}

} // namespace

bool phonebook_split_hostport(const std::string &in, std::string &host,
                              uint16_t &port)
{
    if (in.empty())
        return false;

    size_t colon = in.rfind(':');
    if (colon == std::string::npos)
    {
        host = in;
        port = 23;
        return true;
    }

    std::string h = in.substr(0, colon);
    std::string p = in.substr(colon + 1);
    if (h.empty() || !all_digits(p))
        return false;

    // strtol, not std::stoi: ESP-IDF builds -fno-exceptions, so a throwing
    // conversion on malformed input becomes std::terminate.
    const char *start = p.c_str();
    char *end = nullptr;
    long v = std::strtol(start, &end, 10);
    if (end == start || *end != '\0' || v < 1 || v > 65535)
        return false;

    host = h;
    port = (uint16_t)v;
    return true;
}

void Phonebook::sort()
{
    // Shortest first, then lexicographic within a length -- that orders 1, 2,
    // 10 the way a user reads them, which plain lexicographic would not.
    std::sort(entries_.begin(), entries_.end(),
              [](const PhonebookEntry &a, const PhonebookEntry &b) {
                  if (a.number.size() != b.number.size())
                      return a.number.size() < b.number.size();
                  return a.number < b.number;
              });
}

bool Phonebook::store(const std::string &number, const std::string &hostport,
                      const std::string &mods)
{
    if (!all_digits(number))
        return false;

    std::string host;
    uint16_t port = 23;
    if (!phonebook_split_hostport(hostport, host, port))
        return false;

    for (auto &e : entries_)
    {
        if (e.number == number)
        {
            e.host = host;
            e.port = port;
            e.mods = mods;
            return true;
        }
    }

    PhonebookEntry e;
    e.number = number;
    e.host = host;
    e.port = port;
    e.mods = mods;
    entries_.push_back(e);
    sort();
    return true;
}

const PhonebookEntry *Phonebook::find(const std::string &number) const
{
    for (const auto &e : entries_)
    {
        if (e.number == number)
            return &e;
    }
    return nullptr;
}

bool Phonebook::erase(const std::string &number)
{
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].number == number)
        {
            entries_.erase(entries_.begin() + i);
            return true;
        }
    }
    return false;
}

#endif // ENABLE_MODEM
```

- [ ] **Step 5: Run test to verify it passes**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: PASS, 55 cases, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add lib/modem/phonebook.h lib/modem/phonebook.cpp \
        test/native/test_modem_at/engine_sources.cpp \
        test/native/test_modem_at/test_modem_at.cpp
git commit -m "feat(modem): phonebook

Numbers compare as strings because leading zeros are significant - a
user who stored 007 dials 007, not 7. Listing sorts shortest-first then
lexicographically, which orders 1, 2, 10 the way a user reads them.

A bare host defaults to port 23; every phase-1 target is a BBS and
typing the port each time is friction. strtol, not std::stoi, for the
-fno-exceptions reason."
```

---

### Task 6: Telnet negotiation filter

`TelnetFilter` deliberately does **not** inherit `MStream`. It is a pure byte-in/byte-out unit over two sinks, which is what makes libtelnet's option negotiation reachable from the native suite. `TelnetMStream` (Task 8) is a thin wrapper over it.

**Files:**
- Create: `lib/modem/telnet_filter.h`
- Create: `lib/modem/telnet_filter.cpp`
- Create: `test/native/test_modem_at/libtelnet_source.c`
- Modify: `test/native/test_modem_at/engine_sources.cpp`
- Modify: `test/native/test_modem_at/test_modem_at.cpp`

**Interfaces:**
- Consumes: `lib/telnet/libtelnet.h`.
- Produces:
  - `class TelnetFilter` with `using Sink = std::function<void(const uint8_t *, size_t)>;`, `bool begin(Sink to_app, Sink to_peer);`, `void end();`, `bool isOpen() const;`, `void receive(const uint8_t *buf, size_t n);`, `void transmit(const uint8_t *buf, size_t n);`

- [ ] **Step 1: Write the failing test**

Append to `test/native/test_modem_at/test_modem_at.cpp`, before `main`:

```cpp
#include "telnet_filter.h"

// ------------------------------------------------------------- telnet_filter

namespace
{
struct FilterHarness
{
    TelnetFilter f;
    std::string to_app;   // bytes the terminal should see
    std::string to_peer;  // bytes that should go out on the socket

    FilterHarness()
    {
        f.begin(
            [this](const uint8_t *b, size_t n) {
                to_app.append((const char *)b, n);
            },
            [this](const uint8_t *b, size_t n) {
                to_peer.append((const char *)b, n);
            });
    }

    void recv(const std::string &s)
    {
        f.receive((const uint8_t *)s.data(), s.size());
    }
};
} // namespace

void test_telnet_plain_data_reaches_the_application(void)
{
    FilterHarness h;
    h.recv("hello");
    TEST_ASSERT_EQUAL_STRING("hello", h.to_app.c_str());
    TEST_ASSERT_EQUAL_STRING("", h.to_peer.c_str());
}

// The whole reason ATDT exists: without negotiation these bytes land on the
// user's screen as garbage.
void test_telnet_iac_negotiation_never_reaches_the_application(void)
{
    FilterHarness h;
    // IAC DO ECHO (255 253 1)
    h.recv("\xFF\xFD\x01");
    TEST_ASSERT_EQUAL_STRING("", h.to_app.c_str());
    TEST_ASSERT_TRUE(h.to_peer.size() > 0);
}

void test_telnet_negotiation_between_data_is_stripped_in_place(void)
{
    FilterHarness h;
    h.recv("ab\xFF\xFD\x01" "cd");
    TEST_ASSERT_EQUAL_STRING("abcd", h.to_app.c_str());
}

// A literal 0xFF in the data stream is sent as IAC IAC and must arrive as one
// byte. Getting this wrong corrupts any binary or high-ASCII content.
void test_telnet_escaped_iac_becomes_one_literal_byte(void)
{
    FilterHarness h;
    h.recv("a\xFF\xFF" "b");
    TEST_ASSERT_EQUAL_UINT(3, h.to_app.size());
    TEST_ASSERT_EQUAL_UINT8(0xFF, (uint8_t)h.to_app[1]);
}

// Outbound: a literal 0xFF from the terminal must be doubled on the wire, or
// the far end reads it as the start of a command.
void test_telnet_transmit_escapes_a_literal_iac(void)
{
    FilterHarness h;
    const uint8_t data[] = { 'a', 0xFF, 'b' };
    h.f.transmit(data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(4, h.to_peer.size());
    TEST_ASSERT_EQUAL_UINT8(0xFF, (uint8_t)h.to_peer[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, (uint8_t)h.to_peer[2]);
}

// A negotiation split across two reads is the normal case on a real socket,
// not an edge case -- TCP does not respect message boundaries.
void test_telnet_negotiation_split_across_reads_is_handled(void)
{
    FilterHarness h;
    h.recv("ab\xFF");
    h.recv("\xFD\x01" "cd");
    TEST_ASSERT_EQUAL_STRING("abcd", h.to_app.c_str());
}

void test_telnet_end_is_safe_to_call_twice(void)
{
    TelnetFilter f;
    f.begin([](const uint8_t *, size_t) {}, [](const uint8_t *, size_t) {});
    TEST_ASSERT_TRUE(f.isOpen());
    f.end();
    TEST_ASSERT_FALSE(f.isOpen());
    f.end();  // must not double-free
    TEST_ASSERT_FALSE(f.isOpen());
}

// Calls after end() are no-ops rather than a null dereference: the stream's
// close() path can race a final read.
void test_telnet_calls_after_end_are_inert(void)
{
    FilterHarness h;
    h.f.end();
    h.recv("hello");
    const uint8_t data[] = { 'x' };
    h.f.transmit(data, 1);
    TEST_ASSERT_EQUAL_STRING("", h.to_app.c_str());
    TEST_ASSERT_EQUAL_STRING("", h.to_peer.c_str());
}
```

Add to `main`:

```cpp
    RUN_TEST(test_telnet_plain_data_reaches_the_application);
    RUN_TEST(test_telnet_iac_negotiation_never_reaches_the_application);
    RUN_TEST(test_telnet_negotiation_between_data_is_stripped_in_place);
    RUN_TEST(test_telnet_escaped_iac_becomes_one_literal_byte);
    RUN_TEST(test_telnet_transmit_escapes_a_literal_iac);
    RUN_TEST(test_telnet_negotiation_split_across_reads_is_handled);
    RUN_TEST(test_telnet_end_is_safe_to_call_twice);
    RUN_TEST(test_telnet_calls_after_end_are_inert);
```

Append to `engine_sources.cpp`:

```cpp
#include "../../../lib/modem/telnet_filter.cpp"
```

Create `test/native/test_modem_at/libtelnet_source.c`:

```c
/* libtelnet, compiled for the native test suite. It is C, so it cannot be
 * folded into engine_sources.cpp the way the C++ units are. */
#include "../../../lib/telnet/libtelnet.c"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: FAIL — `telnet_filter.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `lib/modem/telnet_filter.h` (GPL header first, then):

```cpp
// Telnet option negotiation, as a pure byte filter.
//
// This deliberately does NOT inherit MStream. It is a byte-in/byte-out unit
// over two sinks, which is what makes libtelnet's negotiation reachable from
// test/native/test_modem_at. TelnetMStream in lib/meatloaf/network/telnet.h is
// a thin wrapper over it.
//
// Without this, a BBS that negotiates sprays IAC bytes onto the user's screen.

#ifndef MEATLOAF_MODEM_TELNET_FILTER
#define MEATLOAF_MODEM_TELNET_FILTER

#ifdef ENABLE_MODEM

#include <cstddef>
#include <cstdint>
#include <functional>

extern "C" {
#include "libtelnet.h"
}

class TelnetFilter
{
public:
    using Sink = std::function<void(const uint8_t *buf, size_t n)>;

    TelnetFilter() = default;
    ~TelnetFilter();

    TelnetFilter(const TelnetFilter &) = delete;
    TelnetFilter &operator=(const TelnetFilter &) = delete;

    // to_app receives payload bytes destined for the terminal.
    // to_peer receives bytes that must be written to the socket -- both
    // negotiation responses and escaped outbound data.
    bool begin(Sink to_app, Sink to_peer);

    // Idempotent, so a caller may end() on error and again in a destructor.
    void end();

    bool isOpen() const { return telnet_ != nullptr; }

    // Bytes read from the remote. Negotiation is answered on to_peer and never
    // reaches to_app; a doubled IAC arrives as one literal 0xFF. A no-op after
    // end(), because the stream's close() path can race a final read.
    void receive(const uint8_t *buf, size_t n);

    // Bytes from the terminal. A literal 0xFF is doubled on the wire, or the
    // far end reads it as the start of a command.
    void transmit(const uint8_t *buf, size_t n);

private:
    static void handler(telnet_t *t, telnet_event_t *ev, void *user);

    telnet_t *telnet_ = nullptr;
    Sink to_app_;
    Sink to_peer_;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_TELNET_FILTER
```

- [ ] **Step 4: Write the implementation**

Create `lib/modem/telnet_filter.cpp` (GPL header first, then):

```cpp
#include "telnet_filter.h"

#ifdef ENABLE_MODEM

namespace
{

// What Meatloaf offers and accepts as a telnet CLIENT.
//
// BINARY both ways so 8-bit data (ANSI art, high ASCII) survives; SGA both
// ways because a BBS is character-at-a-time, not line-at-a-time; the server
// may ECHO but we never will. TTYPE and NAWS are declined -- there is no
// terminal here to report a type or a size for, and offering them invites
// subnegotiation this filter would then have to answer.
const telnet_telopt_t kTelopts[] = {
    { TELNET_TELOPT_BINARY, TELNET_WILL, TELNET_DO   },
    { TELNET_TELOPT_SGA,    TELNET_WILL, TELNET_DO   },
    { TELNET_TELOPT_ECHO,   TELNET_WONT, TELNET_DO   },
    { TELNET_TELOPT_TTYPE,  TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_NAWS,   TELNET_WONT, TELNET_DONT },
    { -1, 0, 0 }
};

} // namespace

TelnetFilter::~TelnetFilter()
{
    end();
}

void TelnetFilter::handler(telnet_t *t, telnet_event_t *ev, void *user)
{
    (void)t;
    TelnetFilter *self = (TelnetFilter *)user;
    if (self == nullptr)
        return;

    switch (ev->type)
    {
    case TELNET_EV_DATA:
        if (self->to_app_ && ev->data.size > 0)
            self->to_app_((const uint8_t *)ev->data.buffer, ev->data.size);
        break;

    case TELNET_EV_SEND:
        if (self->to_peer_ && ev->data.size > 0)
            self->to_peer_((const uint8_t *)ev->data.buffer, ev->data.size);
        break;

    default:
        // Every other event is negotiation libtelnet has already answered on
        // our behalf via TELNET_EV_SEND. Nothing to do, and in particular
        // nothing to forward -- that is the whole point of the filter.
        break;
    }
}

bool TelnetFilter::begin(Sink to_app, Sink to_peer)
{
    end();

    to_app_ = to_app;
    to_peer_ = to_peer;

    // Flags 0: not a proxy, and no NVT end-of-line translation. A BBS session
    // is a byte pipe; rewriting CR/LF here would corrupt ANSI positioning.
    telnet_ = telnet_init(kTelopts, &TelnetFilter::handler, 0, this);
    if (telnet_ == nullptr)
    {
        to_app_ = nullptr;
        to_peer_ = nullptr;
        return false;
    }
    return true;
}

void TelnetFilter::end()
{
    if (telnet_ != nullptr)
    {
        telnet_free(telnet_);
        telnet_ = nullptr;
    }
    to_app_ = nullptr;
    to_peer_ = nullptr;
}

void TelnetFilter::receive(const uint8_t *buf, size_t n)
{
    if (telnet_ == nullptr || buf == nullptr || n == 0)
        return;
    telnet_recv(telnet_, (const char *)buf, n);
}

void TelnetFilter::transmit(const uint8_t *buf, size_t n)
{
    if (telnet_ == nullptr || buf == nullptr || n == 0)
        return;
    // telnet_send, not telnet_send_text: send_text applies NVT end-of-line
    // translation, which corrupts ANSI cursor sequences.
    telnet_send(telnet_, (const char *)buf, n);
}

#endif // ENABLE_MODEM
```

- [ ] **Step 5: Run test to verify it passes**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: PASS, 63 cases, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add lib/modem/telnet_filter.h lib/modem/telnet_filter.cpp \
        test/native/test_modem_at/engine_sources.cpp \
        test/native/test_modem_at/libtelnet_source.c \
        test/native/test_modem_at/test_modem_at.cpp
git commit -m "feat(modem): telnet negotiation filter over libtelnet

Deliberately not an MStream - a byte-in/byte-out unit over two sinks, so
libtelnet's negotiation is reachable from the native suite. TelnetMStream
wraps it.

Offers BINARY and SGA both ways so 8-bit ANSI art survives and a
character-at-a-time BBS works; declines TTYPE and NAWS, which would
invite subnegotiation with no terminal to describe. telnet_send rather
than telnet_send_text: NVT end-of-line translation corrupts ANSI cursor
sequences.

Covers the split-negotiation case, which is the normal case on a real
socket - TCP does not respect message boundaries."
```

---

### Task 7: `MStream::waitReadable()`

The modem multiplexes terminal input against a connection. `MStream` has no readiness concept, and reaching past it to the underlying fd would break the abstraction the whole design rests on.

**Files:**
- Modify: `lib/meatloaf/meatloaf.h` (in `class MStream`, next to `available()`)
- Modify: `lib/meatloaf/meatloaf.cpp`
- Modify: `test/native/test_mstream_seek/test_mstream_seek.cpp`

**Interfaces:**
- Consumes: `MStream::available()`, `MStream::isOpen()`.
- Produces: `virtual bool waitReadable(uint32_t timeout_ms);` on `MStream`.

- [ ] **Step 1: Read the existing suite to match its stub style**

Run: `sed -n '1,60p' test/native/test_mstream_seek/test_mstream_seek.cpp`

The new cases below subclass `MStream` directly. If that suite already defines a concrete test stream, derive from it instead of repeating the pure-virtual overrides.

- [ ] **Step 2: Write the failing test**

Append to `test/native/test_mstream_seek/test_mstream_seek.cpp`, before its `main`:

```cpp
// ------------------------------------------------------------- waitReadable

// A stream whose available() becomes non-zero after a set number of polls,
// standing in for a socket whose data arrives late.
class LateStream : public MStream
{
public:
    LateStream(int polls_until_ready)
        : MStream("test://late"), remaining_(polls_until_ready) {}

    bool isOpen() override { return true; }
    bool open(std::ios_base::openmode) override { return true; }
    void close() override {}
    uint32_t read(uint8_t *, uint32_t) override { return 0; }
    uint32_t write(const uint8_t *, uint32_t) override { return 0; }
    bool seek(uint32_t) override { return false; }

    uint32_t available() override
    {
        if (remaining_ > 0)
        {
            --remaining_;
            return 0;
        }
        return 1;
    }

private:
    int remaining_;
};

void test_wait_readable_returns_true_when_data_is_already_available(void)
{
    LateStream s(0);
    TEST_ASSERT_TRUE(s.waitReadable(0));
}

void test_wait_readable_polls_until_data_arrives(void)
{
    LateStream s(3);
    TEST_ASSERT_TRUE(s.waitReadable(1000));
}

// A zero timeout must still check once -- it is a poll, not a no-op.
void test_wait_readable_with_zero_timeout_checks_once(void)
{
    LateStream s(1);
    TEST_ASSERT_FALSE(s.waitReadable(0));
}

void test_wait_readable_times_out_when_no_data_arrives(void)
{
    LateStream s(1000000);
    TEST_ASSERT_FALSE(s.waitReadable(50));
}

// A closed stream is not "not yet readable" -- it will never be readable, and
// a caller looping on a timeout would spin until its own deadline.
void test_wait_readable_returns_false_immediately_when_closed(void)
{
    class ClosedStream : public LateStream
    {
    public:
        ClosedStream() : LateStream(1000000) {}
        bool isOpen() override { return false; }
    };

    ClosedStream s;
    TEST_ASSERT_FALSE(s.waitReadable(5000));
}
```

Add to that file's `main`:

```cpp
    RUN_TEST(test_wait_readable_returns_true_when_data_is_already_available);
    RUN_TEST(test_wait_readable_polls_until_data_arrives);
    RUN_TEST(test_wait_readable_with_zero_timeout_checks_once);
    RUN_TEST(test_wait_readable_times_out_when_no_data_arrives);
    RUN_TEST(test_wait_readable_returns_false_immediately_when_closed);
```

- [ ] **Step 3: Run test to verify it fails**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_mstream_seek`
Expected: FAIL — `'class LateStream' has no member named 'waitReadable'`.

- [ ] **Step 4: Declare it on the base class**

In `lib/meatloaf/meatloaf.h`, inside `class MStream`, immediately after the `available()` declaration, add:

```cpp
    // Blocks until at least one byte can be read, the stream closes, or
    // timeout_ms elapses. Returns true only when data is available.
    //
    // The default polls available(), which is correct for every stream and
    // cheap enough for the ones that are not sockets. A stream that can do
    // better -- a socket that could select(), an SSH channel with its own
    // non-blocking read -- overrides it.
    //
    // Exists because the modem multiplexes terminal input against a
    // connection and must not reach past MStream to the underlying fd.
    virtual bool waitReadable(uint32_t timeout_ms);
```

- [ ] **Step 5: Write the default implementation**

At the top of `lib/meatloaf/meatloaf.cpp`, add the includes the non-ESP branch needs:

```cpp
#ifndef ESP_PLATFORM
#include <chrono>
#include <thread>
#endif
```

Then add near the other `MStream` method definitions:

```cpp
// Polling interval for the default waitReadable(). Small enough that an
// interactive session feels immediate, large enough that a task blocked on it
// is not spinning.
static constexpr uint32_t MSTREAM_POLL_INTERVAL_MS = 10;

bool MStream::waitReadable(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    for (;;)
    {
        // A closed stream will never become readable. Returning false at once
        // stops a caller from spinning until its own deadline.
        if (!isOpen())
            return false;

        if (available() > 0)
            return true;

        if (waited >= timeout_ms)
            return false;

        uint32_t slice = MSTREAM_POLL_INTERVAL_MS;
        if (slice > timeout_ms - waited)
            slice = timeout_ms - waited;
        if (slice == 0)
            slice = 1;

#ifdef ESP_PLATFORM
        vTaskDelay(pdMS_TO_TICKS(slice));
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
#endif
        waited += slice;
    }
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_mstream_seek`
Expected: PASS — the suite's existing cases plus 5 new, 0 failures.

- [ ] **Step 7: Confirm nothing else regressed**

`MStream` is a widely-derived base class, so run the whole native suite:

Run: `~/.platformio/penv/Scripts/pio.exe test -e native`

Expected: the same pass/skip/error counts as before this task, plus the 5 new cases. **`test_EdUrlParser`, `test_hdd_read` and `test_strings` error at baseline** — they must continue to error identically. That is a known pre-existing state, not a regression; if the set of erroring suites changes, this task caused it.

- [ ] **Step 8: Commit**

```bash
git add lib/meatloaf/meatloaf.h lib/meatloaf/meatloaf.cpp \
        test/native/test_mstream_seek/test_mstream_seek.cpp
git commit -m "feat(meatloaf): MStream::waitReadable()

The modem multiplexes terminal input against a connection and must not
reach past MStream to the underlying fd. The default polls available(),
which is correct for every existing subclass, so none change behaviour.

A closed stream returns false immediately rather than waiting out the
timeout - it will never become readable, and a caller looping on it
would otherwise spin until its own deadline."
```

---

### Task 8: `telnet://` filesystem

**Files:**
- Modify: `lib/meatloaf/network/telnet.h` (currently a 3-line stub)
- Create: `lib/meatloaf/network/telnet.cpp`
- Modify: `lib/meatloaf/meatloaf.cpp` (register the filesystem)
- Modify: `platformio.ini.sample` (`-D ENABLE_MODEM` on the S3 board)

**Interfaces:**
- Consumes: `TelnetFilter` (Task 6), `MStream::waitReadable()` (Task 7), `TCPMFile`/`TCPMStream` from `lib/meatloaf/network/tcp.h`.
- Produces:
  - `class TelnetMStream : public MStream` — `TelnetMStream(std::string url, std::shared_ptr<MStream> inner)`
  - `class TelnetMFile : public MFile`
  - `class TelnetMFileSystem : public MFileSystem` — `handles("telnet:")`

**No native test.** This depends on `MStream` and a live socket; `MFSOwner::File()` aborts under the native stubs. The negotiation logic underneath is already covered by Task 6. Verification is by build plus the hardware run in Task 12.

- [ ] **Step 1: Read the existing stub so nothing is lost**

Run: `cat lib/meatloaf/network/telnet.h`

It is a 3-line placeholder. Preserve its GPL header if present; replace the rest.

- [ ] **Step 2: Write the header**

Replace `lib/meatloaf/network/telnet.h` with (GPL header first, then):

```cpp
// TELNET:// - Telnet with option negotiation
// https://datatracker.ietf.org/doc/html/rfc854
//
// A decorator over tcp://. The transport is exactly TCPMStream; this layer
// only runs libtelnet over the byte stream, via TelnetFilter (lib/modem), so
// that IAC negotiation is answered instead of landing on the user's screen.
//
// Decorating rather than subclassing TCPMStream is the same pattern the media
// layer uses for decoder streams, and it is what lets ATD and ATDT be one dial
// path with two URL schemes.

#ifndef MEATLOAF_NETWORK_TELNET
#define MEATLOAF_NETWORK_TELNET

#ifdef ENABLE_MODEM

#include <deque>
#include <memory>
#include <string>

#include "meatloaf.h"
#include "telnet_filter.h"

#include "../../../include/debug.h"

class TelnetMStream : public MStream
{
public:
    // inner is an already-constructed (not necessarily open) byte stream,
    // normally a TCPMStream over the same host and port.
    TelnetMStream(std::string url, std::shared_ptr<MStream> inner);
    ~TelnetMStream() override;

    bool isOpen() override;
    bool isNetwork() override { return true; }
    bool isRandomAccess() override { return false; }

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t *buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    // A telnet session is a byte pipe with no position or extent.
    bool seek(uint32_t) override { return false; }
    uint32_t size() override { return 0; }
    uint32_t position() override { return 0; }

    // Payload bytes already decoded and waiting, plus whatever the socket
    // holds. Non-zero here does not guarantee read() returns data -- a socket
    // read may be entirely negotiation -- which is why waitReadable() loops.
    uint32_t available() override;

    bool waitReadable(uint32_t timeout_ms) override;

private:
    // Pulls one chunk off the inner stream and runs it through the filter,
    // which appends payload to rx_ and writes any response to the inner
    // stream. Returns false when the inner stream is closed or errored.
    bool pump();

    static constexpr uint32_t CHUNK = 512;

    std::shared_ptr<MStream> inner_;
    TelnetFilter             filter_;
    std::deque<uint8_t>      rx_;
    bool                     open_ = false;
};

class TelnetMFile : public MFile
{
public:
    TelnetMFile(std::string path) : MFile(path) {}
    ~TelnetMFile() override = default;

    // Like tcp://, a telnet URL is a connection, not a file, so it is never
    // wrapped in a decoder.
    std::shared_ptr<MStream> getSourceStream(
        std::ios_base::openmode mode = std::ios_base::in) override
    {
        return createStream(mode);
    }

    std::shared_ptr<MStream> getDecodedStream(
        std::shared_ptr<MStream> src) override
    {
        return src;
    }

    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override { return false; }
    bool exists() override { return true; }
    bool rewindDirectory() override { return false; }
    MFile *getNextFileInDir() override { return nullptr; }
    bool remove() override { return false; }
    bool rename(std::string) override { return false; }
    time_t getLastWrite() override { return 0; }
    time_t getCreationTime() override { return 0; }
    uint64_t getAvailableSpace() override { return 0; }
    bool mkDir() override { return false; }
};

class TelnetMFileSystem : public MFileSystem
{
public:
    TelnetMFileSystem() : MFileSystem("telnet") { isRootFS = true; }

    bool handles(std::string name) override
    {
        return mstr::startsWith(name, (char *)"telnet:", false);
    }

    MFile *getFile(std::string path) override { return new TelnetMFile(path); }
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_NETWORK_TELNET
```

If `MFileSystem::handles()` is not virtual in this codebase, drop the `override` on it — check `lib/meatloaf/network/tcp.h`, which declares it without `override`.

- [ ] **Step 3: Write the implementation**

Create `lib/meatloaf/network/telnet.cpp` (GPL header first, then):

```cpp
#include "telnet.h"

#ifdef ENABLE_MODEM

#include <cstring>

#include "tcp.h"

TelnetMStream::TelnetMStream(std::string url, std::shared_ptr<MStream> inner)
    : MStream(url), inner_(inner)
{
}

TelnetMStream::~TelnetMStream()
{
    close();
}

bool TelnetMStream::isOpen()
{
    return open_ && inner_ != nullptr && inner_->isOpen();
}

bool TelnetMStream::open(std::ios_base::openmode m)
{
    if (inner_ == nullptr)
        return false;

    if (!inner_->isOpen() && !inner_->open(m))
    {
        Debug_printv("telnet: inner stream failed to open url[%s]", url.c_str());
        return false;
    }

    // The filter writes negotiation responses straight back to the inner
    // stream. Capturing `this` is safe: the filter is a member and cannot
    // outlive the stream.
    bool ok = filter_.begin(
        [this](const uint8_t *b, size_t n) {
            for (size_t i = 0; i < n; ++i)
                rx_.push_back(b[i]);
        },
        [this](const uint8_t *b, size_t n) {
            if (inner_ != nullptr)
                inner_->write(b, (uint32_t)n);
        });

    if (!ok)
    {
        Debug_printv("telnet: telnet_init failed url[%s]", url.c_str());
        inner_->close();
        return false;
    }

    mode = m;
    open_ = true;

    // Nothing is sent proactively. libtelnet emits our WILL/DO offers as the
    // remote's negotiation arrives, which keeps a server that negotiates
    // nothing from seeing bytes it never asked for.
    return true;
}

void TelnetMStream::close()
{
    filter_.end();
    rx_.clear();
    if (inner_ != nullptr)
        inner_->close();
    open_ = false;
}

bool TelnetMStream::pump()
{
    if (inner_ == nullptr || !inner_->isOpen())
        return false;

    uint8_t buf[CHUNK];
    uint32_t got = inner_->read(buf, CHUNK);
    if (got == 0)
        return inner_->isOpen();

    // Appends payload to rx_ and writes any negotiation reply to inner_.
    filter_.receive(buf, got);
    return true;
}

uint32_t TelnetMStream::read(uint8_t *buf, uint32_t size)
{
    if (buf == nullptr || size == 0)
        return 0;

    if (rx_.empty())
        pump();

    uint32_t n = 0;
    while (n < size && !rx_.empty())
    {
        buf[n++] = rx_.front();
        rx_.pop_front();
    }
    return n;
}

uint32_t TelnetMStream::write(const uint8_t *buf, uint32_t size)
{
    if (!isOpen() || buf == nullptr || size == 0)
        return 0;

    // The filter escapes a literal 0xFF and writes through to inner_.
    filter_.transmit(buf, size);

    // Report the caller's own count: the wire count differs whenever an IAC
    // was doubled, and a caller told "more bytes written than I gave you"
    // would treat it as an error.
    return size;
}

uint32_t TelnetMStream::available()
{
    uint32_t n = (uint32_t)rx_.size();
    if (inner_ != nullptr)
        n += inner_->available();
    return n;
}

bool TelnetMStream::waitReadable(uint32_t timeout_ms)
{
    // Decoded payload already waiting.
    if (!rx_.empty())
        return true;

    // A socket read can be entirely negotiation, leaving no payload -- so
    // "the inner stream is readable" is not the same question as "this stream
    // is readable", and the wait has to be re-entered rather than returned
    // from. The inner waitReadable() does the actual blocking; this loop only
    // re-checks after each pump.
    uint32_t waited = 0;
    for (;;)
    {
        if (!isOpen())
            return false;

        uint32_t remaining = (timeout_ms > waited) ? (timeout_ms - waited) : 0;
        uint32_t slice = (remaining > 50) ? 50 : remaining;

        if (inner_ != nullptr && inner_->waitReadable(slice))
        {
            if (!pump())
                return false;
            if (!rx_.empty())
                return true;
        }

        waited += (slice == 0) ? 1 : slice;
        if (waited >= timeout_ms)
            return !rx_.empty();
    }
}

std::shared_ptr<MStream> TelnetMFile::createStream(std::ios_base::openmode mode)
{
    // Reuse tcp:// verbatim for the transport. Building a TCPMFile from the
    // rewritten URL rather than a TCPMStream directly keeps the SessionBroker
    // bookkeeping identical to a plain tcp:// dial.
    std::string tcp_url = url;
    if (mstr::startsWith(tcp_url, (char *)"telnet:", false))
        tcp_url = "tcp:" + tcp_url.substr(strlen("telnet:"));

    auto tcp_file = std::shared_ptr<MFile>(MFSOwner::File(tcp_url));
    if (tcp_file == nullptr)
    {
        Debug_printv("telnet: could not resolve transport url[%s]", tcp_url.c_str());
        return nullptr;
    }

    auto inner = tcp_file->getSourceStream(mode);
    if (inner == nullptr)
    {
        Debug_printv("telnet: no transport stream url[%s]", tcp_url.c_str());
        return nullptr;
    }

    return std::make_shared<TelnetMStream>(url, inner);
}

#endif // ENABLE_MODEM
```

- [ ] **Step 4: Register the filesystem**

In `lib/meatloaf/meatloaf.cpp`, find the initializer for `MFSOwner::availableFS`. Add next to the existing `new TCPMFileSystem()` entry:

```cpp
#ifdef ENABLE_MODEM
    new TelnetMFileSystem(),
#endif
```

And add the include next to the `network/tcp.h` include at the top of that file:

```cpp
#ifdef ENABLE_MODEM
#include "network/telnet.h"
#endif
```

- [ ] **Step 5: Enable the build gate and build**

In `platformio.ini.sample`, add to `[env:esp32-s3-devkitc-1]`'s `build_flags`:

```ini
    -D ENABLE_MODEM
```

Mirror the same line into your local `platformio.ini` — it is gitignored, so the sample alone does not affect your build.

New source files were added, so the CMake source glob must be invalidated first:

```bash
rm -f .pio/build/esp32-s3-devkitc-1/CMakeCache.txt
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1 > /tmp/modem-build.log 2>&1
grep -E "SUCCESS|FAILED|error:|overflowed" /tmp/modem-build.log
```

Expected: `SUCCESS`, no `error:`, no `overflowed`.

- [ ] **Step 6: Record the flash cost**

Measure rather than estimate — this repo has twice gated the wrong thing by assuming source size equals flash text:

```bash
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1 -t size > /tmp/modem-size.log 2>&1
grep -E "Flash|RAM" /tmp/modem-size.log
```

Put the Flash percentage in the commit message so later phases have a baseline.

- [ ] **Step 7: Commit**

```bash
git add lib/meatloaf/network/telnet.h lib/meatloaf/network/telnet.cpp \
        lib/meatloaf/meatloaf.cpp platformio.ini.sample
git commit -m "feat(meatloaf): telnet:// filesystem over tcp://

A decorator, not a subclass: the transport is TCPMStream verbatim and
this layer only runs libtelnet over the byte stream. Same pattern the
media layer uses for decoder streams, and it is what lets ATD and ATDT
be one dial path with two URL schemes.

waitReadable() re-enters its wait rather than returning after the inner
stream reports readable - a socket read can be entirely negotiation and
yield no payload, so the two questions are not the same.

write() reports the caller's count, not the wire count: those differ
whenever an IAC was doubled, and a caller told more bytes went out than
it supplied would treat it as an error."
```

---

### Task 9: ModemPort

**Files:**
- Create: `lib/modem/modem_port.h`
- Create: `lib/modem/modem_port.cpp`

**Interfaces:**
- Consumes: FreeRTOS StreamBuffers.
- Produces:
  - `class ModemPort` with `bool begin();`, `void end();`, `bool isOpen() const;`, `size_t pushRx(const uint8_t*, size_t, uint32_t);`, `size_t popRx(uint8_t*, size_t, uint32_t);`, `size_t pushTx(const uint8_t*, size_t, uint32_t);`, `size_t popTx(uint8_t*, size_t, uint32_t);`, `void setAttached(bool);`, `bool attached() const;`

**No native test:** FreeRTOS StreamBuffers do not exist on the host. Verified by the hardware run in Task 12.

- [ ] **Step 1: Write the header**

Create `lib/modem/modem_port.h` (GPL header first, then):

```cpp
// One console's connection to the modem task.
//
// The modem task must never touch a console file descriptor. Two tasks on one
// socket is the condition behind the NFS 0x6400 heap corruption and the reason
// console.printf() is banned from I/O hot paths; the ESC-cancel design relies
// on the shell task being blocked while a command runs, so there is never a
// second reader. A modem task writing to the console fd would break that.
//
// So the shell task stays the sole reader and writer of its own fd and becomes
// a pump: bytes off the fd go into rx, bytes out of tx go to the fd. FreeRTOS
// StreamBuffers are single-writer/single-reader by design, so no mutex is
// needed as long as exactly one task is on each end -- which is the case here.

#ifndef MEATLOAF_MODEM_PORT
#define MEATLOAF_MODEM_PORT

#ifdef ENABLE_MODEM

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

class ModemPort
{
public:
    // Terminal input is slow and arrives a keystroke at a time; modem output
    // is a BBS at full speed. Sizing them differently keeps the total small,
    // and these buffers are internal DRAM -- StreamBuffer storage cannot live
    // in PSRAM.
    static constexpr size_t RX_BYTES = 256;
    static constexpr size_t TX_BYTES = 1024;

    ModemPort() = default;
    ~ModemPort();

    ModemPort(const ModemPort &) = delete;
    ModemPort &operator=(const ModemPort &) = delete;

    // Idempotent. Returns false when either buffer cannot be allocated, which
    // the caller reports rather than entering modem mode half-wired.
    bool begin();
    void end();
    bool isOpen() const { return rx_ != nullptr && tx_ != nullptr; }

    // Shell task -> modem task.
    size_t pushRx(const uint8_t *buf, size_t n, uint32_t timeout_ms);
    // Modem task <- shell task.
    size_t popRx(uint8_t *buf, size_t n, uint32_t timeout_ms);

    // Modem task -> shell task.
    size_t pushTx(const uint8_t *buf, size_t n, uint32_t timeout_ms);
    // Shell task <- modem task.
    size_t popTx(uint8_t *buf, size_t n, uint32_t timeout_ms);

    // True for the one port that receives stream-mode data. Command-mode
    // responses go to every open port so both consoles see the modem's state;
    // stream data goes only to the attached one.
    void setAttached(bool a) { attached_ = a; }
    bool attached() const { return attached_; }

private:
    StreamBufferHandle_t rx_ = nullptr;
    StreamBufferHandle_t tx_ = nullptr;
    volatile bool        attached_ = false;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_PORT
```

- [ ] **Step 2: Write the implementation**

Create `lib/modem/modem_port.cpp` (GPL header first, then):

```cpp
#include "modem_port.h"

#ifdef ENABLE_MODEM

#include "../../include/debug.h"

ModemPort::~ModemPort()
{
    end();
}

bool ModemPort::begin()
{
    if (isOpen())
        return true;

    // Trigger level 1: wake the reader as soon as any byte is available. A
    // terminal session is interactive; batching would add latency for nothing.
    rx_ = xStreamBufferCreate(RX_BYTES, 1);
    tx_ = xStreamBufferCreate(TX_BYTES, 1);

    if (rx_ == nullptr || tx_ == nullptr)
    {
        Debug_printv("modem: port buffers failed to allocate");
        end();
        return false;
    }
    return true;
}

void ModemPort::end()
{
    if (rx_ != nullptr)
    {
        vStreamBufferDelete(rx_);
        rx_ = nullptr;
    }
    if (tx_ != nullptr)
    {
        vStreamBufferDelete(tx_);
        tx_ = nullptr;
    }
    attached_ = false;
}

size_t ModemPort::pushRx(const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (rx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferSend(rx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

size_t ModemPort::popRx(uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (rx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferReceive(rx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

size_t ModemPort::pushTx(const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (tx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferSend(tx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

size_t ModemPort::popTx(uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (tx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferReceive(tx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

#endif // ENABLE_MODEM
```

- [ ] **Step 3: Build**

```bash
rm -f .pio/build/esp32-s3-devkitc-1/CMakeCache.txt
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1 > /tmp/modem-build.log 2>&1
grep -E "SUCCESS|FAILED|error:|overflowed" /tmp/modem-build.log
```

Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add lib/modem/modem_port.h lib/modem/modem_port.cpp
git commit -m "feat(modem): ModemPort StreamBuffer pump

The modem task never touches a console fd. Two tasks on one socket is
the condition behind the NFS 0x6400 heap corruption and the reason
console.printf() is banned from I/O hot paths; the ESC-cancel design
depends on the shell task being the only reader.

So the shell stays sole reader and writer of its own fd and becomes a
pump between it and two StreamBuffers. StreamBuffers are
single-writer/single-reader by design, so with exactly one task on each
end no mutex is needed.

RX is 256 bytes and TX 1024: terminal input arrives a keystroke at a
time, modem output is a BBS at full speed. Both are internal DRAM -
StreamBuffer storage cannot live in PSRAM."
```

---

### Task 10: Modem core — state machine, dial, task

**Files:**
- Create: `lib/modem/modem.h`
- Create: `lib/modem/modem.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–9.
- Produces:
  - `enum class ModemState { COMMAND, DIALING, ONLINE, ONLINE_COMMAND };`
  - `class Modem` with `bool start();`, `void stop();`, `bool isRunning() const;`, `bool attach(ModemPort *port);`, `void detach(ModemPort *port);`, `bool sessionActive() const;`, `void loadConfig();`, `void saveConfig();`
  - `extern Modem modem;`

**No native test:** needs FreeRTOS, `MFSOwner` and `mlConfig`. Verified by the hardware run in Task 12.

- [ ] **Step 1: Write the header**

Create `lib/modem/modem.h` (GPL header first, then):

```cpp
// The modem engine.
//
// One global instance. Either console can drive it and command-mode responses
// reach both, so both see the modem's state; stream data goes only to the
// attached port. A task owns the engine and never touches a console file
// descriptor -- see modem_port.h for why.

#ifndef MEATLOAF_MODEM
#define MEATLOAF_MODEM

#ifdef ENABLE_MODEM

#include <memory>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "at_parser.h"
#include "at_result.h"
#include "at_settings.h"
#include "escape.h"
#include "modem_port.h"
#include "phonebook.h"

class MStream;

enum class ModemState
{
    COMMAND,         // no connection
    DIALING,         // connect in progress
    ONLINE,          // connected, bytes flowing
    ONLINE_COMMAND,  // connected, +++ taken, AT commands accepted
};

class Modem
{
public:
    // Stack for the modem task. Phase 1 reaches dial (MFSOwner path
    // resolution) plus libtelnet; the AGENTS.md reference points are an HTTPS
    // GET with TLS handshake at 4360 bytes and AFP at 9332. 8 KB leaves
    // headroom without claiming the deep tier, which phase 4 adds separately
    // for SSH. Confirm against the `ps` high-water mark in Task 12.
    static constexpr uint32_t TASK_STACK = 8192;
    static constexpr uint32_t TASK_PRIORITY = 5;   // well below IEC's 17
    static constexpr BaseType_t TASK_CORE = 0;     // IEC owns core 1

    // Creates the task. Called once at boot: task stacks are internal-DRAM
    // only with no PSRAM fallback and can fail from fragmentation, so the
    // stack is claimed while contiguous internal RAM is still available. The
    // task idles on its ports until a console enters modem mode.
    bool start();
    void stop();
    bool isRunning() const { return task_ != nullptr; }

    // Registers a console's port. False when the port table is full.
    bool attach(ModemPort *port);
    void detach(ModemPort *port);

    // True while a connection is up, in either ONLINE or ONLINE_COMMAND.
    bool sessionActive() const;

    // Loads settings and the phonebook from mlConfig. Safe before start().
    void loadConfig();
    // Writes settings and the phonebook to mlConfig and saves. AT&W.
    void saveConfig();

private:
    static void taskEntry(void *arg);
    void run();

    void serviceCommandMode();
    void serviceStreamMode();

    // Applies one parsed line. Commands run left to right and stop at the
    // first failure, which is real-modem behaviour and stops a line like
    // AT&S62=1DT"bad" from half-applying silently.
    void executeLine(const std::string &line);

    // Returns false to make the caller emit ERROR. A command that has already
    // emitted its own result (a failed dial reports BUSY or NO ANSWER) sets
    // reported to true so the caller does not add a second code.
    bool executeCommand(const AtCommand &cmd, bool &reported);

    bool doDial(const AtCommand &cmd, bool &reported);
    void doHangup();
    bool doReturnOnline(bool &reported);
    void doInfo(long which);
    bool doPhonebook(const AtCommand &cmd);

    // Writes to every open port. Command-mode output.
    void broadcast(const std::string &s);
    // Writes to the attached port only. Stream-mode data.
    void toAttached(const uint8_t *buf, size_t n);
    void sendResult(AtResult r);

    // Guards ports_ against the shell tasks that call attach()/detach() while
    // the modem task is running. Everything else is touched only by the modem
    // task itself.
    class Lock;

    static constexpr size_t MAX_PORTS = 2;  // one serial, one TCP

    TaskHandle_t             task_ = nullptr;
    SemaphoreHandle_t        mutex_ = nullptr;
    volatile bool            stop_requested_ = false;

    ModemPort               *ports_[MAX_PORTS] = { nullptr, nullptr };

    ModemState               state_ = ModemState::COMMAND;
    AtSettings               settings_;
    Phonebook                phonebook_;
    EscapeDetector           escape_;
    std::shared_ptr<MStream> conn_;
    std::string              last_line_;   // for A/
    std::string              cmd_buf_;     // partial command-mode line
};

extern Modem modem;

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM
```

- [ ] **Step 2: Write the implementation — lifecycle and ports**

Create `lib/modem/modem.cpp` (GPL header first, then this; the remaining sections are appended in Steps 3 and 4):

```cpp
#include "modem.h"

#ifdef ENABLE_MODEM

#include <map>

#include <esp_heap_caps.h>
#include "esp_timer.h"

#include "meatloaf.h"
#include "mlConfig.h"
#include "fnWiFi.h"
#include "fnSystem.h"
#include "../../include/version.h"
#include "../../include/debug.h"

Modem modem;

namespace
{

uint32_t now_ms()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

} // namespace

class Modem::Lock
{
public:
    explicit Lock(SemaphoreHandle_t m) : m_(m)
    {
        if (m_ != nullptr)
            xSemaphoreTake(m_, portMAX_DELAY);
    }
    ~Lock()
    {
        if (m_ != nullptr)
            xSemaphoreGive(m_);
    }

private:
    SemaphoreHandle_t m_;
};

bool Modem::start()
{
    if (task_ != nullptr)
        return true;

    if (mutex_ == nullptr)
    {
        mutex_ = xSemaphoreCreateMutex();
        if (mutex_ == nullptr)
        {
            Debug_printv("modem: mutex allocation failed");
            return false;
        }
    }

    settings_.factory();
    loadConfig();

    stop_requested_ = false;
    if (xTaskCreatePinnedToCore(&Modem::taskEntry, "modem", TASK_STACK, this,
                                TASK_PRIORITY, &task_, TASK_CORE) != pdPASS)
    {
        // Report the numbers that actually explain the failure. Free heap and
        // largest contiguous block are different questions, and only the
        // second one explains an allocation failure.
        Debug_printv("modem: task create failed free_internal=%u largest=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        task_ = nullptr;
        return false;
    }
    return true;
}

void Modem::stop()
{
    stop_requested_ = true;
}

bool Modem::attach(ModemPort *port)
{
    if (port == nullptr)
        return false;

    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] == port)
            return true;
    }
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] == nullptr)
        {
            ports_[i] = port;
            return true;
        }
    }
    return false;
}

void Modem::detach(ModemPort *port)
{
    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] == port)
            ports_[i] = nullptr;
    }
}

bool Modem::sessionActive() const
{
    return state_ == ModemState::ONLINE || state_ == ModemState::ONLINE_COMMAND;
}

void Modem::broadcast(const std::string &s)
{
    if (s.empty())
        return;

    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] != nullptr && ports_[i]->isOpen())
            ports_[i]->pushTx((const uint8_t *)s.data(), s.size(), 100);
    }
}

void Modem::toAttached(const uint8_t *buf, size_t n)
{
    if (buf == nullptr || n == 0)
        return;

    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] != nullptr && ports_[i]->isOpen() && ports_[i]->attached())
            ports_[i]->pushTx(buf, n, 500);
    }
}

void Modem::sendResult(AtResult r)
{
    broadcast(at_format_result(r, settings_));
}

void Modem::taskEntry(void *arg)
{
    static_cast<Modem *>(arg)->run();
}

void Modem::run()
{
    Debug_printv("modem: task started");

    while (!stop_requested_)
    {
        if (state_ == ModemState::ONLINE)
            serviceStreamMode();
        else
            serviceCommandMode();
    }

    doHangup();
    Debug_printv("modem: task exiting");
    task_ = nullptr;
    vTaskDelete(nullptr);
}
```

- [ ] **Step 3: Append the two service loops**

Append to `lib/modem/modem.cpp`:

```cpp
void Modem::serviceCommandMode()
{
    uint8_t buf[64];
    size_t got = 0;

    {
        Lock lock(mutex_);
        for (size_t i = 0; i < MAX_PORTS && got == 0; ++i)
        {
            if (ports_[i] != nullptr && ports_[i]->isOpen())
                got = ports_[i]->popRx(buf, sizeof(buf), 0);
        }
    }

    if (got == 0)
    {
        // A suspended connection must still report NO CARRIER when the remote
        // hangs up -- that is the difference between ONLINE_COMMAND and being
        // offline, and it is what makes ATO meaningful.
        if (state_ == ModemState::ONLINE_COMMAND && conn_ != nullptr &&
            !conn_->isOpen())
        {
            conn_.reset();
            state_ = ModemState::COMMAND;
            sendResult(AtResult::NO_CARRIER);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }

    long cr = settings_.getRegister(AT_S_CR);
    long bs = settings_.getRegister(AT_S_BS);

    for (size_t i = 0; i < got; ++i)
    {
        uint8_t c = buf[i];

        if (c == '\n')
            continue;  // a CRLF terminal sends both; CR is the terminator

        if (c == (uint8_t)cr || c == '\r')
        {
            if (settings_.echo)
                broadcast("\r\n");
            std::string line = cmd_buf_;
            cmd_buf_.clear();
            executeLine(line);
            continue;
        }

        if (c == (uint8_t)bs || c == 0x7F)
        {
            if (!cmd_buf_.empty())
            {
                cmd_buf_.pop_back();
                if (settings_.echo)
                    broadcast("\b \b");
            }
            continue;
        }

        // A command line has no business being unbounded; a runaway sender
        // would otherwise grow this string until the heap gave out.
        if (cmd_buf_.size() < 256)
        {
            cmd_buf_.push_back((char)c);
            if (settings_.echo)
                broadcast(std::string(1, (char)c));
        }
    }
}

void Modem::serviceStreamMode()
{
    if (conn_ == nullptr || !conn_->isOpen())
    {
        conn_.reset();
        state_ = ModemState::COMMAND;
        escape_.reset();
        sendResult(AtResult::NO_CARRIER);
        return;
    }

    // Terminal -> remote, through the escape detector.
    uint8_t in[128];
    size_t got = 0;
    {
        Lock lock(mutex_);
        for (size_t i = 0; i < MAX_PORTS && got == 0; ++i)
        {
            if (ports_[i] != nullptr && ports_[i]->isOpen() &&
                ports_[i]->attached())
                got = ports_[i]->popRx(in, sizeof(in), 0);
        }
    }

    std::string forward;
    EscapeDetector::Verdict verdict = EscapeDetector::Verdict::NONE;

    for (size_t i = 0; i < got; ++i)
        verdict = escape_.feed(in[i], now_ms(), forward);

    // tick() is what completes the trailing guard and what releases held
    // bytes that turned out to be data, so it must run on every idle pass.
    if (got == 0)
        verdict = escape_.tick(now_ms(), forward);

    if (!forward.empty())
        conn_->write((const uint8_t *)forward.data(), (uint32_t)forward.size());

    if (verdict == EscapeDetector::Verdict::ESCAPED)
    {
        // The connection stays up. That distinction is what makes ATO
        // meaningful, and it is the one Zimodem does not draw.
        state_ = ModemState::ONLINE_COMMAND;
        cmd_buf_.clear();
        sendResult(AtResult::OK);
        return;
    }

    // Remote -> terminal.
    uint8_t out[512];
    uint32_t n = conn_->read(out, sizeof(out));
    if (n > 0)
        toAttached(out, n);
    else if (got == 0)
        vTaskDelay(pdMS_TO_TICKS(5));
}
```

- [ ] **Step 4: Append command dispatch, dial, and config**

Append to `lib/modem/modem.cpp`:

```cpp
void Modem::executeLine(const std::string &raw)
{
    std::string line = raw;

    if (at_is_repeat(line))
        line = last_line_;
    else if (!line.empty())
        last_line_ = line;

    if (line.empty())
        return;

    AtLine cmds;
    size_t err_pos = 0;
    if (!at_parse(line, cmds, &err_pos))
    {
        Debug_printv("modem: parse error at %u in [%s]", (unsigned)err_pos,
                     line.c_str());
        sendResult(AtResult::ERROR);
        return;
    }

    for (const auto &cmd : cmds)
    {
        bool reported = false;
        if (!executeCommand(cmd, reported))
        {
            if (!reported)
                sendResult(AtResult::ERROR);
            return;
        }
        // A successful dial has already emitted CONNECT and switched state;
        // anything after it on the line would be typed into the session.
        if (state_ == ModemState::ONLINE)
            return;
    }

    sendResult(AtResult::OK);
}

bool Modem::executeCommand(const AtCommand &cmd, bool &reported)
{
    if (cmd.prefix == '+')
    {
        if (cmd.name == "SHELL")
        {
            // Leaves modem mode. The modem, its settings and any connection
            // stay alive, which is what makes the shared model meaningful.
            // Detaching is the signal the shell pumps watch for.
            Lock lock(mutex_);
            for (size_t i = 0; i < MAX_PORTS; ++i)
            {
                if (ports_[i] != nullptr)
                    ports_[i]->setAttached(false);
            }
            return true;
        }
        return false;
    }

    if (cmd.prefix == '&')
    {
        switch (cmd.verb)
        {
        case 'S':
            if (cmd.query)
            {
                long v = settings_.getRegister(cmd.number);
                if (v < 0)
                    return false;
                broadcast("\r\n" + std::to_string(v) + "\r\n");
                return true;
            }
            return settings_.setRegister(cmd.number, cmd.value);
        case 'W':
            saveConfig();
            return true;
        case 'F':
            settings_.factory();
            return true;
        default:
            return false;
        }
    }

    switch (cmd.verb)
    {
    case 'D':
        return doDial(cmd, reported);

    case 'H':
        doHangup();
        reported = true;  // doHangup() emits NO CARRIER when it had a call
        return true;

    case 'O':
        return doReturnOnline(reported);

    case 'Z':
        doHangup();
        reported = false;  // ATZ answers OK even though it may have hung up
        settings_.factory();
        loadConfig();
        return true;

    case 'E':
        settings_.echo = (cmd.number != 0);
        return true;

    case 'Q':
        settings_.quiet = (cmd.number != 0);
        return true;

    case 'V':
        settings_.verbose = (cmd.number != 0);
        return true;

    case 'X':
        if (cmd.number < 0 || cmd.number > 4)
            return false;
        settings_.xlevel = (uint8_t)cmd.number;
        return true;

    case 'S':
        if (cmd.query)
        {
            long v = settings_.getRegister(cmd.number);
            if (v < 0)
                return false;
            broadcast("\r\n" + std::to_string(v) + "\r\n");
            return true;
        }
        return settings_.setRegister(cmd.number, cmd.value);

    case 'I':
        doInfo(cmd.number);
        return true;

    case 'P':
        return doPhonebook(cmd);

    // Phase 3 owns ATA. Until then it is a recognised command that cannot
    // succeed, which is a clearer answer than "unknown command".
    case 'A':
        return false;

    default:
        return false;
    }
}

bool Modem::doDial(const AtCommand &cmd, bool &reported)
{
    if (sessionActive())
    {
        sendResult(AtResult::BUSY);
        reported = true;
        return false;
    }

    if (!fnWiFi.connected())
    {
        sendResult(AtResult::NO_DIALTONE);
        reported = true;
        return false;
    }

    std::string target = cmd.arg;
    std::string mods = cmd.mods;

    // Bare digits are a phonebook lookup.
    if (!target.empty() &&
        target.find_first_not_of("0123456789") == std::string::npos)
    {
        const PhonebookEntry *e = phonebook_.find(target);
        if (e == nullptr)
            return false;
        target = e->host + ":" + std::to_string(e->port);
        if (mods.empty())
            mods = e->mods;
    }

    std::string host;
    uint16_t port = 23;
    if (!phonebook_split_hostport(target, host, port))
        return false;

    // ATDT dials telnet; so does a plain ATD when S62 is set, which is the
    // Zimodem convention terminal programs already send.
    bool use_telnet = (mods.find('T') != std::string::npos) ||
                      (settings_.getRegister(AT_S_TELNET) != 0);

    std::string url = (use_telnet ? "telnet://" : "tcp://") + host + ":" +
                      std::to_string(port);

    state_ = ModemState::DIALING;
    Debug_printv("modem: dialing %s", url.c_str());

    auto file = std::shared_ptr<MFile>(MFSOwner::File(url));
    std::shared_ptr<MStream> stream;
    if (file != nullptr)
        stream = file->getSourceStream(std::ios_base::in | std::ios_base::out);

    if (stream == nullptr || !stream->isOpen())
    {
        state_ = ModemState::COMMAND;
        sendResult(AtResult::NO_ANSWER);
        reported = true;
        return false;
    }

    conn_ = stream;
    escape_.configure((uint8_t)settings_.getRegister(AT_S_ESCCHAR),
                      (uint16_t)settings_.getRegister(AT_S_GUARD));
    escape_.reset();

    state_ = ModemState::ONLINE;
    sendResult(AtResult::CONNECT);
    reported = true;
    return true;
}

void Modem::doHangup()
{
    if (conn_ != nullptr)
    {
        conn_->close();
        conn_.reset();
    }
    escape_.reset();

    if (state_ != ModemState::COMMAND)
    {
        state_ = ModemState::COMMAND;
        sendResult(AtResult::NO_CARRIER);
    }
}

bool Modem::doReturnOnline(bool &reported)
{
    if (state_ != ModemState::ONLINE_COMMAND || conn_ == nullptr ||
        !conn_->isOpen())
        return false;

    escape_.reset();
    state_ = ModemState::ONLINE;
    sendResult(AtResult::CONNECT);
    reported = true;
    return true;
}

void Modem::doInfo(long which)
{
    std::string out = "\r\n";

    switch (which)
    {
    case 1:
        out += "E" + std::string(settings_.echo ? "1" : "0");
        out += " Q" + std::string(settings_.quiet ? "1" : "0");
        out += " V" + std::string(settings_.verbose ? "1" : "0");
        out += " X" + std::to_string((int)settings_.xlevel);
        out += "\r\nS0=" + std::to_string(settings_.getRegister(AT_S_AUTOANSWER));
        out += " S2=" + std::to_string(settings_.getRegister(AT_S_ESCCHAR));
        out += " S7=" + std::to_string(settings_.getRegister(AT_S_CONNTIMEOUT));
        out += " S12=" + std::to_string(settings_.getRegister(AT_S_GUARD));
        out += " S62=" + std::to_string(settings_.getRegister(AT_S_TELNET));
        out += "\r\n";
        break;

    case 2:
        out += fnSystem.Net.get_ip4_address_str() + "\r\n";
        break;

    case 3:
        out += std::string(fnWiFi.connected() ? "connected" : "not connected") + "\r\n";
        break;

    case 4:
        out += std::string(FN_VERSION_FULL) + "\r\n";
        break;

    case 5:
        for (long n = 0; n < AtSettings::REGISTER_COUNT; ++n)
        {
            long v = settings_.getRegister(n);
            if (v != 0)
                out += "S" + std::to_string(n) + "=" + std::to_string(v) + "\r\n";
        }
        break;

    default:
        out += "Meatloaf modem " + std::string(FN_VERSION_FULL) + "\r\n";
        break;
    }

    broadcast(out);
}

bool Modem::doPhonebook(const AtCommand &cmd)
{
    // Bare ATP lists; the parser sets assign only when an '=' was present.
    if (!cmd.assign)
    {
        std::string out = "\r\n";
        for (const auto &e : phonebook_.all())
        {
            out += e.number + ": " + e.host + ":" + std::to_string(e.port);
            if (!e.mods.empty())
                out += " " + e.mods;
            out += "\r\n";
        }
        broadcast(out);
        return true;
    }

    std::string number = std::to_string(cmd.number);

    // ATPn= with no argument deletes.
    if (cmd.arg.empty())
        return phonebook_.erase(number);

    return phonebook_.store(number, cmd.arg, cmd.mods);
}

void Modem::loadConfig()
{
    auto &root = mlConfig.data();
    if (!root.contains("modem"))
        return;

    auto &m = root["modem"];

    if (m.contains("settings") && m["settings"].is_object())
    {
        std::map<std::string, long> kv;
        for (auto it = m["settings"].begin(); it != m["settings"].end(); ++it)
        {
            if (it.value().is_number_integer())
                kv[it.key()] = it.value().template get<long>();
        }
        settings_.fromKeyValues(kv);
    }

    if (m.contains("phonebook") && m["phonebook"].is_object())
    {
        phonebook_.clear();
        for (auto it = m["phonebook"].begin(); it != m["phonebook"].end(); ++it)
        {
            auto &e = it.value();
            if (!e.contains("host"))
                continue;
            std::string hostport = e["host"].template get<std::string>();
            if (e.contains("port"))
                hostport += ":" + std::to_string(e["port"].template get<int>());
            std::string mods = e.contains("mods")
                                   ? e["mods"].template get<std::string>()
                                   : std::string();
            phonebook_.store(it.key(), hostport, mods);
        }
    }
}

void Modem::saveConfig()
{
    auto &root = mlConfig.data();

    // Rebuild both subtrees rather than merging, so a deleted phonebook entry
    // or a register returned to its default actually disappears.
    root["modem"]["settings"] = psram_json::object();
    for (const auto &pair : settings_.toKeyValues())
        root["modem"]["settings"][pair.first] = pair.second;

    root["modem"]["phonebook"] = psram_json::object();
    for (const auto &e : phonebook_.all())
    {
        root["modem"]["phonebook"][e.number]["host"] = e.host;
        root["modem"]["phonebook"][e.number]["port"] = (int)e.port;
        if (!e.mods.empty())
            root["modem"]["phonebook"][e.number]["mods"] = e.mods;
    }

    // save() hashes each section and writes only what changed; there are no
    // dirty flags to set.
    mlConfig.save();
}

#endif // ENABLE_MODEM
```

- [ ] **Step 5: Build**

```bash
rm -f .pio/build/esp32-s3-devkitc-1/CMakeCache.txt
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1 > /tmp/modem-build.log 2>&1
grep -E "SUCCESS|FAILED|error:|overflowed" /tmp/modem-build.log
```

Expected: `SUCCESS`.

If a symbol from `fnWiFi`, `fnSystem` or `version.h` does not match — `FN_VERSION_FULL`, `fnSystem.Net.get_ip4_address_str()` and `fnWiFi.connected()` are taken from usage elsewhere in the tree but were not verified against their declarations while writing this plan — **read the real declaration and correct the call**. Do not stub it out or delete the line.

- [ ] **Step 6: Commit**

```bash
git add lib/modem/modem.h lib/modem/modem.cpp
git commit -m "feat(modem): engine, state machine and task

+++ yields ONLINE_COMMAND with the connection still up, not OFFLINE.
That is what makes ATO meaningful and is the distinction Zimodem does
not draw; NO CARRIER fires from that state too, so a suspended session
still reports a remote hangup.

Commands run left to right and stop at the first failure, which is real
modem behaviour and stops AT&S62=1DT\"bad\" from half-applying. A command
that already emitted its own result reports that, so a failed dial does
not get a second ERROR after its NO ANSWER.

Dial resolves tcp:// or telnet:// through MFSOwner, so the engine
touches no sockets. ATDT picks telnet, and so does a plain ATD when S62
is set - the Zimodem convention terminal programs already send.

saveConfig() rebuilds both subtrees rather than merging, so a deleted
phonebook entry actually disappears. mlConfig.save() detects dirtiness
by hashing; there are no flags to set."
```

---

### Task 11: Console `at` intercept and boot wiring

**Files:**
- Modify: `lib/console/Console.cpp` (`repl_task()` and `Console::execute()`)
- Modify: `lib/console/tcpsvr.cpp` and `lib/console/tcpsvr.h`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `Modem` and `ModemPort` from Tasks 9–10.
- Produces:
  - `at` as a raw-line command in both shells
  - `static void modem_shell_pump(ESP32Console::Console::Origin origin)` in `Console.cpp`
  - `bool TCPServer::modemFeed(const char *buf, size_t n)` in `tcpsvr.cpp`
  - `void TCPServer::setModemSink(ModemPort *port)` in `tcpsvr.cpp`

- [ ] **Step 1: Give TCPServer a modem sink**

The serial shell reads its own fd inside the pump loop. A TCP session cannot: `session_task()` owns the client socket and is the only task allowed to `recv()` on it. So received bytes must be handed to the modem from there, not pulled from inside the pump.

In `lib/console/tcpsvr.h`, inside `class TCPServer`, add:

```cpp
#ifdef ENABLE_MODEM
    // Set while this session is in modem mode. session_task() routes received
    // bytes here instead of to console.execute(), because the modem consumes a
    // byte stream rather than lines -- and because session_task() must remain
    // the only reader of the client socket.
    static void setModemSink(class ModemPort *port);
    static bool modemFeed(const char *buf, size_t n);

private:
    static class ModemPort *_modem_sink;
public:
#endif
```

In `lib/console/tcpsvr.cpp`, add near the other static member definitions (beside `TCPServer::_client_socket`):

```cpp
#ifdef ENABLE_MODEM
#include "modem_port.h"

ModemPort *TCPServer::_modem_sink = nullptr;

void TCPServer::setModemSink(ModemPort *port)
{
    _modem_sink = port;
}

bool TCPServer::modemFeed(const char *buf, size_t n)
{
    if (_modem_sink == nullptr || buf == nullptr || n == 0)
        return false;
    _modem_sink->pushRx((const uint8_t *)buf, n, 100);
    return true;
}
#endif
```

Then, in `session_task()`, where a received buffer is currently handed to `console.execute()`, add the modem branch **before** that call:

```cpp
#ifdef ENABLE_MODEM
            // In modem mode the session is a byte stream, not a line-oriented
            // shell. Feed it and skip the command path entirely.
            if (TCPServer::modemFeed(rx_buffer, len))
                continue;
#endif
```

- [ ] **Step 2: Add the pump helper to Console.cpp**

Near the top of `lib/console/Console.cpp`, after the existing includes, add:

```cpp
#ifdef ENABLE_MODEM
#include "modem.h"
#include "modem_port.h"

// Runs one console's side of a modem session. The shell task stays the sole
// reader and writer of its own fd and pumps bytes to and from the modem task
// through a ModemPort. Two tasks on one fd is the condition behind the NFS
// 0x6400 heap corruption; see modem_port.h.
//
// Returns when the session ends -- AT+SHELL detaches the port, or the modem
// stops.
static void modem_shell_pump(ESP32Console::Console::Origin origin)
{
    if (!modem.isRunning())
    {
        ::printf("modem: not running\r\n");
        return;
    }

    ModemPort port;
    if (!port.begin())
    {
        ::printf("modem: could not allocate port buffers\r\n");
        return;
    }

    if (!modem.attach(&port))
    {
        ::printf("modem: no free port (in use from another console)\r\n");
        return;
    }
    port.setAttached(true);

#ifdef ENABLE_CONSOLE_TCP
    if (origin == ESP32Console::Console::ORIGIN_REMOTE)
        TCPServer::setModemSink(&port);
#endif

    // Raw byte mode for the session: the console driver's interactive
    // line-end translation (RX \r -> \n, TX \n -> \r\n) corrupts a BBS stream,
    // and the modem emits its own S3/S4 terminators.
    ConsoleRawIOGuard raw_guard;

    ::printf("\r\nModem mode. AT+SHELL to return.\r\n");

    while (port.attached() && modem.isRunning())
    {
        // Modem -> terminal.
        uint8_t out[128];
        size_t n = port.popTx(out, sizeof(out), 20);
        if (n > 0)
        {
            if (origin == ESP32Console::Console::ORIGIN_SERIAL)
            {
                fwrite(out, 1, n, stdout);
                fflush(stdout);
            }
#ifdef ENABLE_CONSOLE_TCP
            else
            {
                tcp_server.send(std::string((const char *)out, n));
            }
#endif
        }

        // Terminal -> modem. Serial reads its own fd here; a TCP session's
        // bytes arrive via TCPServer::modemFeed() from session_task(), which
        // must stay the only reader of that socket.
        if (origin == ESP32Console::Console::ORIGIN_SERIAL)
        {
            uint8_t in[64];
            size_t got = 0;
            int fd = fileno(stdin);
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            while (got < sizeof(in))
            {
                int c = fgetc(stdin);
                if (c == EOF)
                    break;
                in[got++] = (uint8_t)c;
            }
            fcntl(fd, F_SETFL, fl);
            if (got > 0)
                port.pushRx(in, got, 100);
        }
    }

#ifdef ENABLE_CONSOLE_TCP
    if (origin == ESP32Console::Console::ORIGIN_REMOTE)
        TCPServer::setModemSink(nullptr);
#endif

    modem.detach(&port);
    port.end();
    ::printf("\r\nModem mode exited.\r\n");
}
#endif // ENABLE_MODEM
```

- [ ] **Step 3: Intercept `at` in repl_task()**

In `repl_task()`, immediately after the existing `if (raw_line == "exit")` block, add:

```cpp
#ifdef ENABLE_MODEM
            // "at" enters modem mode. Intercepted as a raw line, before
            // esp_console_run()'s splitter: that splitter drops arguments past
            // CONSOLE_MAX_CMDLINE_ARGS and only strips a leading quote, so a
            // line like AT&S62=1DT"host:23" would not survive it.
            {
                std::string lowered = raw_line;
                mstr::toLower(lowered);
                if (lowered == "at")
                {
                    modem_shell_pump(ORIGIN_SERIAL);
                    continue;
                }
            }
#endif
```

If `mstr::toLower` does not exist with that signature, use whatever the codebase's lowercase helper is — check `lib/utils/string_utils.h`.

- [ ] **Step 4: Intercept `at` in Console::execute()**

In `Console::execute()`, immediately after the existing `#ifdef ENABLE_CONSOLE_TCP` / `if (command_str == "exit")` block, add:

```cpp
#ifdef ENABLE_MODEM
        {
            std::string lowered = command_str;
            mstr::toLower(lowered);
            if (lowered == "at")
            {
                modem_shell_pump(ORIGIN_REMOTE);
                return;
            }
        }
#endif
```

- [ ] **Step 5: Create the modem task at boot**

In `src/main.cpp`, add the include at the top:

```cpp
#ifdef ENABLE_MODEM
#include "modem.h"
#endif
```

And inside `main_setup()`, after the `fnWiFi.start()` and `SessionBroker::setup()` calls:

```cpp
#ifdef ENABLE_MODEM
    // Created at boot, not on first `at`: task stacks are internal-DRAM only
    // with no PSRAM fallback and can fail from fragmentation, so the stack is
    // claimed while contiguous internal RAM is still available. The task idles
    // on its ports until a console enters modem mode.
    if (!modem.start())
        Debug_printv("modem: failed to start; `at` will be unavailable");
#endif
```

- [ ] **Step 6: Build**

```bash
rm -f .pio/build/esp32-s3-devkitc-1/CMakeCache.txt
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1 > /tmp/modem-build.log 2>&1
grep -E "SUCCESS|FAILED|error:|overflowed" /tmp/modem-build.log
```

Expected: `SUCCESS`.

- [ ] **Step 7: Confirm the native suite is unchanged**

This task touched no pure unit, so any change there is a regression:

Run: `~/.platformio/penv/Scripts/pio.exe test -e native -f native/test_modem_at`
Expected: PASS, 63 cases.

- [ ] **Step 8: Commit**

```bash
git add lib/console/Console.cpp lib/console/tcpsvr.cpp lib/console/tcpsvr.h \
        src/main.cpp
git commit -m "feat(modem): 'at' raw-line intercept and boot wiring

Intercepted in repl_task() and Console::execute(), the same place exit
and reboot already are, and before esp_console_run()'s splitter - that
splitter drops arguments past CONSOLE_MAX_CMDLINE_ARGS and only strips a
leading quote, so AT&S62=1DT\"host:23\" would not survive it.

The shell task stays the sole reader and writer of its own fd. Serial
reads inside the pump; a TCP session's bytes arrive through
TCPServer::modemFeed() from session_task(), which must remain the only
reader of that socket. ConsoleRawIOGuard disables the driver's
interactive line-end translation for the session: it corrupts a BBS
stream, and the modem emits its own S3/S4 terminators.

The task is created at boot rather than on first 'at' - task stacks are
internal-DRAM only and can fail from fragmentation."
```

---

### Task 12: Hardware verification

Nothing below the pure units has a regression test: the dial path, `TelnetMStream`, `ModemPort` and the task model need ESP-IDF and a real socket. This task is the evidence for all of it.

**Files:**
- Modify: `AGENTS.md` (Recent Changes entry)

- [ ] **Step 1: Flash and open the console**

```bash
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/Scripts/pio.exe device monitor -b 2000000 --filter esp32_exception_decoder
```

If a serial capture daemon is running, **stop it first** — on Windows the port is exclusive and `esptool` fails with "Could not open COM7, the port is busy".

The REPL is dormant in `fgetc()` until the first byte, so send a throwaway ENTER before the first real command or it arrives with its first character missing.

- [ ] **Step 2: Verify command mode**

At the Meatloaf prompt type `at` and ENTER, then work through:

| Input | Expected |
|---|---|
| `AT` | `OK` |
| `ATI1` | the settings line, then `OK` |
| `ATS12?` | `50`, then `OK` |
| `ATS12=25` then `ATS12?` | `25` |
| `ATV0` then `AT` | `0` (numeric result) |
| `ATV1` then `AT` | `OK` |
| `ATQ1` then `AT` | nothing at all |
| `ATQ0` then `AT` | `OK` |
| `ATZZZ` | `ERROR` |
| `AT+SHELL` | back to the `meatloaf[/]#` prompt |

Record the actual output. A mismatch here is a defect in Tasks 1–3 that the native suite did not reach.

- [ ] **Step 3: Verify dial, escape, resume and hangup**

Enter modem mode again and dial a known telnet BBS. Verify in order:

1. `ATDT"bbs.example.com:23"` → `CONNECT`, then the BBS banner renders cleanly.
2. **No stray IAC bytes on screen** — no `ÿý` sequences. That is the whole point of Task 6; if they appear, negotiation is not running.
3. Type `+++` with a pause before and after → `OK`, and the connection is still up.
4. `ATO` → `CONNECT`, and typing reaches the BBS again.
5. `ATH` → `NO CARRIER`.
6. `ATD"bbs.example.com:23"` (no T, S62 still 0) → `CONNECT`, raw TCP. IAC bytes now *may* appear; that is correct for a non-negotiated dial.
7. `AT&S62=1DT"bbs.example.com:23"` → both commands apply and it connects negotiated, proving the multi-command line works on real input.

- [ ] **Step 4: Verify the escape near-miss**

Reconnect, then type `+++` **immediately after** other text with no pause. The three characters must reach the BBS and the modem must stay online. This is the case the native tests cover and the one users hit by accident.

- [ ] **Step 5: Verify persistence**

```
ATP1="bbs.example.com:23",T
ATP
AT&W
```

Then `AT+SHELL`, `reboot`, and after boot `at` + `ATP` — the entry must still be listed, and `ATD1` must dial it. Confirm `ATS12=25` + `AT&W` survives the reboot too.

- [ ] **Step 6: Verify the shared/observable model**

With the serial console in modem mode and a TCP console connected:

1. On serial, `AT` → both consoles see `OK`.
2. On TCP, type `at` → it attaches (there are two port slots) and both see command output.
3. Dial from serial → BBS data appears **only** on the serial console, since it is the attached port.
4. `AT+SHELL` from either → both return to their shell prompt.

- [ ] **Step 7: Record memory, stack and flash**

At the Meatloaf prompt (not in modem mode):

```
meminfo
ps
```

Record free internal heap and largest contiguous block, both idle and with a session up, and the `modem` task's stack high-water mark. **If the high-water mark shows less than ~2 KB free, raise `Modem::TASK_STACK` and re-measure** rather than leaving it marginal.

```bash
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1 -t size > /tmp/modem-size.log 2>&1
grep -E "Flash|RAM" /tmp/modem-size.log
```

- [ ] **Step 8: Write the AGENTS.md entry**

Add a `## Recent Changes (September 8, 2026)` section covering:

- What phase 1 delivers and what it explicitly does not (phases 2–5: connection registry, listeners, SSH, transfers).
- The measured numbers from Step 7: flash percentage, free internal heap idle and with a session, modem task high-water mark.
- Exactly what was verified and what was not. State plainly that the dial path, `TelnetMStream`, `ModemPort` and the task model have **no** native regression test and are hardware-verified only, and that nothing has been driven from a real C64 — this is a console feature by design.
- The durable rules worth keeping in Important Notes:
  - The modem task never touches a console fd; shells pump through `ModemPort` StreamBuffers. Two tasks on one socket is the NFS `0x6400` condition.
  - `+++` yields ONLINE_COMMAND with the connection still up, not OFFLINE — that is what makes `ATO` meaningful, and NO CARRIER must fire from that state too.
  - The `+++` rule is a leading guard, three characters, then a trailing guard, with the bytes **held** until the trailing guard expires.
  - `at` is intercepted before `esp_console_run()`'s splitter because `AT&S62=1DT"host:23"` would not survive it.
  - `ENABLE_MODEM` is opt-in and enabled only on `esp32-s3-devkitc-1`.
  - A `telnet://` stream decorates `tcp://` rather than subclassing it, which is what makes `ATD` and `ATDT` one dial path.

- [ ] **Step 9: Commit**

```bash
git add AGENTS.md
git commit -m "docs: record modem mode phase 1 hardware verification

Phase 1 delivers the AT engine, settings and phonebook persistence,
single dial-out over tcp:// and telnet://, the +++ escape and the
ONLINE_COMMAND state. Phases 2-5 (connection registry, listeners, SSH,
transfers) are not present.

States what the native suite covers and what is hardware-verified only:
the dial path, TelnetMStream, ModemPort and the task model have no
regression test under them."
```

---

## Self-Review

**Spec coverage.** Every phase-1 item maps to a task: AT parser (1), settings and S-registers (2), `AT&W` (2, 10), result codes (3), escape detector (4), phonebook (5), telnet negotiation (6, 8), `MStream::waitReadable()` (7), `ModemPort` (9), state machine and dial and task (10), `at` intercept and boot wiring (11), gating (8, 11), hardware verification (12).

**Deviations from the spec, both deliberate:**

1. **The telnet filter is split out of `lib/meatloaf/network/telnet.h` into a pure `lib/modem/telnet_filter.*`.** The spec put it all in one file. Splitting makes libtelnet's negotiation natively testable — 8 of the 63 native cases — and follows the precedent `dos_encode.cpp` set for exactly this reason. Strict improvement.
2. **`ATP` modifier syntax is `ATP1="host:23",T`** — modifiers *after* the quoted argument, unlike `ATD` where they precede it. The spec wrote `ATP<n>="host:port"[,modifiers]`, which is what a user would type; the parser now implements that form specifically (Task 1, `test_parse_phonebook_store_with_trailing_modifiers`) rather than forcing the `ATD` shape.

**Placeholder scan.** No TBD, no "add appropriate error handling", no "similar to Task N". Tasks 8, 9, 10 and 11 have no native test; each says so explicitly with the reason rather than leaving the gap unremarked.

**Type consistency.** `AtSettings::getRegister` returns `long` throughout. `AtCommand::assign` is introduced in Task 1 and consumed in Task 10's `doPhonebook()` to tell `ATP` from `ATP1=`. `executeCommand(cmd, reported)` carries the two-argument form consistently between its declaration (Task 10 Step 1), its definition (Step 4) and its caller in `executeLine()`. `EscapeDetector::Verdict` is compared with `==` rather than `TEST_ASSERT_EQUAL`, since Unity cannot compare a scoped enum. `phonebook_split_hostport` has one signature, used in Tasks 5 and 10. `ModemPort` method names match across header, implementation, `modem_shell_pump()` and `TCPServer::modemFeed()`.

**Two things the implementer must verify rather than trust.** Both are flagged in place with instructions to read the real declaration and correct the call:

- `FN_VERSION_FULL`, `fnSystem.Net.get_ip4_address_str()` and `fnWiFi.connected()` in Task 10 are taken from usage elsewhere in the tree, not checked against their declarations.
- `MFileSystem::handles()` may not be virtual — `lib/meatloaf/network/tcp.h` declares it without `override`. Task 8 says to match that file.

**One risk carried deliberately.** `Modem::TASK_STACK` is 8192 by reasoning from AGENTS.md's measured points (HTTPS+TLS 4360, AFP 9332), not by measurement. Task 12 Step 7 measures it and says to raise it if the margin is under ~2 KB.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-09-08-console-modem-mode-phase-1.md`.** Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
