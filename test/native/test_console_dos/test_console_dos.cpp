// Console DOS command encoding and channel transfer loops.
//
// These are the two pieces of the console's file-I/O commands that can be
// tested off-device: lib/console is not compiled in the native environment, so
// encodeDosCommand() and the read/write loops were lifted into units that
// depend on nothing but <string> and mstr::toPETSCII2().
//
// The channel layer underneath them (iecDrive::open/read/write/close) needs
// ESP-IDF and real media; it is verified on hardware. What is covered here is
// everything that decides HOW MANY bytes move and WHAT BYTES go on the wire --
// which is where every bug so far has been.
//
//   pio test -e native -f native/test_console_dos

#include <unity.h>

#include <string>
#include <vector>

#include "../../../lib/console/dos_encode.h"
#include "../../../lib/console/dos_transfer.h"

using ESP32Console::encodeDosCommand;
using ESP32Console::dos_read_loop;
using ESP32Console::dos_write_loop;
using ESP32Console::DOS_CANCEL_INTERVAL;
using ESP32Console::DOS_CHUNK_MAX;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- helpers --

static std::string hexOf(const std::string &s)
{
    static const char *digits = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        out += digits[c >> 4];
        out += digits[c & 0x0F];
    }
    return out;
}

static void assertEncodes(const char *input, const char *expectedHex)
{
    std::string got = hexOf(encodeDosCommand(input));
    TEST_ASSERT_EQUAL_STRING(expectedHex, got.c_str());
}

// A reader that hands out a fixed byte sequence, `per_call` bytes at a time.
struct FakeReader
{
    std::vector<uint8_t> data;
    size_t pos = 0;
    uint8_t per_call;
    size_t calls = 0;

    FakeReader(size_t n, uint8_t per_call = DOS_CHUNK_MAX) : per_call(per_call)
    {
        data.resize(n);
        for (size_t i = 0; i < n; i++)
            data[i] = (uint8_t)(i & 0xFF);
    }

    uint8_t operator()(uint8_t *buf, uint8_t len)
    {
        calls++;
        size_t want = len < per_call ? len : per_call;
        size_t left = data.size() - pos;
        if (want > left) want = left;
        for (size_t i = 0; i < want; i++)
            buf[i] = data[pos + i];
        pos += want;
        return (uint8_t)want;
    }
};

// Collects everything the loop emits, plus the offset it was told.
struct FakeSink
{
    std::vector<uint8_t> bytes;
    std::vector<size_t> offsets;

    void operator()(const uint8_t *buf, size_t len, size_t offset)
    {
        offsets.push_back(offset);
        bytes.insert(bytes.end(), buf, buf + len);
    }
};

// -------------------------------------------------------------- encoding ---

// The bug this suite exists for: one "0x" must introduce a RUN of bytes.
// "m-r0x000005" used to encode as M-R, one 00, then the TEXT "0005", so the
// drive read address $3000 for 48 bytes and answered plausibly for the wrong
// place. "m-r" is 4D 2D 52 in PETSCII.
void test_hex_escape_carries_a_run_of_bytes(void)
{
    assertEncodes("m-r0x000009", "4D2D52" "000009");
}

// The per-byte form the help text has always advertised must survive: the run
// stops at 'x', which is not a hex digit, and the escape branch re-enters.
void test_per_byte_escapes_still_work_and_agree_with_the_run_form(void)
{
    assertEncodes("m-r0x000x000x09", "4D2D52" "000009");
    TEST_ASSERT_EQUAL_STRING(encodeDosCommand("m-r0x000009").c_str(),
                             encodeDosCommand("m-r0x000x000x09").c_str());
}

// A full M-W: address, count, then nine data bytes, all under one escape.
void test_a_long_run_encodes_every_pair(void)
{
    assertEncodes("m-w0x000009010203040506070809",
                  "4D2D57" "000009" "010203040506070809");
}

// The ambiguity the run form accepts, pinned so it is a decision and not a
// surprise: text starting with two hex digits is swallowed by the run.
void test_run_swallows_following_text_that_looks_like_hex(void)
{
    assertEncodes("0x00cd", "00CD");
}

// ...and text that cannot be read as a hex pair ends the run cleanly.
void test_run_stops_at_the_first_non_hex_character(void)
{
    // 'm' is not a hex digit, so the run is 00 00 and "meat" is text.
    assertEncodes("0x0000meat", "0000" "4D454154");
}

// An odd trailing digit is left as text rather than guessed at.
void test_odd_trailing_hex_digit_stays_text(void)
{
    assertEncodes("0x000", "00" "30");
}

// Too short to be an escape at all: three characters of plain text.
void test_short_escape_is_not_an_escape(void)
{
    TEST_ASSERT_EQUAL_size_t(3, encodeDosCommand("0x0").size());
}

