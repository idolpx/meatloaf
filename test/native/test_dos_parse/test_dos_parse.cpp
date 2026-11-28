#include <unity.h>
#include <string>

#include "../../../lib/device/iec/drive/dos/open_spec.cpp"

void setUp(void) {}
void tearDown(void) {}

using std::ios_base;
static OpenRequest P(uint8_t ch, const char *s) { return parseOpenSpec(ch, std::string(s)); }

void test_mode_channel1_is_out(void) { TEST_ASSERT_TRUE(P(1, "FOO").mode == ios_base::out); }
void test_mode_default_in(void) { TEST_ASSERT_TRUE(P(2, "FOO").mode == ios_base::in); }
void test_mode_w(void) { TEST_ASSERT_TRUE(P(2, "FOO,S,W").mode == ios_base::out); }
void test_mode_r_on_channel1(void) { TEST_ASSERT_TRUE(P(1, "FOO,S,R").mode == ios_base::in); }
void test_mode_r(void) { TEST_ASSERT_TRUE(P(2, "FOO,S,R").mode == ios_base::in); }
void test_url_modes(void)
{
    TEST_ASSERT_TRUE(P(2, "http://x/y").mode == (ios_base::in | ios_base::out));
    TEST_ASSERT_TRUE(P(0, "http://x/y").mode == ios_base::in);
}
void test_overwrite_prefixes(void)
{
    const char *v[] = {"@0:FOO", ":FOO", "@:FOO"};
    for (const char *s : v) {
        OpenRequest r = P(2, s);
        TEST_ASSERT_TRUE(r.overwrite);
        TEST_ASSERT_EQUAL_STRING("FOO", r.name.c_str());
    }
    OpenRequest r = P(2, "0:FOO");
    TEST_ASSERT_FALSE(r.overwrite);
    TEST_ASSERT_EQUAL_STRING("FOO", r.name.c_str());
}
void test_leading_slash(void)
{
    TEST_ASSERT_EQUAL_STRING("D/:F", P(2, "/D/:F").name.c_str());
    TEST_ASSERT_EQUAL_STRING("/sd/x.d64", P(2, "/sd/x.d64").name.c_str());
}
void test_cd_prefix(void)
{
    TEST_ASSERT_EQUAL_STRING(":X", P(2, "CD:X").name.c_str());
    TEST_ASSERT_EQUAL_STRING("CDROM", P(2, "CDROM").name.c_str());
}
void test_shifted_space(void) { TEST_ASSERT_EQUAL_STRING("AB", P(2, "AB\xa0" "CD").name.c_str()); }
void test_direct_access(void)
{
    TEST_ASSERT_TRUE(P(2, "#").direct_access);
    TEST_ASSERT_TRUE(P(2, "#3").direct_access);
    TEST_ASSERT_FALSE(P(2, "#A").direct_access);
    TEST_ASSERT_EQUAL_STRING("#3", P(2, "#3").name.c_str());
}
void test_rel_length(void)
{
    OpenRequest r = P(2, "DATA,L,\x0a");
    TEST_ASSERT_TRUE(r.type == DosFileType::Rel);
    TEST_ASSERT_EQUAL_UINT8(10, r.record_length);
    TEST_ASSERT_EQUAL_STRING("DATA", r.name.c_str());
}
void test_rel_length_special_bytes(void)
{
    TEST_ASSERT_EQUAL_UINT8(44, P(2, "DATA,L,\x2c").record_length);
    TEST_ASSERT_EQUAL_UINT8(13, P(2, "DATA,L,\x0d").record_length);
}
void test_rel_no_length(void)
{
    OpenRequest r = P(2, "DATA,L");
    TEST_ASSERT_TRUE(r.type == DosFileType::Rel);
    TEST_ASSERT_EQUAL_UINT8(0, r.record_length);
}
void test_rel_length_is_not_mode(void)
{
    // 0x52 == 'R', 0x57 == 'W': must not switch mode
    TEST_ASSERT_TRUE(P(1, "DATA,L,\x52").mode == ios_base::in);
    TEST_ASSERT_TRUE(P(2, "DATA,L,\x57").mode == ios_base::in);
}
void test_rel_on_channel1_is_not_a_write_open(void)
{
    // ,L goes down the REL path on any channel; channel 1's implied write
    // mode would create a plain file for a missing name before answering 64.
    TEST_ASSERT_TRUE(P(1, "DATA,L,\x14").mode == ios_base::in);
    TEST_ASSERT_TRUE(P(1, "DATA,L").mode == ios_base::in);
    TEST_ASSERT_TRUE(P(1, "DATA,S").mode == ios_base::out);
}
void test_types(void)
{
    TEST_ASSERT_TRUE(P(2, "A,P").type == DosFileType::Prg);
    TEST_ASSERT_TRUE(P(2, "A,S").type == DosFileType::Seq);
    TEST_ASSERT_TRUE(P(2, "A,U").type == DosFileType::Usr);
    TEST_ASSERT_TRUE(P(2, "A,D").type == DosFileType::Del);
    TEST_ASSERT_TRUE(P(2, "A,X").type == DosFileType::Default);
    TEST_ASSERT_TRUE(P(2, "A").type == DosFileType::Default);
}
void test_empty(void) { TEST_ASSERT_EQUAL_STRING("", P(2, "").name.c_str()); }

