#include "unity.h"

#include "punycode.h"
#include <string>
// #include "../lib/utils/string_utils.cpp"

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
    // https://www.name.com/punycode-converter
    // https://r12a.github.io/app-conversion/
    // https://onlinetools.com/unicode/convert-unicode-to-hex

    //std::string chinese = "文件档案名";
    const uint32_t chineseAsUnicode[] = {0x6587, 0x4ef6, 0x6843, 0x684c, 0x540d};
    char asPunycode[256];
    size_t dstlen = sizeof asPunycode;
    // size_t punycode_encode(const uint32_t *const src, const size_t srclen, char *const dst, size_t *const dstlen)
    punycode_encode(chineseAsUnicode, 5, asPunycode, &dstlen);
    std::string punycode(asPunycode, dstlen);
    // Debug_printv("Chinese U32 as punycode:'%s'\n", punycode.c_str());
    // std::string asUnicode = U8Char::fromPunycode(punycode);
    // Debug_printv("Chinese UTF8 from the above punycode:'%s'\n", asUnicode.c_str());

//     std::string punycode2 = U8Char::toPunycode(asUnicode);
//     Debug_printv("Chinese text as from punycode again:'%s'\n", punycode2.c_str());


    // uint32_t asU32[1024];
    //char asPunycode[1024];
    //dstlen = sizeof asPunycode;
    // size_t n_converted;
    // U8Char temp(' ');

    // Debug_printv("Calling toUnicode32\n");
    // size_t conv_len = temp.toUnicode32(asUnicode, asU32, sizeof asU32);
    // Debug_printv("Conv len=%d, encoding now...\n", conv_len);
    // n_converted = punycode_encode(asU32, conv_len, asPunycode, &dstlen);    

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