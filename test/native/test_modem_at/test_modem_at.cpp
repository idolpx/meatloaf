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
// codes, escape detector and phonebook are all reachable here. TelnetMStream
// (lib/meatloaf/network/telnet.cpp) is reachable too, via a FakeMStream
// standing in for the tcp:// transport -- see the telnet_stream section below
// for why that is safe (createStream(), the one thing that actually dials
// out through MFSOwner, is never exercised). What is NOT reachable is
// anything that genuinely needs a socket or FreeRTOS: the dial path itself,
// ModemPort and the modem task are verified on hardware.
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

void test_parse_accepts_the_harmless_hayes_verbs_terminals_send(void)
{
    // A terminal's init string carries verbs for hardware a virtual modem
    // does not have -- carrier detect, DTR, flow control, speaker volume.
    // Each parses as an ordinary numeric-suffix command; executeCommand
    // accepts and ignores them. Failing the line instead made real terminal
    // software give up on the modem entirely.
    const char *lines[] = {
        "ATB0", "ATL3", "ATM1", "ATN1", "ATW2", "ATY0",
        "AT&C1", "AT&D2", "AT&K3", "AT&G0", "AT&Q5", "AT&R1", "AT&T0",
    };

    for (const char *text : lines)
    {
        AtLine line;
        size_t err = 0;
        TEST_ASSERT_TRUE_MESSAGE(at_parse(text, line, &err), text);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, line.size(), text);
    }
}

void test_parse_reads_a_full_terminal_init_string_as_one_line(void)
{
    // The shape that actually arrives from a terminal program, in one line.
    // Every command must be present and in order -- a line that half-applies
    // and then errors is the failure mode this change exists to remove.
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATE0V1&C1&D2&K3S0=0", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(6, line.size());

    TEST_ASSERT_EQUAL_CHAR('E', line[0].verb);
    TEST_ASSERT_EQUAL_INT(0, line[0].number);
    TEST_ASSERT_EQUAL_CHAR('V', line[1].verb);
    TEST_ASSERT_EQUAL_INT(1, line[1].number);
    TEST_ASSERT_EQUAL_CHAR('&', line[2].prefix);
    TEST_ASSERT_EQUAL_CHAR('C', line[2].verb);
    TEST_ASSERT_EQUAL_INT(1, line[2].number);
    TEST_ASSERT_EQUAL_CHAR('&', line[3].prefix);
    TEST_ASSERT_EQUAL_CHAR('D', line[3].verb);
    TEST_ASSERT_EQUAL_INT(2, line[3].number);
    TEST_ASSERT_EQUAL_CHAR('&', line[4].prefix);
    TEST_ASSERT_EQUAL_CHAR('K', line[4].verb);
    TEST_ASSERT_EQUAL_INT(3, line[4].number);
    TEST_ASSERT_EQUAL_CHAR('S', line[5].verb);
    TEST_ASSERT_EQUAL_INT(0, line[5].number);
}