// Field splitting matches util_tokenize() (getline on ','): an empty middle
// field still counts, and nothing follows a trailing comma.
void test_tokenizer_parity(void)
{
    OpenRequest r = P(2, "A,,W");
    TEST_ASSERT_TRUE(r.mode == ios_base::out);
    TEST_ASSERT_EQUAL_STRING("A", r.name.c_str());
    TEST_ASSERT_TRUE(P(2, "A,S,").mode == ios_base::in);
    TEST_ASSERT_EQUAL_STRING("", P(2, ",X").name.c_str());
}

static bool PC(const char *s, size_t n, PositionCommand &o) { return parsePositionCommand((const uint8_t *)s, (uint8_t)n, o); }
void test_p_full(void)
{
    PositionCommand c;
    TEST_ASSERT_TRUE(PC("P\x62\x05\x00\x03", 5, c));
    TEST_ASSERT_EQUAL_UINT8(2, c.channel);
    TEST_ASSERT_EQUAL_UINT16(5, c.record);
    TEST_ASSERT_EQUAL_UINT8(3, c.position);
}
void test_p_defaults(void)
{
    PositionCommand c;
    TEST_ASSERT_TRUE(PC("P\x62\x05", 3, c));
    TEST_ASSERT_EQUAL_UINT8(1, c.position);
    TEST_ASSERT_EQUAL_UINT16(5, c.record);
    TEST_ASSERT_TRUE(PC("P\x62\x05\x01", 4, c));
    TEST_ASSERT_EQUAL_UINT16(0x105, c.record);
    TEST_ASSERT_TRUE(PC("P\x62", 2, c));
    TEST_ASSERT_EQUAL_UINT16(0, c.record);
}
void test_p_cr(void)
{
    PositionCommand c;
    TEST_ASSERT_TRUE(PC("P\x62\x0d\x00\x01\x0d", 6, c));
    TEST_ASSERT_EQUAL_UINT16(13, c.record);
    TEST_ASSERT_EQUAL_UINT8(1, c.position);
    // BASIC's PRINT#15,"P"CHR$(98)CHR$(1)CHR$(0): the CR PRINT# appends is
    // dropped, not read as position 13 (VICE vdrive-command.c).
    TEST_ASSERT_TRUE(PC("P\x62\x01\x00\x0d", 5, c));
    TEST_ASSERT_EQUAL_UINT16(1, c.record);
    TEST_ASSERT_EQUAL_UINT8(1, c.position);
    // Only ONE trailing CR is dropped: a record-lo of 13 followed by CR stays 13.
    TEST_ASSERT_TRUE(PC("P\x62\x0d\x0d", 4, c));
    TEST_ASSERT_EQUAL_UINT16(13, c.record);
    TEST_ASSERT_EQUAL_UINT8(1, c.position);
    // "P" + CR alone has no channel.
    TEST_ASSERT_FALSE(PC("P\x0d", 2, c));
}
void test_p_bad(void)
{
    PositionCommand c;
    TEST_ASSERT_FALSE(PC("P", 1, c));
}


