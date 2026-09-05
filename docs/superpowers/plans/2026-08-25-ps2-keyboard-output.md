# PS/2 Keyboard Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Meatloaf act as a PS/2 keyboard, sending keystrokes to an attached
Commodore DTV64 on command from the console or WebSocket.

**Architecture:** Keep the vendored `components/ps2/` and patch it (P1-P8);
add a thin Meatloaf device class in `lib/device/ps2/` following the `DisplayLEDs`
idiom; add a `ps2` console command that WebSocket reaches for free through
`console.execute()`. Compiled on any board whose pinmap defines `PIN_KB_CLK`.

**Tech Stack:** ESP-IDF via PlatformIO, FreeRTOS, `esp_rom_delay_us`,
`esp_timer_get_time`, GPIO ISR, Unity (native tests).

**Spec:** `docs/superpowers/specs/2026-08-25-ps2-keyboard-output-design.md`

## Global Constraints

Every task's requirements implicitly include this section.

- **The build guard is `PIN_KB_CLK`.** `ENABLE_PS2` must not appear anywhere in
  the tree when this plan is done.
- **Never PETSCII-encode on the PS/2 path.** PS/2 goes to a keyboard controller;
  `mstr::toPETSCII2()` belongs to the IEC boundary only.
- **`pdMS_TO_TICKS(n)` is 0 for n < 10** at `CONFIG_FREERTOS_HZ=100`. Sub-tick
  waits use `esp_rom_delay_us`; tick waits are written `vTaskDelay(1)` literally.
- **Never call `gpio_install_isr_service()`** — `src/main.cpp:238` already does.
- **PS/2 task priority stays 10 / 9 and core stays 0.** Core 1 is IEC's at
  priority 17 and must never contend.
- **Builds are the user's to run.** Check `pgrep -f "platformio run"` first;
  never start a second concurrent build. `pio` is at `/usr/local/bin/pio`.
- **`fujiloaf-rev0` does not build at baseline** — `lib/bus/iec/IECConfig.h` has
  an unbalanced preprocessor chain (line 67 closes the include guard early),
  unrelated to this work. Task 1 establishes which pinned board can be used.

---

### Task 1: Switch the guard to `PIN_KB_CLK` and remove `ENABLE_PS2`

Makes the pin a real capability guard, the way `PIN_TFT_MOSI` works for the
display. No behavior change — the component is unreferenced before and after.

**Files:**
- Modify: `include/pinmap_defaults.h:30-33`
- Modify: `include/pinmap/adafruit_feather_esp32s3_tft.h:47-48`
- Modify: `platformio.ini.sample:245`
- Modify: `src/main.cpp:62-65`

**Interfaces:**
- Consumes: nothing.
- Produces: `PIN_KB_CLK` / `PIN_KB_DATA` defined **only** on boards with the
  hardware — `fujiloaf-rev0` and `esp32-s3-super-mini`, both `GPIO_NUM_16` /
  `GPIO_NUM_17`. Later tasks guard on `#ifdef PIN_KB_CLK`.

- [ ] **Step 1: Establish which pinned board builds at baseline**

`fujiloaf-rev0` is known broken for unrelated reasons. Find out about the other.

```bash
pgrep -f "platformio run" && echo "WAIT - build already running" || \
  pio run -e esp32-s3-super-mini 2>&1 | tail -30
```

Record the result. If it builds, it is the compile target for every later task.
If it does not, note the reason and use `lolin-d32-pro` for
"nothing regressed" checks only — the pinned-board path then cannot be
compile-verified until `IECConfig.h` is repaired, and that must be stated in
every later task's completion note rather than glossed.

- [ ] **Step 2: Delete the PS/2 block from `pinmap_defaults.h`**

Remove these four lines entirely (lines 30-33). Do **not** merely fix the
trailing semicolons — the block existing at all is what breaks the guard.

```c
// PS/2 Keyboard
#ifndef PIN_KB_CLK
#define PIN_KB_CLK              GPIO_NUM_NC;
#define PIN_KB_DATA             GPIO_NUM_NC;
#endif
```

`PIN_TFT_MOSI` appears in no pinmap default, which is exactly why its guard
works. PS/2 now matches.

- [ ] **Step 3: Drop the false definitions in the Adafruit pinmap**

In `include/pinmap/adafruit_feather_esp32s3_tft.h`, delete lines 47-48:

```c
#define PIN_KB_CLK              GPIO_NUM_NC
#define PIN_KB_DATA             GPIO_NUM_NC
```

That board has no PS/2 hardware; leaving these would satisfy `#ifdef` and
compile the feature onto it.

- [ ] **Step 4: Remove the `ENABLE_PS2` flag from `platformio.ini.sample`**

Delete line 245: `    ;-D ENABLE_PS2`

- [ ] **Step 5: Remove the stray global from `main.cpp`**

Delete lines 62-65:

```cpp
#ifdef ENABLE_PS2
#include "ps2_keyboard.h"
ps2dev::PS2Keyboard keyboard(PIN_KB_CLK, PIN_KB_DATA);
#endif
```

The device class replaces this in Task 9. Removing it now leaves the component
unreferenced, which keeps the tree linkable through Tasks 3-8.

- [ ] **Step 6: Verify `ENABLE_PS2` is gone and the guard is honest**

```bash
grep -rn "ENABLE_PS2" --include='*.h' --include='*.cpp' --include='*.ini' . | grep -v '\.pio/'
grep -rn "PIN_KB_CLK" include/ | grep -v '\.pio/'
```

Expected: the first command prints nothing. The second prints exactly two lines,
`fujiloaf-rev0.h` and `esp32-s3-super-mini.h`, both `GPIO_NUM_16`.

- [ ] **Step 7: Verify nothing regressed**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS. `ml_tests.cpp:847` references `PIN_KB_CLK` but is commented
out, so nothing breaks.

- [ ] **Step 8: Commit**

```bash
git add include/pinmap_defaults.h include/pinmap/adafruit_feather_esp32s3_tft.h \
        platformio.ini.sample src/main.cpp
git commit -m "ps2: guard on PIN_KB_CLK, remove ENABLE_PS2

A board that wires the pins has the hardware; that is the whole condition,
matching the PIN_TFT_MOSI pattern in lib/display/lcd.h. Deleting the
pinmap_defaults.h block also removes its trailing-semicolon defect."
```

---

### Task 2: Key names and the ASCII escape decoder (TDD, natively tested)

The only genuinely test-first task in this plan: pure logic with no ESP-IDF
dependency. `lib/console` and `lib/device` are not compiled in the native
environment, so this follows the `lib/console/dos_encode.*` precedent — the
logic lives in a unit that depends on nothing but `<string>` and a header.

The `0xNN` run escape is **shared with `encodeDosCommand`, not cloned.** That
function already implements exactly the semantics we want; the only difference
is that PS/2 must not PETSCII-convert.

**Files:**
- Modify: `lib/console/dos_encode.h`
- Modify: `lib/console/dos_encode.cpp`
- Create: `lib/device/ps2/ps2_keynames.h`
- Create: `lib/device/ps2/ps2_keynames.cpp`
- Modify: `src/CMakeLists.txt`
- Test: `test/native/test_ps2_keys/test_ps2_keys.cpp`
- Test: `test/native/test_ps2_keys/engine_sources.cpp`

**Interfaces:**
- Consumes: `ps2dev::scancodes::Key` from
  `components/ps2/src/scan_codes_set_2.h` (includes only `<stdint.h>`, so it is
  native-safe). Exact enumerator spellings used below —
  note `K_SCROLLOCK` has one `L`, the print key is `K_PRINT`, and the Windows
  keys are `K_LSUPER` / `K_RSUPER`.
- Produces:
  - `std::string ESP32Console::encodeAsciiCommand(const std::string &line)`
  - `namespace ps2keys`:
    - `using Key = ps2dev::scancodes::Key;`
    - `using Overrides = std::map<std::string, std::string>;`
    - `bool lookupKey(const std::string &name, Key &out, const Overrides *ov = nullptr);`
    - `bool parseCombo(const std::string &spec, std::vector<Key> &out, const Overrides *ov = nullptr);`
    - `const char *keyName(Key k);`
    - `void allNames(std::vector<const char *> &out);`

- [ ] **Step 1: Write the failing test**

Create `test/native/test_ps2_keys/test_ps2_keys.cpp`:

```cpp
// PS/2 key-name lookup and the ASCII escape decoder.
//
// lib/console and lib/device are not compiled natively, so the pure logic of
// the `ps2` command lives in units that depend on nothing but <string> and
// scan_codes_set_2.h. Everything below the name table -- the wire, the tasks,
// the ISR -- is hardware-verified only.
//
//   pio test -e native -f native/test_ps2_keys

#include <unity.h>

#include <string>
#include <vector>
#include <map>

#include "../../../lib/device/ps2/ps2_keynames.h"
#include "../../../lib/console/dos_encode.h"

using ps2keys::Key;
using ps2keys::Overrides;
using ESP32Console::encodeAsciiCommand;
using ESP32Console::encodeDosCommand;

void setUp(void) {}
void tearDown(void) {}

// ------------------------------------------------------------ key lookup --

static void test_lookup_exact_name(void)
{
    Key k;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("enter", k));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_RETURN, k);
}

static void test_lookup_is_case_insensitive(void)
{
    Key a, b, c;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("ENTER", a));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("Enter", b));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("enter", c));
    TEST_ASSERT_EQUAL(a, b);
    TEST_ASSERT_EQUAL(b, c);
}

static void test_lookup_aliases_agree(void)
{
    Key ret, enter, esc, escape;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("return", ret));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("enter", enter));
    TEST_ASSERT_EQUAL(ret, enter);

    TEST_ASSERT_TRUE(ps2keys::lookupKey("esc", esc));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("escape", escape));
    TEST_ASSERT_EQUAL(esc, escape);
}

static void test_bare_modifier_resolves_to_left_hand_key(void)
{
    Key ctrl, lctrl, rctrl;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("ctrl", ctrl));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("lctrl", lctrl));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("rctrl", rctrl));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_LCTRL, ctrl);
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_LCTRL, lctrl);
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_RCTRL, rctrl);
}

static void test_function_keys_span_f1_to_f12(void)
{
    Key f1, f12;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("f1", f1));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("f12", f12));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_F1, f1);
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_F12, f12);
}

static void test_unknown_name_is_rejected(void)
{
    Key k;
    TEST_ASSERT_FALSE(ps2keys::lookupKey("nosuchkey", k));
    TEST_ASSERT_FALSE(ps2keys::lookupKey("", k));
}

static void test_single_letter_and_digit_names(void)
{
    Key a, five;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("a", a));
    TEST_ASSERT_TRUE(ps2keys::lookupKey("5", five));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_A, a);
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_5, five);
}

// The DTV's scancode-to-C64 mapping is a bench finding, so C64 key names are
// bound through the override map rather than guessed in code.
static void test_override_rebinds_a_name(void)
{
    Overrides ov = { { "restore", "pageup" } };
    Key k;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("restore", k, &ov));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_PAGEUP, k);
}

static void test_override_pointing_at_unknown_name_is_rejected(void)
{
    Overrides ov = { { "restore", "bogus" } };
    Key k;
    TEST_ASSERT_FALSE(ps2keys::lookupKey("restore", k, &ov));
}

static void test_override_does_not_shadow_unrelated_names(void)
{
    Overrides ov = { { "restore", "pageup" } };
    Key k;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("enter", k, &ov));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_RETURN, k);
}

static void test_keyname_round_trips(void)
{
    const char *n = ps2keys::keyName(ps2dev::scancodes::K_RETURN);
    TEST_ASSERT_NOT_NULL(n);
    Key k;
    TEST_ASSERT_TRUE(ps2keys::lookupKey(n, k));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_RETURN, k);
}

static void test_allnames_is_not_empty_and_every_name_resolves(void)
{
    std::vector<const char *> names;
    ps2keys::allNames(names);
    TEST_ASSERT_TRUE(names.size() > 40);
    for (const char *n : names)
    {
        Key k;
        TEST_ASSERT_TRUE_MESSAGE(ps2keys::lookupKey(n, k), n);
    }
}

// ---------------------------------------------------------------- combos --

static void test_parse_combo_three_keys(void)
{
    std::vector<Key> keys;
    TEST_ASSERT_TRUE(ps2keys::parseCombo("ctrl+alt+del", keys));
    TEST_ASSERT_EQUAL(3, keys.size());
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_LCTRL, keys[0]);
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_LALT, keys[1]);
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_DELETE, keys[2]);
}

static void test_parse_combo_single_key(void)
{
    std::vector<Key> keys;
    TEST_ASSERT_TRUE(ps2keys::parseCombo("f5", keys));
    TEST_ASSERT_EQUAL(1, keys.size());
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_F5, keys[0]);
}

static void test_parse_combo_rejects_unknown_member(void)
{
    std::vector<Key> keys;
    TEST_ASSERT_FALSE(ps2keys::parseCombo("ctrl+nosuchkey", keys));
}

static void test_parse_combo_rejects_empty_member(void)
{
    std::vector<Key> keys;
    TEST_ASSERT_FALSE(ps2keys::parseCombo("ctrl+", keys));
    TEST_ASSERT_FALSE(ps2keys::parseCombo("+del", keys));
    TEST_ASSERT_FALSE(ps2keys::parseCombo("", keys));
}

// --------------------------------------------------------- ascii encoding --

static std::string hexOf(const std::string &s)
{
    static const char *digits = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) { out += digits[c >> 4]; out += digits[c & 0x0F]; }
    return out;
}

// The whole point of the sibling function: plain text must NOT be PETSCII'd.
static void test_ascii_text_passes_through_unchanged(void)
{
    TEST_ASSERT_EQUAL_STRING("dir", encodeAsciiCommand("dir").c_str());
    TEST_ASSERT_EQUAL_STRING("LOAD", encodeAsciiCommand("LOAD").c_str());
}

static void test_ascii_and_dos_encodings_differ(void)
{
    TEST_ASSERT_TRUE(encodeAsciiCommand("dir") != encodeDosCommand("dir"));
}

static void test_ascii_hex_escape_becomes_a_raw_byte(void)
{
    TEST_ASSERT_EQUAL_STRING("6469720D", hexOf(encodeAsciiCommand("dir0x0D")).c_str());
}

static void test_ascii_hex_escape_is_a_run(void)
{
    // One "0x" introduces a RUN, so these two spellings are identical.
    TEST_ASSERT_EQUAL_STRING(hexOf(encodeAsciiCommand("0x0D0A")).c_str(),
                             hexOf(encodeAsciiCommand("0x0D0x0A")).c_str());
    TEST_ASSERT_EQUAL_STRING("0D0A", hexOf(encodeAsciiCommand("0x0D0A")).c_str());
}

static void test_ascii_trailing_odd_hex_digit_stays_text(void)
{
    // "0x0D0" -- the run takes 0D, the lone '0' is not half a byte.
    TEST_ASSERT_EQUAL_STRING("0D30", hexOf(encodeAsciiCommand("0x0D0")).c_str());
}

// Regression guard: the shared-escape refactor must not change PETSCII output.
static void test_dos_encoding_still_petscii(void)
{
    TEST_ASSERT_EQUAL_STRING("4449520D", hexOf(encodeDosCommand("dir0x0D")).c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_lookup_exact_name);
    RUN_TEST(test_lookup_is_case_insensitive);
    RUN_TEST(test_lookup_aliases_agree);
    RUN_TEST(test_bare_modifier_resolves_to_left_hand_key);
    RUN_TEST(test_function_keys_span_f1_to_f12);
    RUN_TEST(test_unknown_name_is_rejected);
    RUN_TEST(test_single_letter_and_digit_names);
    RUN_TEST(test_override_rebinds_a_name);
    RUN_TEST(test_override_pointing_at_unknown_name_is_rejected);
    RUN_TEST(test_override_does_not_shadow_unrelated_names);
    RUN_TEST(test_keyname_round_trips);
    RUN_TEST(test_allnames_is_not_empty_and_every_name_resolves);
    RUN_TEST(test_parse_combo_three_keys);
    RUN_TEST(test_parse_combo_single_key);
    RUN_TEST(test_parse_combo_rejects_unknown_member);
    RUN_TEST(test_parse_combo_rejects_empty_member);
    RUN_TEST(test_ascii_text_passes_through_unchanged);
    RUN_TEST(test_ascii_and_dos_encodings_differ);
    RUN_TEST(test_ascii_hex_escape_becomes_a_raw_byte);
    RUN_TEST(test_ascii_hex_escape_is_a_run);
    RUN_TEST(test_ascii_trailing_odd_hex_digit_stays_text);
    RUN_TEST(test_dos_encoding_still_petscii);
    return UNITY_END();
}
```

Create `test/native/test_ps2_keys/engine_sources.cpp`:

```cpp
// Pulls in the exact translation units the PS/2 key tests need, by
// #include-ing the real .cpp files by relative path. Same approach as
// test/native/test_console_dos/engine_sources.cpp -- see that file for why
// PlatformIO's library dependency finder can't be used here.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef, and
// this file concatenates several .cpp files into ONE translation unit.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"

// The units under test.
#include "../../../lib/console/dos_encode.cpp"
#include "../../../lib/device/ps2/ps2_keynames.cpp"

// util_debug_printf() is the non-ESP backend for the Debug_print*() macros
// (include/debug.h's !ESP_PLATFORM branch); string_utils.cpp calls it. The real
// one lives in lib/utils/utils.cpp, which transitively pulls in the SAM speech
// synthesizer. This suite needs none of that.
#include <cstdarg>
#include <cstdio>
void util_debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
pio test -e native -f native/test_ps2_keys 2>&1 | tail -30
```

Expected: FAIL at compile — `lib/device/ps2/ps2_keynames.h: No such file or
directory` and `encodeAsciiCommand is not a member of ESP32Console`.

- [ ] **Step 3: Add `encodeAsciiCommand`, sharing the escape walk**

In `lib/console/dos_encode.h`, add after `encodeDosCommand`:

```cpp
    // Same 0xNN run-escape as encodeDosCommand, but text passes through as raw
    // ASCII with NO PETSCII conversion.  For destinations that are not the IEC
    // bus -- specifically the PS/2 keyboard, which talks to a keyboard
    // controller and would receive garbage from the PETSCII map.
    std::string encodeAsciiCommand(const std::string &line);
```

In `lib/console/dos_encode.cpp`, replace the body of `encodeDosCommand` with a
shared walk parameterised by the text transform. The escape logic is byte-for-
byte what was there; only the flush differs.

```cpp
namespace
{
    // The 0xNN run-escape walk, shared by both encoders. `xform` is applied to
    // each run of pending TEXT; hex escapes always pass through verbatim.
    std::string encodeEscapes(const std::string &line,
                              std::string (*xform)(const std::string &))
    {
        std::string out;
        std::string text;

        auto flushText = [&out, &text, xform]()
        {
            if (text.empty())
                return;
            out += xform(text);
            text.clear();
        };

        for (size_t i = 0; i < line.size(); )
        {
            if (i + 4 <= line.size() && line[i] == '0' &&
                (line[i + 1] == 'x' || line[i + 1] == 'X') &&
                isxdigit(static_cast<unsigned char>(line[i + 2])) &&
                isxdigit(static_cast<unsigned char>(line[i + 3])))
            {
                flushText();
                size_t j = i + 2;
                while (j + 2 <= line.size() &&
                       isxdigit(static_cast<unsigned char>(line[j])) &&
                       isxdigit(static_cast<unsigned char>(line[j + 1])))
                {
                    char hex[3] = { line[j], line[j + 1], '\0' };
                    out += static_cast<char>(strtol(hex, nullptr, 16));
                    j += 2;
                }
                i = j;
            }
            else
            {
                text += line[i++];
            }
        }
        flushText();
        return out;
    }

    std::string xformPetscii(const std::string &s) { return mstr::toPETSCII2(s); }
    std::string xformNone(const std::string &s)    { return s; }
}

namespace ESP32Console
{
    std::string encodeDosCommand(const std::string &line)
    {
        return encodeEscapes(line, xformPetscii);
    }

    std::string encodeAsciiCommand(const std::string &line)
    {
        return encodeEscapes(line, xformNone);
    }
}
```

- [ ] **Step 4: Write `ps2_keynames.h`**

Create `lib/device/ps2/ps2_keynames.h`:

