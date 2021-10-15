#include "U8Char.h"

// from https://style64.org/petscii/

// PETSCII table in UTF8
const char16_t U8Char::utf8map[] = {
//  ---0,  ---1,  ---2,  ---3,  ---4,  ---5,  ---6,  ---7,  ---8,  ---9,  --10,  --11,  --12,  --13,  --14,  --15    
       0,     0,     0,     3,     0,     0,     0,     0,     0,     0,     0,     0,     0,    10,     0,     0,
       0,     0,     0,     0,   0x8,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    0x20,  0x21,  0x22,  0x23,  0x24,  0x25,  0x26,  0x27,  0x28,  0x29,  0x2a,  0x2b,  0x2c,  0x2d,  0x2e,  0x2f, // punct
    0x30,  0x31,  0x32,  0x33,  0x34,  0x35,  0x36,  0x37,  0x38,  0x39,  0x3a,  0x3b,  0x3c,  0x3d,  0x3e,  0x3f, // numbers

    0x40,  
    0x61,  0x62,  0x63,  0x64,  0x65,  0x66,  0x67,  0x68,  0x69,  0x6a,  0x6b,  0x6c,  0x6d,  0x6e,  0x6f, // a-z
    0x70,  0x71,  0x72,  0x73,  0x74,  0x75,  0x76,  0x77,  0x78,  0x79,  0x7a,  
    0x5b,  0xa3,  0x5d,0x2191,0x2190,

  0x2500,  
    0x41,  0x42,  0x43,  0x44,  0x45,  0x46,  0x47,  0x48,  0x49,  0x4a,  0x4b,  0x4c,  0x4d,  0x4e,  0x4f, // A-Z
    0x50,  0x51,  0x52,  0x53,  0x54,  0x55,  0x56,  0x57,  0x58,  0x59,  0x5a,
  0x253c,  0x00,0x2502,  0x00,  0x00,

    0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,0x2028,  0x00,  0x00, // control codes
    0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00,

    0xa0,0x258c,0x2584,0x2594,0x2581,0x258e,0x2592,  0x00,  0x00,  0x00,  0x00,0x251c,0x2597,0x2514,0x2510,0x2582, // tables etc.
  0x250c,0x2534,0x252c,0x2524,0x258e,0x258d,  0x00,  0x00,  0x00,0x2583,0x2713,0x2596,0x259d,0x2518,0x2598,0x259a,
  0x2500,  0x41,  0x42,  0x43,  0x44,  0x45,  0x46,  0x47,  0x48,  0x49,  0x4a,  0x4b,  0x4c,  0x4d,  0x4e,  0x4f, // A-Z
    0x50,  0x51,  0x52,  0x53,  0x54,  0x55,  0x56,  0x57,  0x58,  0x59,  0x5a,0x253c,  0x00,0x2502,  0x00,  0x00, 
    0xa0,0x258c,0x2584,0x2594,0x2581,0x258e,0x2592,  0x00,  0x00,  0x00,  0x00,0x251c,0x2597,0x2514,0x2510,0x2582, // tables etc.
  0x250c,0x2534,0x252c,0x2524,0x258e,0x258d,  0x00,  0x00,  0x00,0x2583,0x2713,0x2596,0x259d,0x2518,0x2598,  0x00

};

void U8Char::fromUtf8Stream(std::istream* reader) {
    uint8_t byte = reader->get();
    if(byte<=0x7f) {
        ch = byte;
    }   
    else if((byte & 0b11100000) == 0b11000000) {
        uint16_t hi =  ((uint16_t)(byte & 0b1111)) << 6;
        uint16_t lo = (reader->get() & 0b111111);
        ch = hi | lo;
    }
    else if((byte & 0b11110000) == 0b11100000) {
        uint16_t hi = ((uint16_t)(byte & 0b111)) << 12;
        uint16_t mi = ((uint16_t)(reader->get() & 0b111111)) << 6;
        uint16_t lo = reader->get() & 0b111111;
        ch = hi | mi | lo;
    }
    else {
        ch = 0;
    }
};


std::string U8Char::toUtf8() {
    if(ch==0) {
        return std::string(1, missing);
    }
    else if(ch>=0x01 && ch<=0x7f) {
        return std::string(1, char(ch));
    }
    else if(ch>=0x80 && ch<=0x7ff) {
        auto upper = (ch>>6) & (0b11111 | 0b11000000); 
        char lower = ch & (0b111111 | 0b10000000); 
        char arr[] = { (char)upper, (char)lower, '\0'};
        return std::string(arr);
    }
    else {
        auto lower = (uint8_t)ch & (0b00111111 | 0b10000000);
        auto mid = (uint8_t)(ch>>6) & (0b00111111 | 0b10000000);
        auto hi = (uint8_t)(ch>>12) & (0b00111111 | 0b11100000);
        char arr[] = { (char)hi, (char)mid, (char)lower, '\0'};
        return std::string(arr);
    }
}

uint8_t U8Char::toPetscii() {
    for(int i = 0; i<256; i++) {
        if(utf8map[i]==ch)
            return i;
    }
    return missing;
}