static int BP(const char *s, int out[4]) { out[0]=out[1]=out[2]=out[3]=-7; return parseBlockParams(std::string(s), out); }
void test_bp_spaces(void)
{
    int v[4]; TEST_ASSERT_EQUAL(4, BP("U1 2 0 18 1", v));
    TEST_ASSERT_EQUAL(2, v[0]); TEST_ASSERT_EQUAL(0, v[1]); TEST_ASSERT_EQUAL(18, v[2]); TEST_ASSERT_EQUAL(1, v[3]);
}
void test_bp_colon_commas(void)
{
    int v[4]; TEST_ASSERT_EQUAL(4, BP("U1:2,0,18,1", v));
    TEST_ASSERT_EQUAL(18, v[2]); TEST_ASSERT_EQUAL(1, v[3]);
    TEST_ASSERT_EQUAL(4, BP("B-R:2, 0, 17, 09", v));
    TEST_ASSERT_EQUAL(17, v[2]); TEST_ASSERT_EQUAL(9, v[3]);
}
void test_bp_basic_str(void)
{
    // PRINT#15,"U1";2;0;T;S -> numbers carry a leading space and a trailing one
    int v[4]; TEST_ASSERT_EQUAL(4, BP("U1 2  0  18  1 ", v));
    TEST_ASSERT_EQUAL(1, v[3]);
}
void test_bp_long_word(void)
{
    int v[4]; TEST_ASSERT_EQUAL(4, BP("BLOCK-READ 2 0 18 1", v));
    TEST_ASSERT_EQUAL(18, v[2]);
}
void test_bp_fewer(void)
{
    int v[4]; TEST_ASSERT_EQUAL(2, BP("B-P 2 5", v));
    TEST_ASSERT_EQUAL(2, v[0]); TEST_ASSERT_EQUAL(5, v[1]);
    TEST_ASSERT_EQUAL(0, BP("U1", v));
}
void test_bp_bad(void)
{
    int v[4];
    TEST_ASSERT_EQUAL(-1, BP("U1:2 0 50 E0", v));
    TEST_ASSERT_EQUAL(-1, BP("U1:2 0 18 1X", v));
    TEST_ASSERT_EQUAL(4, BP("U1:2 0 18 1 7", v));   // VICE ignores a separated fifth
    TEST_ASSERT_EQUAL(-1, BP("B-P 2 X", v));
}
void test_bp_hash_and_paren(void)
{
    int v[4]; TEST_ASSERT_EQUAL(4, BP("B-A:#0)18,1,", v) + 1); // 3 params + 1
    TEST_ASSERT_EQUAL(0, v[0]); TEST_ASSERT_EQUAL(18, v[1]); TEST_ASSERT_EQUAL(1, v[2]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_mode_channel1_is_out); RUN_TEST(test_mode_default_in); RUN_TEST(test_mode_w);
    RUN_TEST(test_mode_r_on_channel1); RUN_TEST(test_mode_r); RUN_TEST(test_url_modes);
    RUN_TEST(test_overwrite_prefixes); RUN_TEST(test_leading_slash); RUN_TEST(test_cd_prefix);
    RUN_TEST(test_shifted_space); RUN_TEST(test_direct_access); RUN_TEST(test_rel_length);
    RUN_TEST(test_rel_length_special_bytes); RUN_TEST(test_rel_no_length);
    RUN_TEST(test_rel_length_is_not_mode); RUN_TEST(test_rel_on_channel1_is_not_a_write_open); RUN_TEST(test_types); RUN_TEST(test_empty);
    RUN_TEST(test_tokenizer_parity);
    RUN_TEST(test_p_full); RUN_TEST(test_p_defaults); RUN_TEST(test_p_cr); RUN_TEST(test_p_bad);
    RUN_TEST(test_bp_spaces); RUN_TEST(test_bp_colon_commas); RUN_TEST(test_bp_basic_str); RUN_TEST(test_bp_long_word);
    RUN_TEST(test_bp_fewer); RUN_TEST(test_bp_bad); RUN_TEST(test_bp_hash_and_paren);
    return UNITY_END();
}