void test_parse_accepts_view_configuration(void)
{
    // AT&V is a real command, not one of the ignored group: executeCommand
    // aliases it to the ATI1 settings report.
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("AT&V", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('&', line[0].prefix);
    TEST_ASSERT_EQUAL_CHAR('V', line[0].verb);
}

void test_parse_still_rejects_a_verb_that_means_nothing(void)
{
    // The mutation guard for the two tests above: the accept list is
    // explicit, so a typo is still reported rather than silently answering
    // OK. Without this, widening the list to "any letter" passes everything
    // else here. Both prefixes, since they have separate lists.
    AtLine line;
    size_t err = 0;

    TEST_ASSERT_FALSE(at_parse("ATG", line, &err));
    TEST_ASSERT_EQUAL_UINT(2, err);

    err = 0;
    TEST_ASSERT_FALSE(at_parse("ATE0&Z1", line, &err));
    TEST_ASSERT_EQUAL_UINT(4, err);

    // And a failure part way along still reports the position of the verb
    // that failed, not the start of the line.
    err = 0;
    TEST_ASSERT_FALSE(at_parse("ATE0&C1J&D2", line, &err));
    TEST_ASSERT_EQUAL_UINT(7, err);
}

void test_parse_accepts_pulse_as_a_dial_modifier_and_drops_it(void)
{
    // ATDP is pulse dialling, which has no meaning over a socket. It is
    // accepted so a user typing it is not refused, and deliberately NOT kept
    // in mods: doDial falls back to a phonebook entry's own modifiers only
    // when the dial line carried none, so a retained 'P' would suppress a
    // stored 'T' and dial a telnet entry as raw TCP.
    AtLine line;
    TEST_ASSERT_TRUE(at_parse("ATDP\"host:23\"", line, nullptr));
    TEST_ASSERT_EQUAL_UINT(1, line.size());
    TEST_ASSERT_EQUAL_CHAR('D', line[0].verb);
    TEST_ASSERT_EQUAL_STRING("", line[0].mods.c_str());
    TEST_ASSERT_EQUAL_STRING("host:23", line[0].arg.c_str());

    // Mixed with a modifier that does mean something, only that one survives.
    AtLine line2;
    TEST_ASSERT_TRUE(at_parse("ATDPT\"host:23\"", line2, nullptr));
    TEST_ASSERT_EQUAL_STRING("T", line2[0].mods.c_str());

    // And a pulse-dialled phonebook entry is the same dial as ATD<n>.
    AtLine line3;
    TEST_ASSERT_TRUE(at_parse("ATDP5", line3, nullptr));
    TEST_ASSERT_EQUAL_STRING("", line3[0].mods.c_str());
    TEST_ASSERT_EQUAL_STRING("5", line3[0].arg.c_str());
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

// Fix round 1: if a byte arrives after the trailing guard has already fully
// elapsed -- tick() simply hasn't been polled yet -- the escape has already
// won. feed() must report it immediately rather than let the blanket release
// silently downgrade it to data, and the racing byte must be dropped.
void test_escape_feed_after_guard_elapsed_reports_escaped_and_drops_the_byte(void)
{
    EscapeDetector d = make_detector();
    std::string fwd;

    d.feed('x', 1000, fwd);
    fwd.clear();

    d.feed('+', 3000, fwd);
    d.feed('+', 3100, fwd);
    d.feed('+', 3200, fwd);   // enters TRAILING

    // The 1000 ms trailing guard has fully elapsed by 4300, but tick() was
    // never called -- this byte arrives first.
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::ESCAPED == d.feed('z', 4300, fwd));
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());
}

// Fix round 1: seen_any_ is what lets an escape typed as the very first thing
// after connect start a candidate even though now_ms itself is small (and so
// less than last_byte_ms_'s zero-initialised value would suggest a long gap).
// Without it this first byte would be forwarded as data instead of held.
void test_escape_starts_a_candidate_on_the_very_first_byte_even_with_a_small_timestamp(void)
{
    EscapeDetector d;
    d.configure('+', 250);   // 250 * 20 = 5000 ms guard, far above now_ms below
    d.reset();

    std::string fwd;
    TEST_ASSERT_TRUE(EscapeDetector::Verdict::NONE == d.feed('+', 5, fwd));
    TEST_ASSERT_EQUAL_STRING("", fwd.c_str());
}

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

// ------------------------------------------------------------- telnet_stream
//
// TelnetMStream (lib/meatloaf/network/telnet.cpp) decorates an inner
// MStream -- normally TCPMStream -- with the filter tested above. FakeMStream
// stands in for that inner transport with a scripted read() queue, so the
// EOF/idle distinction, lazy open and waitReadable's timing can be driven
// deterministically with no real socket. createStream() (the one thing that
// touches MFSOwner, to dial tcp://) is deliberately never called from here --
// every test below constructs TelnetMStream directly, as Task 9-11's dial
// code and iecChannelHandlerFile both do once they hold a stream.

#include "network/telnet.h"

#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

namespace
{
// One scripted response for FakeMStream::read(). An empty, non-sentinel
// event means recv()==0 -- a genuine peer hangup. is_no_data means the
// _MEAT_NO_DATA_AVAIL sentinel -- idle, socket still open.
struct ReadEvent
{
    std::vector<uint8_t> bytes;
    bool is_no_data = false;
};

class FakeMStream : public MStream
{
public:
    FakeMStream() : MStream("fake://test") {}

    std::deque<ReadEvent> queue;
    int open_call_count = 0;
    bool opened = false;
    bool closed = true;
    bool write_should_fail = false;
    // Simulates an inner stream whose waitReadable() returns without
    // actually blocking -- the case telnet.cpp's own comment calls out as
    // something a future inner_ could legitimately do, and which the old
    // `waited += slice` accounting could not survive.
    bool instant_wait = false;
    // Counts entries into waitReadable(). The only way to discriminate
    // telnet.cpp's sleep-remainder fix: wall-clock elapsed is ~timeout_ms
    // either way, because the outer loop returns on a real clock -- what the
    // fix changes is how many times it spins to get there.
    int wait_call_count = 0;
    std::string written;

