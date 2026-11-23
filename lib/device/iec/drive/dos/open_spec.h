#ifndef IEC_DOS_OPEN_SPEC_H
#define IEC_DOS_OPEN_SPEC_H

// Pure parsers for the CBM DOS OPEN filename spec and the binary "P" (position)
// command. No device includes: this is compiled natively by test_dos_parse.

#include <cstdint>
#include <ios>
#include <string>

enum class DosFileType : char { Default = 0, Prg = 'P', Seq = 'S', Usr = 'U', Rel = 'L', Del = 'D' };

struct OpenRequest {
    std::string name;                 // exactly the string iecDrive::open() ends up with
    std::ios_base::openmode mode = std::ios_base::in;
    bool overwrite = false;
    DosFileType type = DosFileType::Default;
    uint8_t record_length = 0;        // ,L,<len> raw byte; 0 = not given
    bool direct_access = false;       // "#" / "#<n>"
};

OpenRequest parseOpenSpec(uint8_t channel, const std::string& raw);

struct PositionCommand { uint8_t channel; uint16_t record; uint8_t position; };

// data is the raw command bytes starting with 'P'. A trailing CR is dropped
// only when it is not a required parameter byte.
bool parsePositionCommand(const uint8_t* data, uint8_t len, PositionCommand& out);

#endif