```cpp
// PS/2 key-name lookup.
//
// Deliberately depends on nothing but <string>, <vector>, <map> and
// scan_codes_set_2.h (which includes only <stdint.h>), so it compiles in the
// native test environment where lib/device is otherwise absent.
#ifndef PS2_KEYNAMES_H
#define PS2_KEYNAMES_H

#include <map>
#include <string>
#include <vector>

#include "scan_codes_set_2.h"

namespace ps2keys
{
    using Key = ps2dev::scancodes::Key;

    // Maps a key NAME onto another key NAME.  The DTV's scancode-to-C64-key
    // table is a bench finding, so C64 names (runstop, restore, commodore...)
    // are bound at runtime from devices.ps2.keymap rather than guessed here.
    using Overrides = std::map<std::string, std::string>;

    // Case-insensitive.  Returns false for an unrecognised name, for an empty
    // name, and for an override that points at a name we do not know.
    bool lookupKey(const std::string &name, Key &out, const Overrides *ov = nullptr);

    // "ctrl+alt+del" -> {K_LCTRL, K_LALT, K_DELETE}, in the order written.
    // False if any member is unknown or empty.
    bool parseCombo(const std::string &spec, std::vector<Key> &out,
                    const Overrides *ov = nullptr);

    // Canonical name for a key, or nullptr if it has none.
    const char *keyName(Key k);

    // Every canonical name, for `ps2 keys`.  Aliases are not listed.
    void allNames(std::vector<const char *> &out);
}

#endif // PS2_KEYNAMES_H
```

- [ ] **Step 5: Write `ps2_keynames.cpp`**

Create `lib/device/ps2/ps2_keynames.cpp`. Note the vendored enumerator
spellings: `K_SCROLLOCK` (one L), `K_PRINT`, `K_LSUPER` / `K_RSUPER`.

```cpp
#include "ps2_keynames.h"

#include <cctype>

namespace
{
    using ps2keys::Key;
    namespace sc = ps2dev::scancodes;

    struct Entry { const char *name; Key key; bool canonical; };

    // Canonical entries are listed by `ps2 keys`; aliases resolve but are not
    // listed.  Linear search -- ~90 entries is nothing, and this keeps the
    // whole table in flash with no heap and no static constructor.
    const Entry TABLE[] = {
        {"a",sc::K_A,true},{"b",sc::K_B,true},{"c",sc::K_C,true},{"d",sc::K_D,true},
        {"e",sc::K_E,true},{"f",sc::K_F,true},{"g",sc::K_G,true},{"h",sc::K_H,true},
        {"i",sc::K_I,true},{"j",sc::K_J,true},{"k",sc::K_K,true},{"l",sc::K_L,true},
        {"m",sc::K_M,true},{"n",sc::K_N,true},{"o",sc::K_O,true},{"p",sc::K_P,true},
        {"q",sc::K_Q,true},{"r",sc::K_R,true},{"s",sc::K_S,true},{"t",sc::K_T,true},
        {"u",sc::K_U,true},{"v",sc::K_V,true},{"w",sc::K_W,true},{"x",sc::K_X,true},
        {"y",sc::K_Y,true},{"z",sc::K_Z,true},
        {"0",sc::K_0,true},{"1",sc::K_1,true},{"2",sc::K_2,true},{"3",sc::K_3,true},
        {"4",sc::K_4,true},{"5",sc::K_5,true},{"6",sc::K_6,true},{"7",sc::K_7,true},
        {"8",sc::K_8,true},{"9",sc::K_9,true},

        {"enter",sc::K_RETURN,true},      {"return",sc::K_RETURN,false},
        {"escape",sc::K_ESCAPE,true},     {"esc",sc::K_ESCAPE,false},
        {"tab",sc::K_TAB,true},
        {"backspace",sc::K_BACKSPACE,true},
        {"space",sc::K_SPACE,true},
        {"insert",sc::K_INSERT,true},     {"ins",sc::K_INSERT,false},
        {"delete",sc::K_DELETE,true},     {"del",sc::K_DELETE,false},
        {"home",sc::K_HOME,true},
        {"end",sc::K_END,true},
        {"pageup",sc::K_PAGEUP,true},     {"pgup",sc::K_PAGEUP,false},
        {"pagedown",sc::K_PAGEDOWN,true}, {"pgdn",sc::K_PAGEDOWN,false},
        {"up",sc::K_UP,true},{"down",sc::K_DOWN,true},
        {"left",sc::K_LEFT,true},{"right",sc::K_RIGHT,true},

        {"f1",sc::K_F1,true},{"f2",sc::K_F2,true},{"f3",sc::K_F3,true},
        {"f4",sc::K_F4,true},{"f5",sc::K_F5,true},{"f6",sc::K_F6,true},
        {"f7",sc::K_F7,true},{"f8",sc::K_F8,true},{"f9",sc::K_F9,true},
        {"f10",sc::K_F10,true},{"f11",sc::K_F11,true},{"f12",sc::K_F12,true},

        // A bare modifier means the LEFT-hand key, which is what a host that
        // does not distinguish sides expects.
        {"ctrl",sc::K_LCTRL,true},   {"lctrl",sc::K_LCTRL,false},
        {"rctrl",sc::K_RCTRL,true},
        {"shift",sc::K_LSHIFT,true}, {"lshift",sc::K_LSHIFT,false},
        {"rshift",sc::K_RSHIFT,true},
        {"alt",sc::K_LALT,true},     {"lalt",sc::K_LALT,false},
        {"ralt",sc::K_RALT,true},
        {"gui",sc::K_LSUPER,true},   {"win",sc::K_LSUPER,false},
        {"lgui",sc::K_LSUPER,false}, {"rgui",sc::K_RSUPER,true},
        {"menu",sc::K_MENU,true},

        {"capslock",sc::K_CAPSLOCK,true},
        {"numlock",sc::K_NUMLOCK,true},
        {"scrolllock",sc::K_SCROLLOCK,true},
        {"printscreen",sc::K_PRINT,true}, {"print",sc::K_PRINT,false},
        {"pause",sc::K_PAUSE,true},

        {"backquote",sc::K_BACKQUOTE,true},
        {"minus",sc::K_MINUS,true},{"equals",sc::K_EQUALS,true},
        {"backslash",sc::K_BACKSLASH,true},
        {"leftbracket",sc::K_LEFTBRACKET,true},
        {"rightbracket",sc::K_RIGHTBRACKET,true},
        {"semicolon",sc::K_SEMICOLON,true},{"quote",sc::K_QUOTE,true},
        {"comma",sc::K_COMMA,true},{"period",sc::K_PERIOD,true},
        {"slash",sc::K_SLASH,true},
    };
    const size_t TABLE_LEN = sizeof(TABLE) / sizeof(TABLE[0]);

    std::string lower(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
            out += static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return out;
    }

    bool findInTable(const std::string &lowered, Key &out)
    {
        for (size_t i = 0; i < TABLE_LEN; i++)
            if (lowered == TABLE[i].name) { out = TABLE[i].key; return true; }
        return false;
    }
}

namespace ps2keys
{
    bool lookupKey(const std::string &name, Key &out, const Overrides *ov)
    {
        if (name.empty())
            return false;

        std::string n = lower(name);

        // One level of indirection only -- an override names a real key, not
        // another override, so a cycle is impossible by construction.
        if (ov)
        {
            Overrides::const_iterator it = ov->find(n);
            if (it != ov->end())
                return findInTable(lower(it->second), out);
        }

        return findInTable(n, out);
    }

    bool parseCombo(const std::string &spec, std::vector<Key> &out, const Overrides *ov)
    {
        out.clear();
        if (spec.empty())
            return false;

        size_t start = 0;
        while (true)
        {
            size_t plus = spec.find('+', start);
            std::string part = (plus == std::string::npos)
                                 ? spec.substr(start)
                                 : spec.substr(start, plus - start);
            Key k;
            if (!lookupKey(part, k, ov)) { out.clear(); return false; }
            out.push_back(k);

            if (plus == std::string::npos)
                break;
            start = plus + 1;
        }
        return true;
    }

    const char *keyName(Key k)
    {
        for (size_t i = 0; i < TABLE_LEN; i++)
            if (TABLE[i].key == k && TABLE[i].canonical)
                return TABLE[i].name;
        return nullptr;
    }

    void allNames(std::vector<const char *> &out)
    {
        out.clear();
        for (size_t i = 0; i < TABLE_LEN; i++)
            if (TABLE[i].canonical)
                out.push_back(TABLE[i].name);
    }
}
```

- [ ] **Step 6: Add the native include path and the firmware source glob**

In `platformio.ini.sample`, under `[env:native]` `build_flags`, add after the
`-I components/zlib/zlib` line:

```
    ; test_ps2_keys: scan_codes_set_2.h (stdint.h only, native-safe).
    -I components/ps2/src
```

In `src/CMakeLists.txt`, add to `INCLUDES` after
`${CMAKE_SOURCE_DIR}/lib/device`:

```cmake
    ${CMAKE_SOURCE_DIR}/lib/device/ps2
```

and to the `FILE(GLOB_RECURSE SOURCES ...)` list after the
`lib/device/iec/*.cpp` line:

```cmake
    ${CMAKE_SOURCE_DIR}/lib/device/ps2/*.cpp
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
pio test -e native -f native/test_ps2_keys 2>&1 | tail -30
```

Expected: PASS, 22 cases.

- [ ] **Step 8: Verify the refactor did not change DOS encoding**

```bash
pio test -e native -f native/test_console_dos 2>&1 | tail -15
```

Expected: PASS, 21 cases — the same count as before this task.

- [ ] **Step 9: Commit**

```bash
git add lib/console/dos_encode.h lib/console/dos_encode.cpp \
        lib/device/ps2/ps2_keynames.h lib/device/ps2/ps2_keynames.cpp \
        test/native/test_ps2_keys/ src/CMakeLists.txt platformio.ini.sample
git commit -m "ps2: key-name lookup and ASCII escape encoding

encodeAsciiCommand shares encodeDosCommand's 0xNN run-escape walk but
skips the PETSCII transform -- PS/2 talks to a keyboard controller, not
the IEC bus. Key names resolve through an override map so the DTV's
scancode mapping can be bound from config once known on the bench.

Native suite: 22 cases."
```

---

### Task 3: Delete the rival implementation and trim the component's dependencies

P2 and P3. Ordering inside this task matters: the includes must go **before**
`REQUIRES` is trimmed, or the build fails.