// Lowercase input is what an unshifted C64 sends: a-z map onto $41-$5A.
// Uppercase would land in the shifted range and match no command.
void test_lowercase_text_maps_onto_the_unshifted_range(void)
{
    assertEncodes("n0:disk,id", "4E30" "3A" "4449534B" "2C" "4944");
    TEST_ASSERT_TRUE(encodeDosCommand("M-R") != encodeDosCommand("m-r"));
}

void test_empty_line_encodes_to_nothing(void)
{
    TEST_ASSERT_TRUE(encodeDosCommand("").empty());
}

// ------------------------------------------------------------- read loop ---

void test_read_loop_stops_at_end_of_file(void)
{
    FakeReader reader(600);
    FakeSink sink;
    bool cancelled = true;

    size_t n = dos_read_loop(0, reader, sink, [] { return false; }, &cancelled);

    TEST_ASSERT_EQUAL_size_t(600, n);
    TEST_ASSERT_EQUAL_size_t(600, sink.bytes.size());
    TEST_ASSERT_FALSE(cancelled);
}

void test_read_loop_honours_the_byte_limit(void)
{
    FakeReader reader(600);
    FakeSink sink;

    size_t n = dos_read_loop(100, reader, sink, [] { return false; });

    TEST_ASSERT_EQUAL_size_t(100, n);
    TEST_ASSERT_EQUAL_size_t(100, sink.bytes.size());
    // The limit is applied to the REQUEST, not by discarding an overshoot:
    // one call for 100 bytes, and the source still holds the other 500.
    TEST_ASSERT_EQUAL_size_t(1, reader.calls);
    TEST_ASSERT_EQUAL_size_t(100, reader.pos);
}

// The device contract: a read of 0 on the FIRST call is an error signal (file
// not found), not an empty file. The loop reports it as zero bytes with no
// cancel, and the caller distinguishes the two.
void test_read_loop_reports_an_immediate_zero_as_no_bytes(void)
{
    FakeReader reader(0);
    FakeSink sink;
    bool cancelled = true;

    size_t n = dos_read_loop(0, reader, sink, [] { return false; }, &cancelled);

    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_FALSE(cancelled);
    TEST_ASSERT_EQUAL_size_t(0, sink.offsets.size());
}

void test_read_loop_gives_the_sink_running_offsets(void)
{
    FakeReader reader(600);          // 255 + 255 + 90
    FakeSink sink;

    dos_read_loop(0, reader, sink, [] { return false; });

    TEST_ASSERT_EQUAL_size_t(3, sink.offsets.size());
    TEST_ASSERT_EQUAL_size_t(0, sink.offsets[0]);
    TEST_ASSERT_EQUAL_size_t(255, sink.offsets[1]);
    TEST_ASSERT_EQUAL_size_t(510, sink.offsets[2]);
}

// Cancel is consulted every DOS_CANCEL_INTERVAL bytes. Reading one byte per
// call puts the check on an exact boundary.
void test_read_loop_cancels_on_the_interval_boundary(void)
{
    FakeReader reader(10000, 1);
    FakeSink sink;
    bool cancelled = false;

    size_t n = dos_read_loop(0, reader, sink, [] { return true; }, &cancelled);

    TEST_ASSERT_EQUAL_size_t(DOS_CANCEL_INTERVAL, n);
    TEST_ASSERT_TRUE(cancelled);
}

// The discriminating half of the same rule: a transfer that ends one byte
// short of the interval never asks, so it reports end of file, not a cancel --
// even with the predicate wired permanently true.
void test_read_loop_does_not_cancel_below_the_interval(void)
{
    FakeReader reader(DOS_CANCEL_INTERVAL - 1, 1);
    FakeSink sink;
    bool cancelled = true;

    size_t n = dos_read_loop(0, reader, sink, [] { return true; }, &cancelled);

    TEST_ASSERT_EQUAL_size_t(DOS_CANCEL_INTERVAL - 1, n);
    TEST_ASSERT_FALSE(cancelled);
}

// ------------------------------------------------------------ write loop ---

struct FakeWriter
{
    size_t accepted = 0;
    size_t accept_limit;            // stop taking bytes past this point
    std::vector<uint8_t> bytes;
    std::vector<bool> eois;
    std::vector<uint8_t> lens;

    explicit FakeWriter(size_t limit = (size_t)-1) : accept_limit(limit) {}

    uint8_t operator()(const uint8_t *buf, uint8_t len, bool eoi)
    {
        eois.push_back(eoi);
        lens.push_back(len);

        size_t room = accept_limit > accepted ? accept_limit - accepted : 0;
        size_t take = len < room ? len : room;
        bytes.insert(bytes.end(), buf, buf + take);
        accepted += take;
        return (uint8_t)take;
    }
};

