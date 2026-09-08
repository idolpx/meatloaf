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

    RUN_TEST(test_result_verbose_is_wrapped_in_terminators);
    RUN_TEST(test_result_numeric_form);
    RUN_TEST(test_result_quiet_suppresses_everything);
    RUN_TEST(test_result_honours_s3_and_s4);
    RUN_TEST(test_result_x0_degrades_extended_codes_to_no_carrier);
    RUN_TEST(test_result_x_levels_gate_each_extended_code);
    RUN_TEST(test_result_x_degradation_applies_in_numeric_mode);

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

    return UNITY_END();
}