**Files:**
- Delete: `components/ps2/src/ps2keyboard.h`
- Delete: `components/ps2/src/ps2keyboard.cpp`
- Modify: `components/ps2/src/ps2_device.h`
- Modify: `components/ps2/src/ps2_keyboard.h`
- Modify: `components/ps2/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: a component that depends only on `driver`, `esp_timer`, `esp_rom`.
  `ps2dev::PS2Keyboard` remains the only keyboard class.

- [ ] **Step 1: Delete the rival files**

```bash
git rm components/ps2/src/ps2keyboard.h components/ps2/src/ps2keyboard.cpp
```

Three independent reasons: it is host-side (reads *from* a keyboard, the
opposite direction); it reuses the include guard `PS2_KEYBOARD_H` and so
collides with `ps2_keyboard.h`; and it calls `gpio_install_isr_service(0)` a
second time when `src/main.cpp:238` already installs it.

- [ ] **Step 2: Remove the unused includes — do this BEFORE touching CMakeLists**

In `components/ps2/src/ps2_device.h`, delete:

```cpp
#include <initializer_list>
#include <stack>
#include <nvs_flash.h>
#include <string>
#include <functional>
```

`<functional>` is used by `ps2_data_callback_t`. Keep it only if that member
survives — it is unreferenced by any Meatloaf code, so delete
`ps2_data_callback_t`, `_ps2_data_callback` and `set_ps2_data_callback()` along
with the include.

In `components/ps2/src/ps2_keyboard.h`, delete:

```cpp
#include <initializer_list>
#include <stack>
#include <nvs_flash.h>
#include <string>
```

`std::initializer_list` IS used — by
`void type(std::initializer_list<scancodes::Key> keys);`. Keep that one include.

- [ ] **Step 3: Trim `REQUIRES`**

In `components/ps2/CMakeLists.txt`, change:

```cmake
  REQUIRES esp_hid nvs_flash driver bt
```

to:

```cmake
  REQUIRES driver esp_timer esp_rom
```

`REQUIRES` is what supplies a component's include paths, which is why Step 2
had to come first. `esp_timer` and `esp_rom` are for Task 4's replacements.

- [ ] **Step 4: Verify the component still compiles**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS. `nvs_flash.h: No such file or directory` here means Step 2
was incomplete.

- [ ] **Step 5: Commit**

```bash
git add -A components/ps2
git commit -m "ps2: delete rival host-side impl, trim component deps

ps2keyboard.{h,cpp} read FROM a keyboard rather than emulating one,
collided on the PS2_KEYBOARD_H include guard, and installed the GPIO ISR
service that main.cpp already installs.

REQUIRES drops esp_hid/nvs_flash/bt -- no source used them. The matching
#includes had to go first, since REQUIRES is what supplies include paths."
```

---

### Task 4: Eliminate the Arduino shims (P1)

Replace them with the ESP-IDF calls they were imitating. ~75 sites.

**Files:**
- Modify: `components/ps2/src/ps2_device.h`
- Modify: `components/ps2/src/ps2_device.cpp`
- Modify: `components/ps2/src/ps2_keyboard.cpp`
- Modify: `components/ps2/src/ps2_mouse.cpp`

**Interfaces:**
- Consumes: the trimmed `REQUIRES` from Task 3 (`esp_timer`, `esp_rom`).
- Produces: no `ps2dev::delay`/`millis`/`micros`/`delayMicroseconds`/
  `xTaskCreateUniversal` symbols anywhere. `write_wait_idle(uint8_t, uint64_t
  timeout_micros)` keeps its signature.

`ps2_mouse.cpp` must keep compiling even though nothing references it — it is
in the component's source glob.

- [ ] **Step 1: Delete the shim block from `ps2_device.h`**

Remove everything between the two ALL-CAPS banner comments — the
`xTaskCreateUniversal`, `delay`, `millis`, `micros` and `delayMicroseconds`
definitions — plus these three macros above them:

```cpp
#define NOP() asm volatile("nop")
#define HIGH 0x1
#define LOW 0x0
```

Add in their place:

```cpp
#include "esp_rom_sys.h"   // esp_rom_delay_us
#include "esp_timer.h"     // esp_timer_get_time
```

- [ ] **Step 2: Replace the call sites**

Mechanical, but **read the tick warning below before running any sed.**

```bash
cd components/ps2/src
sed -i '' 's/\bdelayMicroseconds(/esp_rom_delay_us(/g' ps2_device.cpp ps2_keyboard.cpp ps2_mouse.cpp
sed -i '' 's/\bxTaskCreateUniversal(/xTaskCreatePinnedToCore(/g' ps2_keyboard.cpp ps2_mouse.cpp
sed -i '' 's/== HIGH/== 1/g; s/== LOW/== 0/g; s/, HIGH)/, 1)/g; s/, LOW)/, 0)/g' ps2_device.cpp
cd -
```

Then fix by hand, because a blind substitution is wrong for these:

**`delay(n)` — do NOT sed to `pdMS_TO_TICKS(n)`.** At
`CONFIG_FREERTOS_HZ=100`, `pdMS_TO_TICKS(9)` is `(9*100)/1000` = **0**, exactly
the bug being fixed. Per site:

- `ps2_keyboard.cpp` `begin()`: `delay(200)` -> `vTaskDelay(pdMS_TO_TICKS(200))`
  (20 ticks, correct).
- The four `while (write(...) != 0) delay(1);` loops in `reply_to_host` — leave
  them alone here; Task 8 (A8) rewrites them wholesale.
- `_taskfn_process_host_request`'s `delay(INTERVAL_CHECKING_HOST_SEND_REQUEST_MILLIS)`
  — leave it; Task 7 deletes the whole poll.
- `ps2_mouse.cpp`: any `delay(n)` with n >= 10 becomes
  `vTaskDelay(pdMS_TO_TICKS(n))`; any n < 10 becomes `vTaskDelay(1)`.

**`millis()` / `micros()`** — `ps2_device.cpp` only, 4 sites in `write_wait_idle`
and `read`. Replace with `esp_timer_get_time()` and change the holding variables
to `int64_t`:

```cpp
    int64_t start_time = esp_timer_get_time();
    while (get_bus_state() != BusState::IDLE)
    {
        if (esp_timer_get_time() - start_time > (int64_t)timeout_micros)
            return -1;
    }
```

and in `read()`:

```cpp
    int64_t waiting_since = esp_timer_get_time();
    while (get_bus_state() != BusState::HOST_REQUEST_TO_SEND)
    {
        if ((esp_timer_get_time() - waiting_since) > (int64_t)timeout_ms * 1000)
            return -1;
        esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    }
```

`int64_t` microseconds throughout removes the shim's 32-bit wraparound (~71.6
minutes) entirely.

**`xTaskCreatePinnedToCore`** takes the same seven arguments in the same order,
so the sed above is complete — but confirm each call site's last argument is a
valid core id (0 or 1) and not a negative "any core" value the shim tolerated.

- [ ] **Step 3: Verify no shim symbols survive**

```bash
grep -rn "delayMicroseconds\|xTaskCreateUniversal\|\bNOP()\|\bmillis()\|\bmicros()" components/ps2/src/
grep -rn "\bHIGH\b\|\bLOW\b" components/ps2/src/
```

Expected: both print nothing.

- [ ] **Step 4: Verify it compiles**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS.

- [ ] **Step 5: Verify the duplicate symbols are actually gone**

The component is still unreferenced, so a clean link does not prove this. Check
the archive directly:

```bash
find .pio/build/lolin-d32-pro -name libps2.a
xtensa-esp32-elf-nm $(find .pio/build/lolin-d32-pro -name libps2.a) 2>/dev/null \
  | grep -i "ps2dev.*\(delay\|millis\|micros\|TaskCreateUniversal\)" | grep " T "
```

Expected: no output. Before this task the same command printed the same
`ps2dev::delay` / `ps2dev::micros` symbols from **three** object files, which is
the duplicate-symbol link failure.

- [ ] **Step 6: Commit**

```bash
git add components/ps2/src
git commit -m "ps2: replace Arduino shims with ESP-IDF calls

Deleted rather than inlined, so the duplicate-symbol link failure stops
existing. esp_rom_delay_us is IRAM-resident, calibrated, and free of the
shim's 32-bit micros() wraparound; timeouts move to int64_t microseconds
from esp_timer_get_time().

delay() is NOT blanket-converted: pdMS_TO_TICKS(n<10) is 0 at
CONFIG_FREERTOS_HZ=100, which is the very bug A1 describes."
```

---

### Task 5: Cooperative teardown — `_running` and `end()` (P6, P4)

Both task loops become interruptible and both tasks exit only from outside their
critical sections. This is also where the send task stops busy-spinning, because
the `_running` check and the blocking receive are the same edit.

**Files:**
- Modify: `components/ps2/src/ps2_device.h`
- Modify: `components/ps2/src/ps2_device.cpp`
- Modify: `components/ps2/src/ps2_keyboard.h`
- Modify: `components/ps2/src/ps2_keyboard.cpp`

**Interfaces:**
- Consumes: Task 4's `vTaskDelay` / `esp_timer_get_time` conventions.
- Produces:
  - `void PS2Device::end();`
  - `bool PS2Device::running() const;`
  - `void PS2Device::clearTaskHandle(TaskHandle_t *slot);`
  - `TaskHandle_t PS2Device::hostRequestTask() const;`
  - protected `volatile bool _running`

- [ ] **Step 1: Declare the teardown surface**

In `components/ps2/src/ps2_device.h`, add to `PS2Device`'s public section:

```cpp
    void end();
    bool running() const { return _running; }
    TaskHandle_t hostRequestTask() const { return _task_process_host_request; }
    // A task calls this on ITSELF, after releasing the bus mutex, immediately
    // before vTaskDelete(NULL).  end() waits on these going NULL.
    void clearTaskHandle(TaskHandle_t *slot) { *slot = nullptr; }
    TaskHandle_t *sendTaskSlot()        { return &_task_send_packet; }
    TaskHandle_t *hostRequestTaskSlot() { return &_task_process_host_request; }
```

and to its protected section:

```cpp
    // Written ONLY by begin() (true, last statement) and end() (false, first
    // statement).  Nothing else touches it -- the surrounding code does
    // read-modify-write on its other state and would clobber a shared
    // sentinel.  Same discipline as IECBusHandler::m_enabled.
    volatile bool _running = false;
