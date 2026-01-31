#include <iostream>
#include <string>
#include <cassert>
#include "lib/utils/U8Char.h"

int main() {
    // Canonical check
    std::string s1 = U8Char::fromPunycode("bcher-kva");
    if (s1 != u8"bücher") { std::cerr << "canonical failed: " << s1 << std::endl; return 1; }

    // Chinese round-trip
    std::string chinese = u8"文件档案名";
    std::string puny = U8Char::toPunycode(chinese);
    if (puny.empty()) { std::cerr << "puny empty" << std::endl; return 2; }
    std::string dec = U8Char::fromPunycode(puny);
    if (dec != chinese) { std::cerr << "chinese roundtrip failed: got '" << dec << "'" << std::endl; return 3; }

    // Long repeated characters
    std::string long_e;
    for (int i = 0; i < 200; ++i) long_e += u8"é";
    std::string long_p = U8Char::toPunycode(long_e);
    if (long_p.empty()) { std::cerr << "long encode empty" << std::endl; return 4; }
    std::string long_dec = U8Char::fromPunycode(long_p);
    if (long_dec != long_e) { std::cerr << "long roundtrip failed" << std::endl; return 5; }

    std::cout << "punycode smoke tests passed" << std::endl;
    return 0;
}
