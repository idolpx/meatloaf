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

// Block command parameters (U1/U2, B-R/B-W/B-A/B-F/B-P/B-E), parsed the way
// VICE's vdrive_command_get_block_parameters() does: the parameters start after
// the colon, or after the command word when there is none; separators are
// space, comma, ')', '#' and $1D; each number is decimal. Returns the number
// of parameters found (0-4), or -1 when a parameter is not a number or a
// fifth one follows the fourth (CBM DOS answers 30 for both).
int parseBlockParams(const std::string& command, int out[4]);

#endif