```

- [ ] **Step 2: Set `_running` in `begin()` and implement `end()`**

In `components/ps2/src/ps2_device.cpp`, make `_running = true;` the **last**
statement of `PS2Device::begin()`, and add:

```cpp
  void PS2Device::end()
  {
    if (!_running)
      return;
    _running = false;                  // both loops observe this at the top

    // The host-request task blocks on a notification with no timeout, so it
    // must be woken explicitly.  The send task's bounded queue receive wakes
    // on its own within 250 ms.
    if (_task_process_host_request)
      xTaskNotifyGive(_task_process_host_request);

    for (int i = 0; i < 100 && (_task_send_packet || _task_process_host_request); i++)
      vTaskDelay(pdMS_TO_TICKS(10));   // bounded, <= 1 s

    if (_task_send_packet || _task_process_host_request)
    {
      // Freeing a mutex a live task may still take is a guaranteed crash;
      // leaking ~440 bytes is survivable and leaves this line behind.
      ESP_LOGE("ps2", "tasks did not exit; leaking mutex/queue deliberately");
      return;
    }

    gpio_isr_handler_remove(_ps2clk);   // no-op until Task 7 adds the handler
    gohi(_ps2clk);
    gohi(_ps2data);
    gpio_reset_pin(_ps2clk);
    gpio_reset_pin(_ps2data);

    vQueueDelete(_queue_packet);
    _queue_packet = nullptr;
    vSemaphoreDelete(_mutex_bus);
    _mutex_bus = nullptr;
  }
```

Add `#include "esp_log.h"` to `ps2_device.cpp` if it is not already present.

- [ ] **Step 3: Make the send task interruptible and stop it spinning**

In `components/ps2/src/ps2_keyboard.cpp`, replace `_taskfn_send_packet` entirely:

```cpp
void _taskfn_send_packet(void *arg)
{
  PS2Device *ps2dev = (PS2Device *)arg;
  while (ps2dev->running())
  {
    PS2Packet packet;
    // Blocking receive.  The 250 ms bound is ONLY so end() can retire this
    // task without pushing a sentinel packet (which could find the queue
    // full).  It is not a poll of the bus -- nothing here polls the bus.
    if (xQueueReceive(ps2dev->get_packet_queue_handle(), &packet,
                      pdMS_TO_TICKS(250)) == pdTRUE)
    {
      xSemaphoreTake(ps2dev->get_bus_mutex_handle(), portMAX_DELAY);
      esp_rom_delay_us(BYTE_INTERVAL_MICROS);
      for (int i = 0; i < packet.len; i++)
      {
        ps2dev->write_wait_idle(packet.data[i]);
        esp_rom_delay_us(BYTE_INTERVAL_MICROS);
      }
      xSemaphoreGive(ps2dev->get_bus_mutex_handle());
    }
  }
  // Reached only outside the critical section, so the mutex is never orphaned.
  ps2dev->clearTaskHandle(ps2dev->sendTaskSlot());
  vTaskDelete(NULL);
}
```

The old body used `xQueueReceive(..., 0)` plus `portYIELD()`, so the task never
blocked — at priority 9 on core 0 that starves `httpd` (priority 5), the
console and SessionBroker.

- [ ] **Step 4: Make the host-request task interruptible**

Still in `ps2_keyboard.cpp`, change `_taskfn_process_host_request`'s
`while (true)` to `while (ps2dev->running())` and give it the same tail:

```cpp
  ps2dev->clearTaskHandle(ps2dev->hostRequestTaskSlot());
  vTaskDelete(NULL);
```

Leave its body alone — Task 7 replaces the poll with the ISR.

- [ ] **Step 5: Declare `end()` on the keyboard**

In `components/ps2/src/ps2_keyboard.h`, add `void end();` beside `void begin();`.
In `ps2_keyboard.cpp`:

```cpp
void PS2Keyboard::end()
{
  PS2Device::end();
}
```

It exists so callers have a symmetrical `begin()`/`end()` pair on the class they
actually hold, and so a future keyboard-specific teardown has a home.

- [ ] **Step 6: Verify it compiles**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add components/ps2/src
git commit -m "ps2: cooperative teardown and a non-spinning send task

Both task loops now run while(running()) and self-retire only after
releasing the bus mutex, so end() can free the mutex and queue without
orphaning either. end() waits up to 1s and refuses to free anything if a
task has not exited.

The send task's zero-timeout receive plus portYIELD() meant it never
blocked -- priority 9 on core 0, which starves httpd at priority 5."
```

---

### Task 6: Send-path backpressure and failure propagation (P5, A7)

**Files:**
- Modify: `components/ps2/src/ps2_device.cpp:271`
- Modify: `components/ps2/src/ps2_keyboard.cpp`
- Modify: `components/ps2/src/ps2_keyboard.h`

**Interfaces:**
- Consumes: `PS2Device::send_packet` from Task 5's task loop.
- Produces: `int PS2Keyboard::keydown(Key)`, `int PS2Keyboard::keyup(Key)`,
  `int PS2Keyboard::type(const char *)` — all returning 0 on success and -1 if
  any packet could not be queued. Callers in Task 9 rely on this.

- [ ] **Step 1: Give `send_packet` a timeout and a null guard**

In `components/ps2/src/ps2_device.cpp`, replace line 271:

```cpp
  int PS2Device::send_packet(PS2Packet *packet)
  {
    // A send racing a teardown must fail, not write to a deleted handle.
    if (!_queue_packet)
      return -1;
    // 500 ms comfortably exceeds the ~20 ms a full 20-deep queue takes to
    // drain, so this only expires if the wire is genuinely stuck.  With a 0
    // timeout, `type()` of anything past ~10 characters silently dropped
    // keystrokes: each character is two packets and the wire drains at
    // roughly 1 ms per packet.
    return (xQueueSend(_queue_packet, packet, pdMS_TO_TICKS(500)) == pdTRUE) ? 0 : -1;
  }
```

- [ ] **Step 2: Propagate the result out of `keydown` / `keyup`**

In `components/ps2/src/ps2_keyboard.h`, change the three declarations:

```cpp
    int keydown(scancodes::Key key);
    int keyup(scancodes::Key key);
    int type(const char *str);
```

`void type(scancodes::Key)` and
`void type(std::initializer_list<scancodes::Key>)` keep their signatures — Task
9 does not use them.

In `ps2_keyboard.cpp`, change both bodies to return, keeping the
data-reporting guard:

```cpp
int PS2Keyboard::keydown(scancodes::Key key)
{
  if (!_data_reporting_enabled)
    return 0;                 // host asked us to be quiet; not a failure
  PS2Packet packet;
  packet.len = scancodes::MAKE_CODES_LEN[key];
  for (uint8_t i = 0; i < packet.len; i++)
    packet.data[i] = scancodes::MAKE_CODES[key][i];
  return send_packet(&packet);
}
```

and the same shape for `keyup` using `BREAK_CODES_LEN` / `BREAK_CODES`.

- [ ] **Step 3: Propagate out of `type(const char *)`**

Change its return type to `int`, and make each keydown/keyup site record
failure. Keep going after a failure so a partly-typed string still releases
whatever it pressed:

```cpp
int PS2Keyboard::type(const char *str)
{
  int result = 0;
  size_t i = 0;
  while (str[i] != '\0')
  {
      // ... existing per-character key/shift selection, unchanged ...

      // at each existing keydown/keyup call, replace `keydown(k);` with:
      //     if (keydown(k) != 0) result = -1;
      // and likewise for keyup.
  }
  return result;
}
```

- [ ] **Step 4: Verify it compiles**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS. A `void` value not ignored error means a `type()` overload
was missed.

- [ ] **Step 5: Commit**

```bash
git add components/ps2/src
git commit -m "ps2: backpressure on the packet queue, propagate send failures

xQueueSend with a 0 timeout against a 20-deep queue silently dropped
keystrokes past ~10 characters, and keydown/keyup/type all discarded the
result. Now a bounded send blocks until the wire catches up, and failure
reaches the caller."
```

---

### Task 7: Interrupt-driven host request detection (P7, A5)

Removes bus polling entirely. The host-request task blocks on a notification and
the ISR fires only when the host actually pulls DATA low.

**Files:**
- Modify: `components/ps2/src/ps2_device.h`
- Modify: `components/ps2/src/ps2_device.cpp`
- Modify: `components/ps2/src/ps2_keyboard.cpp`

**Interfaces:**
- Consumes: `hostRequestTask()`, `running()`, `clearTaskHandle()` from Task 5.
- Produces: `gpio_num_t PS2Device::clkPin() const`,
  `gpio_num_t PS2Device::dataPin() const`.

- [ ] **Step 1: Expose the pins to the ISR**

In `components/ps2/src/ps2_device.h`, add to the public section:

```cpp
    gpio_num_t clkPin() const  { return _ps2clk; }
    gpio_num_t dataPin() const { return _ps2data; }
```

- [ ] **Step 2: Replace the dead ISR stub with a real one**

In `components/ps2/src/ps2_device.cpp`, delete the existing commented-out
`ps2_isr_handler` and the commented-out `PS2Device::ps2_task`, and add:

```cpp
  // A host request to send is CLK high && DATA low.  The host's sequence is
  // CLK low (inhibit) -> DATA low (start bit) -> CLK released, so the edge
  // that CREATES the condition is CLK rising.
  static void IRAM_ATTR ps2_clk_isr(void *arg)
  {
    PS2Device *dev = static_cast<PS2Device *>(arg);
    if (gpio_get_level(dev->clkPin()) == 1 && gpio_get_level(dev->dataPin()) == 0)
    {
      BaseType_t hpw = pdFALSE;
      vTaskNotifyGiveFromISR(dev->hostRequestTask(), &hpw);
      if (hpw)
        portYIELD_FROM_ISR();
    }
  }
