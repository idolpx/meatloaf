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

// at_parser.h's whole body is gated on ENABLE_MODEM (mandatory per this
// feature's build convention -- every modem file compiles to nothing without
// it). This test file is its own translation unit, separate from
// engine_sources.cpp, whose #define does not reach here -- so it needs its
// own, for the same reason and by the same reasoning engine_sources.cpp
// documents: defining it in the native env's build_flags would apply to
// every native suite, not just this one.
#define ENABLE_MODEM 1

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

    RUN_TEST(test_settings_factory_defaults);
    RUN_TEST(test_settings_register_round_trip);
    RUN_TEST(test_settings_rejects_out_of_range_register);
    RUN_TEST(test_settings_rejects_out_of_range_value);
    RUN_TEST(test_settings_serialize_round_trip);
    RUN_TEST(test_settings_serialization_omits_defaults);
    RUN_TEST(test_settings_deserialization_ignores_bad_values);

    return UNITY_END();
}
