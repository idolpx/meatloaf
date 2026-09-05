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
    Overrides ov;
    ov["restore"] = "pageup";
    Key k;
    TEST_ASSERT_TRUE(ps2keys::lookupKey("restore", k, &ov));
    TEST_ASSERT_EQUAL(ps2dev::scancodes::K_PAGEUP, k);
}

static void test_override_pointing_at_unknown_name_is_rejected(void)
{
    Overrides ov;
    ov["restore"] = "bogus";
    Key k;
    TEST_ASSERT_FALSE(ps2keys::lookupKey("restore", k, &ov));
}

static void test_override_does_not_shadow_unrelated_names(void)
{
    Overrides ov;
    ov["restore"] = "pageup";
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
    for (size_t i = 0; i < names.size(); i++)
    {
        Key k;
        TEST_ASSERT_TRUE_MESSAGE(ps2keys::lookupKey(names[i], k), names[i]);
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
    for (size_t i = 0; i < s.size(); i++)
    {
        unsigned char c = (unsigned char)s[i];
        out += digits[c >> 4];
        out += digits[c & 0x0F];
    }
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