```

- [ ] **Step 3: Attach the handler in `begin()`**

In `PS2Device::begin()`, after the queue and mutex are created and **before**
`_running = true`:

```cpp
    // The service is installed once, globally, at src/main.cpp:238.  Calling
    // gpio_install_isr_service() here returns ESP_ERR_INVALID_STATE and, worse,
    // is what the deleted ps2keyboard.cpp used to do.
    gpio_set_intr_type(_ps2clk, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(_ps2clk, ps2_clk_isr, this);
    gpio_intr_disable(_ps2clk);   // enabled by the task once it is running
```

The handler must be attached before the task starts notifying, and the
interrupt starts disabled so nothing fires until the task exists.

- [ ] **Step 4: Suppress our own clock edges during transmission**

We drive CLK ourselves — 11 rising edges per byte — and DATA is low on every
`0` bit, so our own traffic would fire the ISR repeatedly and every hit would
look like a host request.

At the top of `PS2Device::write()`, immediately after the `get_bus_state()`
guard, and at the top of `PS2Device::read()`, immediately after its wait loop:

```cpp
    gpio_intr_disable(_ps2clk);
```

At every return point of both functions, and at the end of both:

```cpp
    gpio_intr_enable(_ps2clk);
```

Both already run under `_mutex_bus`, so this is serialized for free. Take care
that `write()`'s early `return -1` (bus not idle) does **not** need the enable,
because it returns before the disable.

- [ ] **Step 5: Replace the polling task body**

In `components/ps2/src/ps2_keyboard.cpp`, replace
`_taskfn_process_host_request` entirely:

```cpp
void _taskfn_process_host_request(void *arg)
{
  PS2Device *ps2dev = (PS2Device *)arg;
  gpio_intr_enable(ps2dev->clkPin());

  while (ps2dev->running())
  {
    // Blocks indefinitely.  NOTHING polls the PS/2 lines: the ISR fires only
    // when the host actually pulls DATA low, and end() wakes this task with
    // xTaskNotifyGive after clearing _running.
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0)
      continue;

    xSemaphoreTake(ps2dev->get_bus_mutex_handle(), portMAX_DELAY);
    if (ps2dev->get_bus_state() == PS2Device::BusState::HOST_REQUEST_TO_SEND)
    {
      uint8_t host_cmd;
      int r = ps2dev->read(&host_cmd);
      if (r == 0)
        ps2dev->reply_to_host(host_cmd);
      else if (r == -2)
        ps2dev->write(0xFE);      // A5: parity error -- ask the host to resend
    }
    xSemaphoreGive(ps2dev->get_bus_mutex_handle());
  }

  ps2dev->clearTaskHandle(ps2dev->hostRequestTaskSlot());
  vTaskDelete(NULL);
}
```

`read()` and `write()` handle the interrupt disable/enable themselves per Step 4.

Before this, `read()`'s `-2` return was discarded by an `if (... == 0)` test, so
a corrupted host command vanished silently instead of being resent.

- [ ] **Step 6: Delete the now-unused poll constant**

In `components/ps2/src/ps2_device.h`, remove:

```cpp
  const uint32_t INTERVAL_CHECKING_HOST_SEND_REQUEST_MILLIS = 9;
```

```bash
grep -rn "INTERVAL_CHECKING_HOST_SEND_REQUEST_MILLIS" components/ps2/src/
```

Expected: nothing. If `ps2_mouse.cpp` still uses it, keep the constant and note
that only the keyboard is interrupt-driven.

- [ ] **Step 7: Verify it compiles**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add components/ps2/src
git commit -m "ps2: interrupt-driven host request detection

CLK-rising ISR samples DATA and notifies the host-request task, which now
blocks on portMAX_DELAY. Nothing polls the PS/2 lines. The interrupt is
disabled while we drive the bus, since our own 11 rising edges per byte
would otherwise self-trigger on every 0 bit.

The service is NOT installed here -- main.cpp:238 already does it.

Parity errors now answer 0xFE (Resend) instead of being discarded."
```

---

### Task 8: Correctness audit fixes (A2, A3, A4, A6, A8)

**Files:**
- Modify: `components/ps2/src/ps2_device.cpp`
- Modify: `components/ps2/src/ps2_keyboard.cpp`

**Interfaces:**
- Consumes: Task 4's `esp_rom_delay_us` / `esp_timer_get_time`.
- Produces: no signature changes.

- [ ] **Step 1: A2 — move the spinlock to file scope**

`write()` and `read()` each declare `portMUX_TYPE mux =
portMUX_INITIALIZER_UNLOCKED;` as a **stack local**. A spinlock in a stack frame
excludes nothing between tasks or cores — every call locks its own private copy,
and what actually protects the byte is the incidental interrupt-disable. The
code works by accident rather than by contract.

In `components/ps2/src/ps2_device.cpp`, near the top of `namespace ps2dev`:

```cpp
  static portMUX_TYPE ps2_mux = portMUX_INITIALIZER_UNLOCKED;
```

Delete both local declarations and change every `taskENTER_CRITICAL(&mux)` /
`taskEXIT_CRITICAL(&mux)` to use `&ps2_mux`.

- [ ] **Step 2: A3 — critical sections per bit, not per byte**

`write()` currently holds interrupts off for ~900 us and `read()` for ~1.2 ms,
on the core that runs WiFi and lwIP. That is unnecessary: in device-to-host
transfers **the device owns the clock**, so being preempted between bits merely
stretches the clock, which hosts tolerate by design.

In `write()`, delete the single `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` pair
that wraps the whole function, and wrap each of the eleven bit phases instead —
the start bit, each of the 8 data bits, the parity bit, and the stop bit. Each
becomes:

```cpp
      taskENTER_CRITICAL(&ps2_mux);
      // ... set data line, quarter delay, clk low, half delay, clk high,
      //     quarter delay -- the existing body of one bit, unchanged ...
      taskEXIT_CRITICAL(&ps2_mux);
```

Do the same in `read()` for its per-bit loop body and for the trailing
ack-bit sequence.

- [ ] **Step 3: A4 — abort a transmission the host has inhibited**

`write()` checks `get_bus_state() != BusState::IDLE` once on entry and never
again, so a host pulling CLK low mid-byte loses the byte silently.

Inside the 8-bit loop, at the top of each iteration and still outside that bit's
critical section:

```cpp
      if (gpio_get_level(_ps2clk) == 0)   // host inhibited mid-byte
      {
        gohi(_ps2data);
        gpio_intr_enable(_ps2clk);
        return -1;                        // caller may retry
      }
```

Release DATA before returning so the bus is not left held.

- [ ] **Step 4: A6 — stop `write_wait_idle` tight-spinning**

It spins for up to 1500 us with no yield. Using Task 4's rewritten body:

```cpp
  int PS2Device::write_wait_idle(uint8_t data, uint64_t timeout_micros)
  {
    int64_t start_time = esp_timer_get_time();
    while (get_bus_state() != BusState::IDLE)
    {
      if (esp_timer_get_time() - start_time > (int64_t)timeout_micros)
        return -1;
      esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    }
    return write(data);
  }
```

- [ ] **Step 5: A8 — bound the four infinite retry loops**

In `components/ps2/src/ps2_keyboard.cpp`, `reply_to_host` has four
`while (write(...) != 0) delay(1);` loops — in `RESET`, `GET_DEVICE_ID` (x2) and
`SET_RESET_LEDS` (x2). `write()` returns `-1` whenever the bus is not IDLE, and
`delay(1)` is `vTaskDelay(0)`, so a host holding CLK low spins **forever at
priority 10 with `_mutex_bus` held** — and `end()` can then never complete.

Add a file-scope helper and replace all four:

```cpp
// Retry a byte for a bounded time.  vTaskDelay(1) -- one tick, 10 ms -- is
// written literally: pdMS_TO_TICKS(1) is 0 at CONFIG_FREERTOS_HZ=100, which is
// exactly the no-op this replaces.
static int write_retry(PS2Device *dev, uint8_t value, int attempts = 20)
{
  for (int i = 0; i < attempts; i++)
  {
    if (dev->write(value) == 0)
      return 0;
    vTaskDelay(1);
  }
  return -1;
}
```

Each site becomes e.g.:

```cpp
    if (write_retry(this, (uint8_t)Command::BAT_SUCCESS) != 0)
      return -1;
```

20 attempts x 10 ms is a 200 ms ceiling per byte.

- [ ] **Step 6: Verify it compiles**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS.

- [ ] **Step 7: Verify no unbounded loops or stack spinlocks remain**

```bash
grep -n "while (write" components/ps2/src/ps2_keyboard.cpp
grep -n "portMUX_TYPE mux" components/ps2/src/ps2_device.cpp
```

Expected: both print nothing.

- [ ] **Step 8: Commit**

```bash
git add components/ps2/src
git commit -m "ps2: correctness audit fixes

A2 spinlock moved off the stack to file scope -- a portMUX in a stack
frame excludes nothing. A3 critical sections per bit not per byte: ~900us
with interrupts off on the WiFi core is unnecessary since the device owns
the clock and hosts tolerate stretching. A4 abort when the host inhibits
mid-byte instead of clocking into the void. A6 yield in write_wait_idle.
A8 bound the four reply_to_host retry loops that spun forever at priority
10 holding the bus mutex."
```

---

### Task 9: The `PS2KeyboardDevice` class and boot wiring

**Files:**
- Create: `lib/device/ps2/ps2.h`
- Create: `lib/device/ps2/ps2.cpp`
- Modify: `src/main.cpp:342`
- Modify: `lib/device/iec/meatloaf.h`

**Interfaces:**
- Consumes: `ps2dev::PS2Keyboard` with `begin()`, `end()`, `int keydown(Key)`,
  `int keyup(Key)`, `int type(const char *)` (Tasks 5-6); `ps2keys::lookupKey`,
  `ps2keys::parseCombo`, `ps2keys::Overrides` (Task 2).
- Produces: `extern PS2KeyboardDevice ps2Keyboard;` with the public API listed
  in Step 1. Task 10's console command uses exactly those names, and every one
  of them is called by Task 10 — there is no unused surface.

- [ ] **Step 1: Write `lib/device/ps2/ps2.h`**

```cpp
#ifndef DEVICE_PS2_H
#define DEVICE_PS2_H

#include <string>
#include <vector>

#include "ps2_keynames.h"

#ifdef PIN_KB_CLK

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ps2_keyboard.h"

class PS2KeyboardDevice
{
public:
    PS2KeyboardDevice();

    void start();                 // boot: reads config only, allocates nothing
    bool isEnabled() const { return _enabled; }
    bool isRunning() const { return _started; }

    bool startDevice();           // lazy begin(); re-announces BAT if already up
    bool enable();
    bool disable();               // releaseAll() -> end() -> persist
    void persistConfig();
    void reloadConfig();

    bool typeText(const std::string &text);
    bool holdKey(ps2keys::Key k);
    bool releaseKey(ps2keys::Key k);
    // No single-key pressKey(): `ps2 key <name>` goes through parseCombo,
    // which yields a one-element vector, so pressCombo covers both.
    bool pressCombo(const std::vector<ps2keys::Key> &keys);
    void releaseAll();

    const std::vector<ps2keys::Key> &heldKeys() const { return _held; }
    const ps2keys::Overrides &overrides() const { return _overrides; }
    bool dataReportingEnabled();
    bool capsLockOn();
    bool numLockOn();
    bool scrollLockOn();

private:
    bool ensureStarted();

    bool _enabled = false;
    bool _started = false;
    std::vector<ps2keys::Key> _held;
    ps2keys::Overrides _overrides;
    SemaphoreHandle_t _api_mutex;
    ps2dev::PS2Keyboard _kb;
};

#else   // no PS/2 hardware on this board

class PS2KeyboardDevice
{
public:
    void start() {}
    bool isEnabled() const { return false; }
    bool isRunning() const { return false; }
    bool startDevice() { return false; }
    bool enable() { return false; }
    bool disable() { return false; }
    void persistConfig() {}
    void reloadConfig() {}
    bool typeText(const std::string &) { return false; }
    bool holdKey(ps2keys::Key) { return false; }
    bool releaseKey(ps2keys::Key) { return false; }
    bool pressCombo(const std::vector<ps2keys::Key> &) { return false; }
    void releaseAll() {}
    const std::vector<ps2keys::Key> &heldKeys() const { static std::vector<ps2keys::Key> e; return e; }
    const ps2keys::Overrides &overrides() const { static ps2keys::Overrides e; return e; }
    bool dataReportingEnabled() { return false; }
    bool capsLockOn() { return false; }
    bool numLockOn() { return false; }
    bool scrollLockOn() { return false; }
};

#endif // PIN_KB_CLK

extern PS2KeyboardDevice ps2Keyboard;

#endif // DEVICE_PS2_H
```

