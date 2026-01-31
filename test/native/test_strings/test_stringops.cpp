#include "unity.h"

#include "punycode.h"
#include "lib/utils/U8Char.h"
#include <string>
#include "../lib/utils/string_utils.cpp"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_PetsciiUtf() {
    //petscii b0 -> 250c should be E2 94 8C
    //petscii b1 -> 2534 should be E2 94 B4
    std::string petchar = "\xb0";
    std::string utf8char = mstr::toUTF8(petchar);
    // print hex representation of utf8char
    // Debug_printv("Petscii b0 as UTF8: %x %x %x (should be e2 94 8c)\r\n", utf8char[0],utf8char[1],utf8char[2]);


    // should give:                        0x250c,0x2534,0x252c,0x2524,
    //std::string petscii = "Hello world! Some PETSCII: 1:\xb0 2:\xb1 3:\xb2 4:\xb3";
    std::string petscii = "fb64";
    std::string utf8 = mstr::toUTF8(petscii);
    std::string petscii2 = mstr::toPETSCII2(utf8);
    std::string utf8again = mstr::toUTF8(petscii2);
    // Debug_printv("Petscii: [%s]\r\n", petscii.c_str());
    // Debug_printv("UTF8: [%s] - should be: [\u250c\u2534\u252c\u2524]\r\n", utf8.c_str());
    // Debug_printv("And back to petscii: [%s]\r\n", petscii2.c_str());
    // Debug_printv("And back to utf8: [%s]\r\n", utf8again.c_str());
}

void test_Punycode() {
    // Canonical decode for known label "bücher" -> "bcher-kva"
    TEST_ASSERT_EQUAL_STRING("bücher", U8Char::fromPunycode("bcher-kva").c_str());

    // Strict encode should match canonical punycode
    TEST_ASSERT_EQUAL_STRING("bcher-kva", U8Char::toPunycode(u8"bücher").c_str());

    // Chinese round-trip (should round-trip; failure indicates a decode bug)
    std::string chinese = u8"文件档案名";
    std::string puny = U8Char::toPunycode(chinese);
    TEST_ASSERT_TRUE(puny.size() > 0);
    std::string decoded = U8Char::fromPunycode(puny);
    TEST_ASSERT_EQUAL_STRING(chinese.c_str(), decoded.c_str());

    // ASCII-only basic label should preserve value (note: punycode may append delimiter)
    TEST_ASSERT_EQUAL_STRING("example-", U8Char::toPunycode("example").c_str());
    TEST_ASSERT_EQUAL_STRING("example", U8Char::fromPunycode("example-").c_str());

    // Mixed Latin-1 (ñ) strict round-trip and canonical punycode
    TEST_ASSERT_EQUAL_STRING("maana-pta", U8Char::toPunycode(u8"mañana").c_str());
    TEST_ASSERT_EQUAL_STRING(u8"mañana", U8Char::fromPunycode(U8Char::toPunycode(u8"mañana")).c_str());

    // Long string of repeated non-ASCII characters should round-trip
    std::string long_e;
    for (int i = 0; i < 200; ++i) long_e += u8"é";
    std::string long_puny = U8Char::toPunycode(long_e);
    TEST_ASSERT_TRUE(long_puny.size() > 0);
    std::string long_dec = U8Char::fromPunycode(long_puny);
    TEST_ASSERT_EQUAL_STRING(long_e.c_str(), long_dec.c_str());

    // Non-BMP characters (emoji) are not representable by the current U8Char implementation;
    // ensure encoder/decoder do not crash and behavior is deterministic
    std::string emoji = "\xF0\x9F\x98\x8A"; // U+1F60A
    TEST_ASSERT_EQUAL_STRING("-", U8Char::toPunycode(emoji).c_str());
    TEST_ASSERT_EQUAL_STRING("", U8Char::fromPunycode("-").c_str());

    // Invalid bytes should be rejected by decoder
    TEST_ASSERT_EQUAL_STRING("", U8Char::fromPunycode("\xFF").c_str());

    // Empty handling
    TEST_ASSERT_EQUAL_STRING("", U8Char::toPunycode("").c_str());
    TEST_ASSERT_EQUAL_STRING("", U8Char::fromPunycode("").c_str());
}

void process()
{
    UNITY_BEGIN();

    RUN_TEST(test_PetsciiUtf);
    RUN_TEST(test_Punycode);

    UNITY_END();
}

void process()
{
    UNITY_BEGIN();

    RUN_TEST(test_PetsciiUtf);
    RUN_TEST(test_Punycode);

    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
}