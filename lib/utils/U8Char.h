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

// https://style64.org/petscii/
// https://www.punycoder.com/punycode/

#ifndef MEATLOAF_UTILS_U8CHAR
#define MEATLOAF_UTILS_U8CHAR

#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

/********************************************************
 * U8Char
 * 
 * A minimal wide char implementation that can handle UTF8
 * and convert it to PETSCII
 ********************************************************/

class U8Char {
    static const char16_t utf8map[];
    const char missing = '?';
    void fromUtf8Stream(std::istream* reader);
    // static std::once_flag ch_to_petascii_init_flag;

public:
    char16_t ch;
    U8Char(const uint16_t codepoint): ch(codepoint) {};
    // U8Char(const uint16_t codepoint): ch(codepoint) { ensureReverseMapInitialized(); };
    U8Char(std::istream* reader) {
        // ensureReverseMapInitialized();
        fromUtf8Stream(reader);
    }
    U8Char(const char petscii) : ch(utf8map[static_cast<uint8_t>(petscii)]) {
        // ensureReverseMapInitialized();
    }

    // static void ensureReverseMapInitialized() {
    //     std::call_once(ch_to_petascii_init_flag, initialize_ch_to_petascii_map);
    // }

    size_t fromCharArray(char* reader);

    std::string toUtf8();
    uint8_t toPetscii();
    size_t toUnicode32(std::string& input_utf8, uint32_t* output_unicode32, size_t max_output_length);
    std::string fromUnicode32(uint32_t* input_unicode32, size_t input_length);
    static std::string toPunycode(std::string utf8String);
    static std::string fromPunycode(std::string punycodeString);
    // ACE wrappers: encode input UTF-8 to ACE-prefixed punycode if non-ASCII chars present
    static std::string encodeACE(std::string utf8String);
    // Decode ACE-prefixed punycode (checks for 'xn--' prefix case-insensitively) and return decoded UTF-8 or original string
    static std::string decodeACE(std::string aceOrPunycode);

    // This is a reverse lookup map used to quickly find the petascci code from the ch value, making it O(1) complexity
    // static std::unordered_map<char16_t, uint8_t> ch_to_petascii_map;

    // static void initialize_ch_to_petascii_map() {
    //     for (size_t i = 0; i < 256; ++i) {
    //         ch_to_petascii_map.insert({utf8map[i], static_cast<uint8_t>(i)});
    //     }
    // }
};

#endif /* MEATLOAF_UTILS_U8CHAR */