    bool isOpen() override { return opened && !closed; }

    bool open(std::ios_base::openmode) override
    {
        open_call_count++;
        opened = true;
        closed = false;
        return true;
    }

    void close() override { closed = true; }

    uint32_t read(uint8_t *buf, uint32_t size) override
    {
        if (queue.empty())
            return _MEAT_NO_DATA_AVAIL;
        ReadEvent ev = queue.front();
        queue.pop_front();
        if (ev.is_no_data)
            return _MEAT_NO_DATA_AVAIL;
        uint32_t n = (uint32_t)ev.bytes.size();
        if (n > size)
            n = size;
        if (n > 0)
            memcpy(buf, ev.bytes.data(), n);
        // 0 (an empty, non-sentinel event) means EOF. TCPMStream::read()
        // records that in its own eof_ and answers isOpen() false from then
        // on -- the local descriptor stays valid after a remote FIN, so
        // read() is the only place the hangup is observable. Model that here
        // or every assertion about a drained-EOF TelnetMStream is made
        // against an inner stream that behaves as no real one does.
        if (n == 0 && !ev.is_no_data)
            closed = true;
        return n;
    }

    uint32_t write(const uint8_t *buf, uint32_t size) override
    {
        if (write_should_fail)
            return size > 0 ? size - 1 : 0;  // short write
        written.append((const char *)buf, size);
        return size;
    }

    bool waitReadable(uint32_t timeout_ms) override
    {
        wait_call_count++;
        if (instant_wait)
            return false;
        return MStream::waitReadable(timeout_ms);
    }

    bool seek(uint32_t) override { return false; }
    uint32_t size() override { return 0; }
    uint32_t position() override { return 0; }
};

void push_bytes(FakeMStream &f, const char *s)
{
    ReadEvent ev;
    ev.bytes.assign(s, s + strlen(s));
    f.queue.push_back(ev);
}

void push_eof(FakeMStream &f) { f.queue.push_back(ReadEvent{}); }
} // namespace

// (a) A caller must receive every byte that was already buffered BEFORE
// isOpen() reports false, even though the peer hung up (the queued EOF)
// before the last of them was drained.
void test_telnet_stream_drains_buffered_bytes_before_isopen_goes_false(void)
{
    auto fake = std::make_shared<FakeMStream>();
    push_bytes(*fake, "HELLO");
    push_eof(*fake);

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];

    // First read: opens lazily, pumps "HELLO" into rx_, hands back 2 bytes.
    // rx_ still holds "LLO" -- pump() has not touched the queued EOF yet.
    TEST_ASSERT_EQUAL_UINT32(2, ts.read(buf, 2));
    TEST_ASSERT_EQUAL_UINT8('H', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('E', buf[1]);
    TEST_ASSERT_TRUE(ts.isOpen());

    // Second read: rx_ is not empty, so no pump runs this call either.
    TEST_ASSERT_EQUAL_UINT32(2, ts.read(buf, 2));
    TEST_ASSERT_EQUAL_UINT8('L', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('L', buf[1]);
    TEST_ASSERT_TRUE(ts.isOpen());

    // Third read: drains the very last buffered byte. rx_ was non-empty
    // when this call STARTED, so pump() still does not run -- the EOF is
    // not observed in this same call, and every byte the peer sent has now
    // reached the caller.
    TEST_ASSERT_EQUAL_UINT32(1, ts.read(buf, 8));
    TEST_ASSERT_EQUAL_UINT8('O', buf[0]);
    TEST_ASSERT_TRUE(ts.isOpen());

    // Fourth read: rx_ is finally empty, so THIS call pumps -- and only now
    // discovers the queued EOF.
    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, 8));
    TEST_ASSERT_FALSE(ts.isOpen());
}