void test_write_loop_sends_everything_in_chunks(void)
{
    std::vector<uint8_t> data(600, 0xAB);
    FakeWriter writer;

    size_t n = dos_write_loop(data.data(), data.size(), writer, [] { return false; });

    TEST_ASSERT_EQUAL_size_t(600, n);
    TEST_ASSERT_EQUAL_size_t(600, writer.bytes.size());
    TEST_ASSERT_EQUAL_size_t(3, writer.lens.size());
    TEST_ASSERT_EQUAL_UINT8(DOS_CHUNK_MAX, writer.lens[0]);
    TEST_ASSERT_EQUAL_UINT8(DOS_CHUNK_MAX, writer.lens[1]);
    TEST_ASSERT_EQUAL_UINT8(90, writer.lens[2]);
}

// EOI marks the end of the transfer, so it belongs on the last chunk only.
void test_write_loop_sets_eoi_on_the_last_chunk_only(void)
{
    std::vector<uint8_t> data(600, 0xAB);
    FakeWriter writer;

    dos_write_loop(data.data(), data.size(), writer, [] { return false; });

    TEST_ASSERT_EQUAL_size_t(3, writer.eois.size());
    TEST_ASSERT_FALSE(writer.eois[0]);
    TEST_ASSERT_FALSE(writer.eois[1]);
    TEST_ASSERT_TRUE(writer.eois[2]);
}

void test_write_loop_sets_eoi_when_the_whole_write_is_one_chunk(void)
{
    std::vector<uint8_t> data(10, 0xAB);
    FakeWriter writer;

    dos_write_loop(data.data(), data.size(), writer, [] { return false; });

    TEST_ASSERT_EQUAL_size_t(1, writer.eois.size());
    TEST_ASSERT_TRUE(writer.eois[0]);
}

// Accepting fewer bytes than offered is the device's "cannot receive more
// data" signal: the transfer ends there rather than retrying.
void test_write_loop_stops_on_a_short_write(void)
{
    std::vector<uint8_t> data(600, 0xAB);
    FakeWriter writer(100);

    size_t n = dos_write_loop(data.data(), data.size(), writer, [] { return false; });

    TEST_ASSERT_EQUAL_size_t(100, n);
    TEST_ASSERT_EQUAL_size_t(1, writer.lens.size());
}

void test_write_loop_cancels_on_the_interval_boundary(void)
{
    std::vector<uint8_t> data(1000, 0xAB);
    FakeWriter writer;
    bool cancelled = false;

    // 255 written leaves pending below the interval; the second chunk crosses
    // it, so the cancel lands after 510 bytes.
    size_t n = dos_write_loop(data.data(), data.size(), writer, [] { return true; }, &cancelled);

    TEST_ASSERT_EQUAL_size_t(510, n);
    TEST_ASSERT_TRUE(cancelled);
}

void test_write_loop_of_nothing_writes_nothing(void)
{
    FakeWriter writer;
    bool cancelled = true;

    size_t n = dos_write_loop(nullptr, 0, writer, [] { return true; }, &cancelled);

    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_EQUAL_size_t(0, writer.lens.size());
    TEST_ASSERT_FALSE(cancelled);
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_hex_escape_carries_a_run_of_bytes);
    RUN_TEST(test_per_byte_escapes_still_work_and_agree_with_the_run_form);
    RUN_TEST(test_a_long_run_encodes_every_pair);
    RUN_TEST(test_run_swallows_following_text_that_looks_like_hex);
    RUN_TEST(test_run_stops_at_the_first_non_hex_character);
    RUN_TEST(test_odd_trailing_hex_digit_stays_text);
    RUN_TEST(test_short_escape_is_not_an_escape);
    RUN_TEST(test_lowercase_text_maps_onto_the_unshifted_range);
    RUN_TEST(test_empty_line_encodes_to_nothing);

    RUN_TEST(test_read_loop_stops_at_end_of_file);
    RUN_TEST(test_read_loop_honours_the_byte_limit);
    RUN_TEST(test_read_loop_reports_an_immediate_zero_as_no_bytes);
    RUN_TEST(test_read_loop_gives_the_sink_running_offsets);
    RUN_TEST(test_read_loop_cancels_on_the_interval_boundary);
    RUN_TEST(test_read_loop_does_not_cancel_below_the_interval);

    RUN_TEST(test_write_loop_sends_everything_in_chunks);
    RUN_TEST(test_write_loop_sets_eoi_on_the_last_chunk_only);
    RUN_TEST(test_write_loop_sets_eoi_when_the_whole_write_is_one_chunk);
    RUN_TEST(test_write_loop_stops_on_a_short_write);
    RUN_TEST(test_write_loop_cancels_on_the_interval_boundary);
    RUN_TEST(test_write_loop_of_nothing_writes_nothing);

    return UNITY_END();
}