- [ ] **Step 2: Write `lib/device/ps2/ps2.cpp`**

```cpp
#include "ps2.h"

#include "mlConfig.h"
#include "../../../include/debug.h"

PS2KeyboardDevice ps2Keyboard;

#ifdef PIN_KB_CLK

PS2KeyboardDevice::PS2KeyboardDevice()
    : _api_mutex(xSemaphoreCreateMutex()), _kb(PIN_KB_CLK, PIN_KB_DATA)
{
}

// Boot: read config and nothing else.  Allocating here would defeat the point
// of the lazy start.
void PS2KeyboardDevice::start()
{
    reloadConfig();
}

void PS2KeyboardDevice::reloadConfig()
{
    const psram_json &devices = mlConfig["devices"];
    if (!devices.contains("ps2"))
        return;

    const psram_json &ps2 = devices["ps2"];
    bool want = ps2.value("enabled", 0) != 0;

    _overrides.clear();
    if (ps2.contains("keymap"))
        for (auto it = ps2["keymap"].begin(); it != ps2["keymap"].end(); ++it)
            if (it.value().is_string())
                _overrides[it.key()] = it.value().get<std::string>();

    if (!want && _started)
        disable();
    _enabled = want;
}

void PS2KeyboardDevice::persistConfig()
{
    auto &entry = mlConfig.data()["devices"]["ps2"];
    entry["enabled"] = _enabled ? 1 : 0;
}

bool PS2KeyboardDevice::ensureStarted()
{
    if (!_enabled)
        return false;
    if (_started)
        return true;

    _kb.begin();          // two tasks, 200 ms settle, 0xAA BAT announcement
    _started = true;
    Debug_printv("ps2: device started on clk[%d] data[%d]", PIN_KB_CLK, PIN_KB_DATA);
    return true;
}

// `ps2 start` on an already-running device RE-ANNOUNCES rather than no-opping.
// A host that booted before we did generally never re-probes, so this is the
// best-effort hot-plug hook.
bool PS2KeyboardDevice::startDevice()
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok;
    if (_started)
    {
        ok = (_kb.write_wait_idle(0xAA) == 0);
        Debug_printv("ps2: re-announced BAT, ok[%d]", ok);
    }
    else
    {
        ok = ensureStarted();
    }
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::enable()
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    _enabled = true;
    persistConfig();
    xSemaphoreGive(_api_mutex);
    mlConfig.save();
    return true;
}

bool PS2KeyboardDevice::disable()
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    _enabled = false;          // no new sends may start
    if (_started)
    {
        releaseAll();          // never leave the host holding a modifier
        _kb.end();
        _started = false;
    }
    persistConfig();
    xSemaphoreGive(_api_mutex);
    mlConfig.save();
    return true;
}

bool PS2KeyboardDevice::typeText(const std::string &text)
{
    // ASCII only.  A non-ASCII character has no PS/2 scancode, and this path
    // must NEVER be PETSCII-encoded -- PS/2 goes to a keyboard controller.
    for (unsigned char c : text)
        if (c > 0x7F)
        {
            Debug_printv("ps2: non-ASCII byte [%02X] has no scancode", c);
            return false;
        }

    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted() && (_kb.type(text.c_str()) == 0);
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::holdKey(ps2keys::Key k)
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted() && (_kb.keydown(k) == 0);
    if (ok)
    {
        bool already = false;
        for (ps2keys::Key h : _held)
            if (h == k) already = true;
        if (!already)
            _held.push_back(k);
    }
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::releaseKey(ps2keys::Key k)
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted() && (_kb.keyup(k) == 0);
    for (size_t i = 0; i < _held.size(); i++)
        if (_held[i] == k) { _held.erase(_held.begin() + i); break; }
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::pressCombo(const std::vector<ps2keys::Key> &keys)
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted();
    if (ok)
    {
        for (size_t i = 0; i < keys.size(); i++)
            if (_kb.keydown(keys[i]) != 0) ok = false;
        // Released in reverse so modifiers outlive the key they modify.
        for (size_t i = keys.size(); i > 0; i--)
            if (_kb.keyup(keys[i - 1]) != 0) ok = false;
    }
    xSemaphoreGive(_api_mutex);
    return ok;
}

// Leaving Ctrl or Shift down modifies every keystroke typed on the HOST's own
// keyboard, so a dropped connection could wedge the target machine.
void PS2KeyboardDevice::releaseAll()
{
    if (!_started)
    {
        _held.clear();
        return;
    }
    for (size_t i = _held.size(); i > 0; i--)
        _kb.keyup(_held[i - 1]);
    _held.clear();
}

bool PS2KeyboardDevice::dataReportingEnabled() { return _kb.data_reporting_enabled(); }
bool PS2KeyboardDevice::capsLockOn()           { return _kb.is_caps_lock_led_on(); }
bool PS2KeyboardDevice::numLockOn()            { return _kb.is_num_lock_led_on(); }
bool PS2KeyboardDevice::scrollLockOn()         { return _kb.is_scroll_lock_led_on(); }

#endif // PIN_KB_CLK
```

Note `releaseAll()` is called from `disable()` while `_api_mutex` is already
held, so it must not take the mutex itself.

- [ ] **Step 3: Call `start()` at boot**

In `src/main.cpp`, add near the top with the other device includes:

```cpp
#include "ps2.h"
```

and immediately after `LEDS.start();` (line 342):

```cpp
    ps2Keyboard.start();   // reads devices.ps2 only; allocates nothing
```

Placement is safe anywhere after `mlConfig` is loaded, and it does no network or
GPIO work, so it has none of the ordering hazards `reloadConfig()` on a drive
has.

- [ ] **Step 4: Hook it into `reloadAllConfig()`**

In `lib/device/iec/meatloaf.h`, inside `iecMeatloaf::reloadAllConfig()`, beside
the existing `LEDS.reloadConfig()` call:

```cpp
    ps2Keyboard.reloadConfig();
```

Add `#include "ps2.h"` to that header if it is not already reachable. No
`#ifdef` is needed at the call site — the stub class has the same signature.

- [ ] **Step 5: Verify both branches of the guard compile**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS — this is the **stub** branch, since `lolin-d32-pro` has no
`PIN_KB_CLK`. Then build the pinned board identified in Task 1 Step 1 to compile
the real branch. If no pinned board builds, record that the real branch is
compile-unverified and say so in the commit body.

- [ ] **Step 6: Commit**

```bash
git add lib/device/ps2/ps2.h lib/device/ps2/ps2.cpp src/main.cpp lib/device/iec/meatloaf.h
git commit -m "ps2: PS2KeyboardDevice, config-gated with lazy start

Follows the DisplayLEDs idiom so config load and reloadAllConfig reach it.
devices.ps2.enabled gates it, default 0; nothing is allocated until the
first send or an explicit ps2 start, which re-announces BAT if already up.

Held keys are tracked and released in reverse on disable -- a stuck
modifier would corrupt keystrokes typed on the host's own keyboard.

typeText rejects non-ASCII rather than mangling it, and never PETSCII
encodes: PS/2 is not the IEC boundary."
```

---

### Task 10: The `ps2` console command

**Files:**
- Create: `lib/console/Commands/PS2Commands.h`
- Create: `lib/console/Commands/PS2Commands.cpp`
- Modify: `lib/console/Console.h`
- Modify: `lib/console/Console.cpp`

**Interfaces:**
- Consumes: `ps2Keyboard` (Task 9), `ps2keys::lookupKey` / `parseCombo`
  (Task 2), `ESP32Console::encodeAsciiCommand` (Task 2).
- Produces: `const ConsoleCommand ESP32Console::Commands::getPS2Command();`

- [ ] **Step 1: Write `PS2Commands.h`**

```cpp
#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getPS2Command();
}
```

No `#ifdef` — the stub class means this compiles everywhere, and `ps2 status`
on a board without the hardware reports "no PS/2 pins on this board".

- [ ] **Step 2: Write `PS2Commands.cpp`**