// (a2) The drain grace must survive the inner stream noticing the hangup
// FIRST, which is the only way it ever happens now: TCPMStream::isOpen()
// answers false the moment its own read() sees the peer's FIN, so bytes this
// stream already decoded out of that last chunk are still sitting in rx_ when
// the transport underneath reports itself closed. isOpen() therefore tests rx_
// before it asks inner_ -- asking inner_ first strands those bytes.
void test_telnet_stream_buffered_bytes_outlive_a_closed_inner_stream(void)
{
    auto fake = std::make_shared<FakeMStream>();
    push_bytes(*fake, "HELLO");

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];

    // Opens lazily, pumps "HELLO" into rx_, hands back 2. rx_ holds "LLO".
    TEST_ASSERT_EQUAL_UINT32(2, ts.read(buf, 2));
    TEST_ASSERT_TRUE(ts.isOpen());

    // The transport discovers the hangup on its own, before this stream has
    // pumped again -- exactly what a real TCPMStream does on a FIN.
    fake->close();
    TEST_ASSERT_FALSE(fake->isOpen());

    // Still open: there are decoded bytes the caller has not been given.
    TEST_ASSERT_TRUE(ts.isOpen());

    TEST_ASSERT_EQUAL_UINT32(3, ts.read(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8('L', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('L', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('O', buf[2]);

    // Drained, and the inner stream is gone -- now it is closed for good.
    TEST_ASSERT_FALSE(ts.isOpen());
}

// (b) Nothing opens the inner transport until the first read()/write() --
// mirroring tcp.h's own lazy-open idiom.
void test_telnet_stream_lazy_opens_on_first_read(void)
{
    auto fake = std::make_shared<FakeMStream>();
    push_eof(*fake);  // so the pump the lazy open triggers returns quickly

    TelnetMStream ts("telnet://test:23", fake);
    TEST_ASSERT_FALSE(fake->opened);

    uint8_t buf[8];
    ts.read(buf, sizeof(buf));
    TEST_ASSERT_TRUE(fake->opened);
    TEST_ASSERT_EQUAL_INT(1, fake->open_call_count);
}

// (c) Once a stream has drained to EOF, a further read() must not resurrect
// it by re-entering open() -- which would both silently clear a write error
// still waiting to be read by the caller (open() resets `_error = 0` on
// success) and reset eof_, making a permanently-dead session look freshly
// reopened. FakeMStream closes on EOF, as TCPMStream does (a real
// socket: the local fd stays valid after a remote FIN), so a resurrection
// here would not be caught by "did the transport get redialed" -- it has to
// be caught by these two more specific effects instead.
void test_telnet_stream_drained_eof_does_not_resurrect_or_clear_error(void)
{
    auto fake = std::make_shared<FakeMStream>();

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];

    // Open the stream on a live (not yet EOF) connection, so the failing
    // write below sets _error via its own ordinary failure path -- not by
    // going through open() at all, which would make the effect this test
    // targets unobservable (open()'s `_error = 0` would be immediately
    // overwritten by the very write that triggered it).
    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, sizeof(buf)));  // idle sentinel
    TEST_ASSERT_TRUE(ts.isOpen());

    fake->write_should_fail = true;
    const uint8_t data[] = { 'x', 'y' };
    TEST_ASSERT_EQUAL_UINT32(0, ts.write(data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT(1, ts.error());
    fake->write_should_fail = false;

    // Now drain to EOF, with the error still pending and untouched (read()
    // never assigns _error).
    push_eof(*fake);
    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, sizeof(buf)));
    TEST_ASSERT_FALSE(ts.isOpen());
    TEST_ASSERT_EQUAL_UINT(1, ts.error());

    // The discriminating call: a further read on the drained-EOF stream.
    // Queue a byte that a resurrection would wrongly deliver.
    push_bytes(*fake, "Z");
    uint32_t n = ts.read(buf, sizeof(buf));

    // Correct: still refused. The pending error must survive untouched, and
    // the queued byte must not have been delivered -- a drained-EOF stream
    // stays dead until an explicit close() + reopen, not until the peer
    // happens to have more to say.
    TEST_ASSERT_EQUAL_UINT32(0, n);
    TEST_ASSERT_EQUAL_UINT(1, ts.error());
}

// (d) available()==0 must not read as end-of-stream: an idle-but-live BBS
// session reports it constantly between sends. eos() must track isOpen(),
// not available().
void test_telnet_stream_eos_is_false_while_idle_true_once_finished(void)
{
    auto fake = std::make_shared<FakeMStream>();
    // Queue left empty: every read() returns the idle sentinel.

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, ts.available());
    TEST_ASSERT_FALSE(ts.eos());  // idle-but-live, not the same as finished

    push_eof(*fake);
    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, sizeof(buf)));
    TEST_ASSERT_TRUE(ts.eos());   // now genuinely finished
}

