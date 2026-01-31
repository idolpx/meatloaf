#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include "lib/utils/U8Char.h"

// Write a simple PETSCII file containing bytes 0..255
static bool write_sample_petscii(const std::string &path) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    for (int i = 0; i < 256; ++i) {
        unsigned char b = static_cast<unsigned char>(i);
        f.put((char)b);
    }
    f.close();
    return true;
}

// Convert a PETSCII-encoded file (raw bytes) to UTF-8 and save
static bool petsciiToUtf8File(const std::string &inPath, const std::string &outPath) {
    std::ifstream in(inPath, std::ios::binary);
    if (!in.is_open()) return false;
    std::string petscii((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::string utf8;
    for (size_t i = 0; i < petscii.size(); ++i) {
        uint8_t p = static_cast<uint8_t>(petscii[i]);
        U8Char u((char)p);
        utf8 += u.toUtf8();
    }

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(utf8.data(), utf8.size());
    out.close();
    return true;
}

// Convert a UTF-8 file back to PETSCII raw bytes and save
static bool utf8ToPetsciiFile(const std::string &inPath, const std::string &outPath) {
    std::ifstream in(inPath, std::ios::binary);
    if (!in.is_open()) return false;
    std::string utf8((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::vector<unsigned char> petscii;
    const char *ptr = utf8.c_str();
    const char *end = ptr + utf8.size();
    while (ptr < end) {
        U8Char ch(' ');
        size_t skip = ch.fromCharArray((char*)ptr);
        uint8_t p = ch.toPetscii();
        petscii.push_back(p);
        ptr += skip;
    }

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write((char*)petscii.data(), petscii.size());
    out.close();
    return true;
}

static void compare_files_hex(const std::string &aPath, const std::string &bPath) {
    std::ifstream a(aPath, std::ios::binary), b(bPath, std::ios::binary);
    if (!a.is_open() || !b.is_open()) {
        std::cout << "Could not open files for comparison" << std::endl;
        return;
    }
    std::vector<unsigned char> va((std::istreambuf_iterator<char>(a)), {});
    std::vector<unsigned char> vb((std::istreambuf_iterator<char>(b)), {});
    a.close(); b.close();

    size_t n = std::min(va.size(), vb.size());
    size_t diffs = 0;
    for (size_t i = 0; i < n; ++i) {
        if (va[i] != vb[i]) {
            if (diffs < 20)
                std::cout << "diff @ " << i << ": orig=0x" << std::hex << (int)va[i] << " new=0x" << (int)vb[i] << std::dec << "\n";
            ++diffs;
        }
    }
    if (va.size() != vb.size()) {
        std::cout << "Size mismatch: orig=" << va.size() << " new=" << vb.size() << std::endl;
    }
    if (diffs == 0) std::cout << "Files identical" << std::endl;
    else std::cout << "Total diffs: " << diffs << std::endl;
}

int main() {
    const std::string orig = "petscii_sample.bin";
    const std::string utf8file = "petscii_as_utf8.txt";
    const std::string round = "petscii_roundtrip.bin";

    // if (!write_sample_petscii(orig)) {
    //     std::cerr << "Failed to write sample PETSCII file" << std::endl;
    //     return 1;
    // }
    // std::cout << "Wrote sample PETSCII file: " << orig << std::endl;

    if (!petsciiToUtf8File(orig, utf8file)) {
        std::cerr << "Conversion PETSCII->UTF8 failed" << std::endl;
        return 2;
    }
    {
        std::ifstream fs(utf8file, std::ios::binary);
        fs.seekg(0, std::ios::end);
        std::streamoff fsize = fs.tellg();
        fs.close();
        std::cout << "Converted to UTF8: " << utf8file << " (size " << fsize << ")" << std::endl;
    }

    if (!utf8ToPetsciiFile(utf8file, round)) {
        std::cerr << "Conversion UTF8->PETSCII failed" << std::endl;
        return 3;
    }
    std::cout << "Converted back to PETSCII: " << round << std::endl;

    compare_files_hex(orig, round);

    return 0;
}