```cpp
#include "PS2Commands.h"

#include <string>
#include <vector>

#include "ps2.h"
#include "ps2_keynames.h"
#include "../dos_encode.h"
#include "../Helpers/PWDHelpers.h"
#include "string_utils.h"

using ESP32Console::encodeAsciiCommand;

namespace
{
    // The console splitter leaves each word as its own argv entry and collapses
    // runs of whitespace, so a typed sentence has to be rejoined.  Same
    // limitation `write` has: consecutive spaces cannot be reproduced, and
    // esp_console_run() silently drops arguments past CONSOLE_MAX_CMDLINE_ARGS.
    std::string joinFrom(int argc, char **argv, int first)
    {
        std::string out;
        for (int i = first; i < argc; i++)
        {
            if (!out.empty()) out += ' ';
            out += argv[i];
        }
        return out;
    }

    void printStatus()
    {
        printf("ps2: enabled[%d] running[%d]\r\n",
               ps2Keyboard.isEnabled() ? 1 : 0, ps2Keyboard.isRunning() ? 1 : 0);

        if (!ps2Keyboard.isRunning())
        {
            printf("     host handshake: n/a (device not started)\r\n");
            return;
        }

        // If the host booted before `ps2 start` ran it will never have sent
        // anything, and this line is what names that rather than leaving you
        // typing into the void.
        printf("     data reporting[%d]  caps[%d] num[%d] scroll[%d]\r\n",
               ps2Keyboard.dataReportingEnabled() ? 1 : 0,
               ps2Keyboard.capsLockOn() ? 1 : 0,
               ps2Keyboard.numLockOn() ? 1 : 0,
               ps2Keyboard.scrollLockOn() ? 1 : 0);

        const std::vector<ps2keys::Key> &held = ps2Keyboard.heldKeys();
        printf("     held[%d]", (int)held.size());
        for (ps2keys::Key k : held)
        {
            const char *n = ps2keys::keyName(k);
            printf(" %s", n ? n : "?");
        }
        printf("\r\n");
    }

    int ps2(int argc, char **argv)
    {
        if (argc < 2 || mstr::startsWith(argv[1], "status"))
        {
            printStatus();
            return EXIT_SUCCESS;
        }

        std::string sub = argv[1];

        if (sub == "start")
            return ps2Keyboard.startDevice() ? EXIT_SUCCESS : EXIT_FAILURE;

        if (sub == "enable")
        {
            ps2Keyboard.enable();
            printStatus();
            return EXIT_SUCCESS;
        }

        if (sub == "disable")
        {
            ps2Keyboard.disable();
            printStatus();
            return EXIT_SUCCESS;
        }

        if (sub == "release")
        {
            ps2Keyboard.releaseAll();
            return EXIT_SUCCESS;
        }

        if (sub == "keys")
        {
            std::vector<const char *> names;
            ps2keys::allNames(names);
            for (size_t i = 0; i < names.size(); i++)
                printf("%s%s", names[i], ((i % 8) == 7) ? "\r\n" : " ");
            printf("\r\n");
            return EXIT_SUCCESS;
        }

        if (sub == "type")
        {
            if (argc < 3) { printf("usage: ps2 type <text>\r\n"); return EXIT_FAILURE; }
            // encodeAsciiCommand applies the 0xNN run escape WITHOUT PETSCII,
            // so `ps2 type "dir0x0D"` sends dir then Enter.
            std::string text = encodeAsciiCommand(joinFrom(argc, argv, 2));
            if (!ps2Keyboard.typeText(text))
            {
                printf("ps2: type failed (disabled, not started, or non-ASCII)\r\n");
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        }

        if (sub == "key" || sub == "down" || sub == "up")
        {
            if (argc < 3) { printf("usage: ps2 %s <name>\r\n", sub.c_str()); return EXIT_FAILURE; }

            if (sub == "key")
            {
                std::vector<ps2keys::Key> keys;
                if (!ps2keys::parseCombo(argv[2], keys, &ps2Keyboard.overrides()))
                {
                    printf("ps2: unknown key in '%s' (try `ps2 keys`)\r\n", argv[2]);
                    return EXIT_FAILURE;
                }
                return ps2Keyboard.pressCombo(keys) ? EXIT_SUCCESS : EXIT_FAILURE;
            }

            ps2keys::Key k;
            if (!ps2keys::lookupKey(argv[2], k, &ps2Keyboard.overrides()))
            {
                printf("ps2: unknown key '%s' (try `ps2 keys`)\r\n", argv[2]);
                return EXIT_FAILURE;
            }
            bool ok = (sub == "down") ? ps2Keyboard.holdKey(k) : ps2Keyboard.releaseKey(k);
            return ok ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        printf("ps2 {status|start|enable|disable|type <text>|key <a>[+<b>]|"
               "down <name>|up <name>|release|keys}\r\n");
        return EXIT_FAILURE;
    }
}

namespace ESP32Console::Commands
{
    const ConsoleCommand getPS2Command()
    {
        return ConsoleCommand("ps2", &ps2, "Send keystrokes over the PS/2 interface");
    }
}
```

- [ ] **Step 3: Register the command**

In `lib/console/Console.h`, declare beside the other registrars:

```cpp
        void registerPS2Commands();
```

In `lib/console/Console.cpp`, after `registerDisplayCommands()`:

```cpp
    void Console::registerPS2Commands()
    {
        registerCommand(getPS2Command());
    }
```

and add `registerPS2Commands();` to the block near line 274 that calls
`registerCoreCommands()` and its siblings. Add
`#include "Commands/PS2Commands.h"` alongside the other command includes.

- [ ] **Step 4: Verify it compiles on both guard branches**

```bash
pgrep -f "platformio run" && echo "WAIT" || pio run -e lolin-d32-pro 2>&1 | tail -20
```

Expected: SUCCESS. Then the pinned board from Task 1.

- [ ] **Step 5: Confirm WebSocket reaches it with no extra code**

No code change — verify by reading `lib/www/ws/ws_command.cpp`:
`WsCommandExecutor::dispatch` falls through to `console.execute()` for plain
text, so a WS frame of `ps2 type hello` runs the same command. Note it in the
commit body; it is checked for real in Task 11.

- [ ] **Step 6: Commit**

```bash
git add lib/console/Commands/PS2Commands.h lib/console/Commands/PS2Commands.cpp \
        lib/console/Console.h lib/console/Console.cpp
git commit -m "ps2: console command

ps2 {status|start|enable|disable|type|key|down|up|release|keys}.
WebSocket reaches it for free -- WsCommandExecutor::dispatch falls through
to console.execute() for plain text.

ps2 type uses encodeAsciiCommand, which applies the 0xNN run escape
without PETSCII: PS/2 talks to a keyboard controller, not the IEC bus."
```

---

### Task 11: Hardware verification and DTV mapping discovery

Nothing below the key-name table has a native regression test — the wire, the
tasks, the ISR and the teardown are hardware-only. This task is the evidence.

**Files:**
- Modify: `AGENTS.md` (record durable findings)
- Modify: `docs/superpowers/specs/2026-08-25-ps2-keyboard-output-design.md`
  (fill in the Open Questions section)

**Interfaces:**
- Consumes: everything from Tasks 1-10.
- Produces: the DTV scancode mapping, recorded as `devices.ps2.keymap` defaults.

- [ ] **Step 1: Confirm the link**

Build and flash the pinned board from Task 1 Step 1. A successful link is the
proof for P1 — the duplicate-symbol failure fires as soon as anything references
the component, which `PS2KeyboardDevice` now does unconditionally on a pinned
board.

If `fujiloaf-rev0` is the only pinned board and it still does not build, repair
`lib/bus/iec/IECConfig.h` first (line 67 closes the `#ifndef IECCONFIG_H`
include guard early, so line 106 is `#endif without #if`) — but do that as its
own commit, not folded in here.

- [ ] **Step 2: Verify core 0 survives — the most load-bearing prediction**

```
ps2 enable
ps2 start
```

Then, with the device running, exercise the web server (load a page) and the
console (run `ls`). Both must respond normally.

Before Task 5, both PS/2 tasks never blocked — priority 10 and 9 on core 0 —
which should have starved `httpd` at priority 5. If either stalls now, Task 5
or Task 7 is wrong.

- [ ] **Step 3: Verify teardown reclaims memory**

```
meminfo
ps2 start
meminfo
ps2 disable
meminfo
```

Expected: internal free drops by roughly 8 KB after `start` (two 4096-byte task
stacks) plus ~440 bytes for the queue and mutex, and returns after `disable`.
`disable` must return promptly — if it takes a second and logs "tasks did not
exit", Task 5's cooperative exit is not working.

- [ ] **Step 4: Verify the first byte on the wire**

```
ps2 type "a"
```

Expected: an `A` appears on the DTV. If nothing does, scope CLK and DATA — a
byte is 11 bit cells at ~80 us each, roughly 900 us total.

- [ ] **Step 5: Verify backpressure**

```
ps2 type "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
```

Expected: all 200 characters arrive, taking **2-6 seconds** — 10 ms per unshifted
character and 30 ms per shifted one, from `type()`'s own `vTaskDelay` between
keydown and keyup. NOT the ~400 ms this step originally claimed: that came from
queue-drain rate alone and was corrected during implementation (see spec P5).

**This step does not test backpressure, and cannot.** The producer cannot outrun
the consumer through `type()`, so the queue never fills. What it verifies is that
a long string arrives complete and in order — treat a truncated result as a wire
or encoding fault, not a queue-timeout fault.

- [ ] **Step 6: Verify held keys and the safety release**

```
ps2 down shift
ps2 status
ps2 type "abc"
ps2 release
ps2 status
```

Expected: `ps2 status` lists `shift` as held; the typed text arrives shifted;
after `release` the held list is empty. Then confirm `ps2 disable` also clears
a held key — press `ps2 down ctrl`, then `ps2 disable`, and check the DTV is not
left with a stuck modifier.

- [ ] **Step 7: Verify WebSocket reaches it**

Send the text frame `ps2 type hello` over the WebSocket connection. Expected:
`hello` appears on the DTV, with no PS/2-specific web code anywhere.

- [ ] **Step 8: Discover the DTV scancode mapping**

Run `ps2 keys` to list the names, then send each candidate and record what the
DTV produces:

```
ps2 key escape
ps2 key pageup
ps2 key tab
ps2 key home
ps2 key insert
ps2 key delete
```

Record which PS/2 key the DTV turns into RUN/STOP, RESTORE, C= (Commodore),
CLR/HOME, INST/DEL, and the arrow keys. Also note whether the DTV sends anything
back — `ps2 status` showing data-reporting or LED state changing is the tell,
and it answers whether the Task 7 interrupt path ever fires in practice.

- [ ] **Step 9: Record the mapping as config**

Write the discovered bindings into the shipped
`data/BUILD_IEC.*/.sys/devices.json` files:

```json
    "ps2": {
      "enabled": 0,
      "keymap": {
        "runstop": "escape",
        "restore": "pageup",
        "commodore": "tab"
      }
    }
```

Substitute whatever Step 8 actually found — these three values are
placeholders for the shape, not a guess at the answer. Then verify
`ps2 key runstop` works.

- [ ] **Step 10: Record durable findings**

Add an `## Important Notes` entry to `AGENTS.md` covering: that the PS/2 guard
is `PIN_KB_CLK` and why `pinmap_defaults.h` must not reacquire a fallback; that
nothing may call `gpio_install_isr_service()` in the component; that
`pdMS_TO_TICKS(n<10)` is 0 at `CONFIG_FREERTOS_HZ=100`; and that the PS/2 path
is ASCII and must never be PETSCII-encoded. Fill in the spec's Open Questions
section with the Step 8 results.

- [ ] **Step 11: Commit**

```bash
git add AGENTS.md docs/superpowers/specs/2026-08-25-ps2-keyboard-output-design.md \
        data/BUILD_IEC.*/.sys/devices.json
git commit -m "ps2: hardware verification and DTV keymap

Records the DTV's scancode-to-C64-key mapping discovered on the bench,
and the durable rules in AGENTS.md."
```