// (e) A failed transmit must be reported: write() returns 0, not the
// caller's byte count, and error() reflects it.
void test_telnet_stream_write_reports_a_transmit_failure(void)
{
    auto fake = std::make_shared<FakeMStream>();
    fake->write_should_fail = true;

    TelnetMStream ts("telnet://test:23", fake);
    const uint8_t data[] = { 'a', 'b', 'c' };
    TEST_ASSERT_EQUAL_UINT32(0, ts.write(data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT(1, ts.error());
}

// (f) waitReadable() must not sit out the full timeout once EOF has been
// discovered mid-wait.
void test_telnet_stream_waitreadable_returns_promptly_once_eof_is_discovered(void)
{
    auto fake = std::make_shared<FakeMStream>();
    push_eof(*fake);  // queued, not yet read

    TelnetMStream ts("telnet://test:23", fake);

    // Open via write() rather than read(), so the queued EOF event is still
    // untouched going into the wait below.
    const uint8_t data[] = { 'x' };
    ts.write(data, sizeof(data));
    TEST_ASSERT_TRUE(ts.isOpen());

    auto start = std::chrono::steady_clock::now();
    bool readable = ts.waitReadable(5000);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    TEST_ASSERT_FALSE(readable);
    TEST_ASSERT_FALSE(ts.isOpen());  // the wait's own pump() found the EOF
    TEST_ASSERT_TRUE_MESSAGE(elapsed_ms < 1000,
        "waitReadable() burned the full 5000ms timeout instead of "
        "returning promptly once EOF was discovered");
}

// (f) waitReadable() must measure real elapsed time rather than assume each
// slice fully elapsed. An inner wait that returns without blocking (legal --
// waitReadable() is only guaranteed to wait UP TO its argument) must not
// make the loop think time passed that did not: with the old `waited +=
// slice` accounting this would return in a handful of microseconds instead
// of genuinely waiting out the requested timeout.
void test_telnet_stream_waitreadable_measures_real_elapsed_time(void)
{
    auto fake = std::make_shared<FakeMStream>();
    // Queue left empty (idle sentinel) so the opening read does not set eof_.

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];
    ts.read(buf, sizeof(buf));
    TEST_ASSERT_TRUE(ts.isOpen());

    fake->instant_wait = true;  // inner wait never actually blocks

    auto start = std::chrono::steady_clock::now();
    bool readable = ts.waitReadable(150);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    TEST_ASSERT_FALSE(readable);
    TEST_ASSERT_TRUE_MESSAGE(elapsed_ms >= 100,
        "waitReadable() returned too quickly -- it must measure real "
        "elapsed time rather than assume each slice fully elapsed");
    TEST_ASSERT_TRUE_MESSAGE(elapsed_ms < 1000,
        "waitReadable() took far longer than the requested 150ms timeout");
}

// (fix round 3, FINDING 1) The write()-side mirror of test (c) above.
// Mutation 1 in fix round 2 only reverted the read() site's guard -- it
// never exercised write()'s independently, so a re-review flipping ONLY
// write()'s `!open_` back to `!isOpen()` still passed the whole suite. This
// opens the stream via write() (not read()), so the resurrection this test
// targets can only be reached through write()'s own guard.
void test_telnet_stream_write_side_drained_eof_does_not_clear_pending_error(void)
{
    auto fake = std::make_shared<FakeMStream>();

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];

    // Open via write(), not read() -- the discriminator this test needs.
    const uint8_t open_byte[] = { 'o' };
    TEST_ASSERT_EQUAL_UINT32(1, ts.write(open_byte, 1));
    TEST_ASSERT_TRUE(ts.isOpen());
    TEST_ASSERT_EQUAL_INT(1, fake->open_call_count);

    // A failing write on the still-live connection sets a pending error via
    // its own ordinary failure path (same reasoning as test (c): not via a
    // resurrection, so the effect under test stays observable).
    fake->write_should_fail = true;
    const uint8_t data[] = { 'x', 'y' };
    TEST_ASSERT_EQUAL_UINT32(0, ts.write(data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT(1, ts.error());
    fake->write_should_fail = false;

    // Drain to EOF with the error still pending.
    push_eof(*fake);
    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, sizeof(buf)));
    TEST_ASSERT_FALSE(ts.isOpen());
    TEST_ASSERT_EQUAL_UINT(1, ts.error());

    // The discriminating call: a further write() on the drained-EOF stream.
    // A resurrection re-enters open(), which resets `_error = 0` and
    // `eof_ = false` -- observable here even though the write itself then
    // succeeds either way (FakeMStream's inner transport never truly closes
    // on EOF, matching a real socket, so the write reaches it regardless).
    const uint8_t more[] = { 'z' };
    ts.write(more, 1);
    TEST_ASSERT_EQUAL_UINT(1, ts.error());
    TEST_ASSERT_FALSE(ts.isOpen());
}

