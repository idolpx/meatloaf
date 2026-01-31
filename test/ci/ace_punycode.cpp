#include <iostream>
#include <string>
#include <cassert>
#include "lib/utils/U8Char.h"

int main() {
    // ASCII passthrough
    std::string ascii = "example";
    std::string enc_ascii = U8Char::encodeACE(ascii);
    if (enc_ascii != ascii) { std::cerr << "ASCII passthrough failed: got '" << enc_ascii << "'" << std::endl; return 1; }

    // Non-ASCII -> punycode + xn-- prefix
    std::string nonascii = u8"bücher";
    std::string puny = U8Char::toPunycode(nonascii);
    if (puny.empty()) { std::cerr << "toPunycode returned empty" << std::endl; return 2; }
    std::string expected_ace = std::string("xn--") + puny;
    std::string enc = U8Char::encodeACE(nonascii);
    if (enc != expected_ace) { std::cerr << "encodeACE failed: got '" << enc << "' expected '" << expected_ace << "'" << std::endl; return 3; }

    // Decode ACE-prefixed string back to UTF-8
    std::string dec = U8Char::decodeACE(expected_ace);
    if (dec != nonascii) { std::cerr << "decodeACE failed: got '" << dec << "' expected '" << nonascii << "'" << std::endl; return 4; }

    // Case-insensitive prefix
    std::string upper_ace = std::string("XN--") + puny;
    std::string dec2 = U8Char::decodeACE(upper_ace);
    if (dec2 != nonascii) { std::cerr << "decodeACE case-insensitive failed: got '" << dec2 << "'" << std::endl; return 5; }

    // Non-prefixed decode returns original
    std::string plain = "plain";
    std::string decPlain = U8Char::decodeACE(plain);
    if (decPlain != plain) { std::cerr << "decodeACE passthrough failed: got '" << decPlain << "'" << std::endl; return 6; }

    std::cout << "ACE punycode tests passed" << std::endl;
    return 0;
}