// (fix round 3, FINDING 1) Covers the `inner_->isOpen() == false` branch in
// open() -- calling inner_->open() again to redial. No existing test reached
// it past the very first open() of a fresh pair: FakeMStream never closes
// itself on EOF, and nothing called TelnetMStream::close() before this.
// An explicit close() DOES close the inner transport (TelnetMStream::close()
// calls inner_->close()), so the next lazy-open genuinely finds
// inner_->isOpen() false and must redial.
void test_telnet_stream_explicit_close_then_reopen_redials_inner(void)
{
    auto fake = std::make_shared<FakeMStream>();
    // Idle sentinel: opens cleanly without reaching EOF.

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];

    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, sizeof(buf)));  // lazy-opens
    TEST_ASSERT_TRUE(ts.isOpen());
    TEST_ASSERT_EQUAL_INT(1, fake->open_call_count);

    ts.close();
    TEST_ASSERT_TRUE(fake->closed);
    TEST_ASSERT_FALSE(fake->isOpen());  // the branch under test needs this false

    // Reopen: a fresh payload proves the inner transport was genuinely
    // re-dialed, not just the outer bookkeeping reset.
    push_bytes(*fake, "HI");
    TEST_ASSERT_EQUAL_UINT32(2, ts.read(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8('H', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('I', buf[1]);
    TEST_ASSERT_TRUE(ts.isOpen());
    TEST_ASSERT_EQUAL_INT(2, fake->open_call_count);
}

// (fix round 3, FINDING 2) eos() must lazy-open a fresh stream rather than
// report it as already finished. Uses its own fake/stream pair (not shared
// with the waitReadable() test below) so eos() opening the stream cannot
// mask whether waitReadable() ALSO opens on its own.
void test_telnet_stream_eos_lazy_opens_a_fresh_stream(void)
{
    auto fake = std::make_shared<FakeMStream>();
    push_bytes(*fake, "HI");

    TelnetMStream ts("telnet://test:23", fake);
    TEST_ASSERT_FALSE(fake->opened);  // exactly what createStream() hands back

    TEST_ASSERT_FALSE(ts.eos());
    TEST_ASSERT_TRUE(fake->opened);  // eos() had to open it to answer correctly
}

// (fix round 3, FINDING 2) waitReadable() must lazy-open a fresh stream too
// -- `while (waitReadable(...)) { read(...); }` on a freshly dialed stream
// is exactly Tasks 9-11's connection-pump shape.
void test_telnet_stream_waitreadable_lazy_opens_a_fresh_stream(void)
{
    auto fake = std::make_shared<FakeMStream>();
    push_bytes(*fake, "HI");

    TelnetMStream ts("telnet://test:23", fake);
    TEST_ASSERT_FALSE(fake->opened);

    TEST_ASSERT_TRUE(ts.waitReadable(1000));
    TEST_ASSERT_TRUE(fake->opened);
}

// (fix round 3, FINDING 3) telnet.cpp's waitReadable() sleeps out whatever
// part of each slice the inner wait did not consume. An inner that returns
// immediately -- what `instant_wait` constructs -- would otherwise make the
// outer loop a hot spin with no yield for the whole timeout, burning a core
// on a FreeRTOS task shared with the IEC bus.
//
// Wall-clock cannot discriminate this: elapsed is ~timeout_ms with or without
// the fix, since the loop only returns once a real clock says the timeout
// expired. The ITERATION COUNT is what changes -- bounded to roughly
// timeout_ms/slice with the fix, unbounded without it.
void test_telnet_stream_waitreadable_does_not_hot_spin_on_an_instant_inner(void)
{
    auto fake = std::make_shared<FakeMStream>();
    fake->instant_wait = true;   // returns false without blocking

    TelnetMStream ts("telnet://test:23", fake);
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT32(0, ts.read(buf, sizeof(buf)));  // lazy-open, no data
    fake->wait_call_count = 0;                               // count the loop only

    TEST_ASSERT_FALSE(ts.waitReadable(150));

    // 150ms of 50ms slices is 3 iterations; allow generous headroom for a
    // slow host while still failing by orders of magnitude if the sleep is
    // gone (an unslept spin re-enters the inner wait thousands of times).
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(20, fake->wait_call_count,
        "waitReadable() spun without yielding -- the per-slice sleep remainder is missing");
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
    RUN_TEST(test_parse_plus_command_takes_an_assignment);
    RUN_TEST(test_parse_plus_assignment_survives_a_two_million_baud_value);
    RUN_TEST(test_parse_plus_command_takes_a_query);
    RUN_TEST(test_parse_plus_command_with_no_argument_is_still_valid);
    RUN_TEST(test_parse_plus_command_rejects_a_malformed_assignment);
    RUN_TEST(test_parse_plus_command_name_is_uppercased);
    RUN_TEST(test_parse_numeric_suffix_defaults_to_zero_when_absent);
    RUN_TEST(test_parse_whitespace_between_commands_is_ignored);
    RUN_TEST(test_parse_phonebook_store_with_trailing_modifiers);
    RUN_TEST(test_parse_phonebook_store_without_modifiers);
    RUN_TEST(test_parse_phonebook_delete_form);
    RUN_TEST(test_parse_phonebook_list_form);
    RUN_TEST(test_parse_unterminated_quote_is_an_error_at_the_quote);
    RUN_TEST(test_parse_unknown_verb_is_an_error_at_the_verb);
    RUN_TEST(test_parse_accepts_the_harmless_hayes_verbs_terminals_send);
    RUN_TEST(test_parse_reads_a_full_terminal_init_string_as_one_line);
    RUN_TEST(test_parse_accepts_view_configuration);
    RUN_TEST(test_parse_still_rejects_a_verb_that_means_nothing);
    RUN_TEST(test_parse_accepts_pulse_as_a_dial_modifier_and_drops_it);
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
    RUN_TEST(test_escape_feed_after_guard_elapsed_reports_escaped_and_drops_the_byte);
    RUN_TEST(test_escape_starts_a_candidate_on_the_very_first_byte_even_with_a_small_timestamp);

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

    RUN_TEST(test_telnet_plain_data_reaches_the_application);
    RUN_TEST(test_telnet_iac_negotiation_never_reaches_the_application);
    RUN_TEST(test_telnet_negotiation_between_data_is_stripped_in_place);
    RUN_TEST(test_telnet_escaped_iac_becomes_one_literal_byte);
    RUN_TEST(test_telnet_transmit_escapes_a_literal_iac);
    RUN_TEST(test_telnet_negotiation_split_across_reads_is_handled);
    RUN_TEST(test_telnet_end_is_safe_to_call_twice);
    RUN_TEST(test_telnet_calls_after_end_are_inert);

    RUN_TEST(test_telnet_stream_drains_buffered_bytes_before_isopen_goes_false);
    RUN_TEST(test_telnet_stream_buffered_bytes_outlive_a_closed_inner_stream);
    RUN_TEST(test_telnet_stream_lazy_opens_on_first_read);
    RUN_TEST(test_telnet_stream_drained_eof_does_not_resurrect_or_clear_error);
    RUN_TEST(test_telnet_stream_eos_is_false_while_idle_true_once_finished);
    RUN_TEST(test_telnet_stream_write_reports_a_transmit_failure);
    RUN_TEST(test_telnet_stream_waitreadable_returns_promptly_once_eof_is_discovered);
    RUN_TEST(test_telnet_stream_waitreadable_measures_real_elapsed_time);
    RUN_TEST(test_telnet_stream_write_side_drained_eof_does_not_clear_pending_error);
    RUN_TEST(test_telnet_stream_explicit_close_then_reopen_redials_inner);
    RUN_TEST(test_telnet_stream_eos_lazy_opens_a_fresh_stream);
    RUN_TEST(test_telnet_stream_waitreadable_lazy_opens_a_fresh_stream);
    RUN_TEST(test_telnet_stream_waitreadable_does_not_hot_spin_on_an_instant_inner);

    return UNITY_END();
}
