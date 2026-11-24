#include "open_spec.h"

OpenRequest parseOpenSpec(uint8_t channel, const std::string& rawIn)
{
    OpenRequest r;

    // iecDrive::open() receives a C string, so a NUL ends the spec.
    std::string raw = rawIn.substr(0, rawIn.find('\0'));

    // Fields are split at commas like util_tokenize(): no token after a trailing comma.
    size_t c1 = raw.find(',');
    size_t c2 = (c1 == std::string::npos) ? std::string::npos : raw.find(',', c1 + 1);

    std::string name = raw.substr(0, c1);
    std::string f1, f2;
    bool have_f2 = false;
    if (c1 != std::string::npos && c1 + 1 < raw.size())
        f1 = raw.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
    if (c2 != std::string::npos && c2 + 1 < raw.size())
    {
        have_f2 = true;
        size_t c3 = raw.find(',', c2 + 1);
        f2 = raw.substr(c2 + 1, c3 == std::string::npos ? std::string::npos : c3 - c2 - 1);
    }

    if (f1.size() == 1)
    {
        switch (f1[0])
        {
            case 'P': r.type = DosFileType::Prg; break;
            case 'S': r.type = DosFileType::Seq; break;
            case 'U': r.type = DosFileType::Usr; break;
            case 'L': r.type = DosFileType::Rel; break;
            case 'D': r.type = DosFileType::Del; break;
            default: break;
        }
    }

    // determine file mode (read/write)
    // A REL OPEN (,L) is never a write open, whatever the channel: the REL
    // path picks read-write itself, and as 'out' a missing file on channel 1
    // would be created as a plain file before the type check answered 64.
    std::ios_base::openmode mode = std::ios_base::in;
    if (channel == 1 && r.type != DosFileType::Rel) mode = std::ios_base::out;
    if (r.type == DosFileType::Rel)
    {
        // third field is the record-length byte, not a mode token
        if (have_f2) r.record_length = (uint8_t)raw[c2 + 1];
    }
    else if (have_f2)
    {
        if (f2 == "R") mode = std::ios_base::in;
        else if (f2 == "W") mode = std::ios_base::out;
    }

    // Network URLs need read-write on channels >= 2 (full-mode protocol).
    if (name.find("://") != std::string::npos && channel >= 2 &&
        mode != (std::ios_base::in | std::ios_base::out))
        mode = std::ios_base::in | std::ios_base::out;

    if (!name.empty() && name[0] == '@')
    {
        r.overwrite = true;
        name.erase(0, 1);
    }
    if (!name.empty() && name[0] == ':')
    {
        r.overwrite = true;
        name.erase(0, 1);
    }
    if (name.compare(0, 2, "0:") == 0)
        name.erase(0, 2);

    // "/DIR/:FILE" is relative to the current directory; keyed on "/:" so
    // host-absolute paths such as "/sd/x.d64" are untouched.
    if (name.size() > 1 && name[0] == '/' && name[1] != '/' &&
        name.find("/:") != std::string::npos)
        name.erase(0, 1);

    if (name.compare(0, 2, "CD") == 0 && name.size() > 2)
    {
        char c = name[2];
        if (c == ':' || c == '_' || c == '^' || c == '/')
            name.erase(0, 2);
    }

    // name ends at the first shifted space
    size_t i = name.find('\xa0');
    if (i != std::string::npos) name = name.substr(0, i);

    r.direct_access = !name.empty() && name[0] == '#' &&
                      name.find_first_not_of("0123456789", 1) == std::string::npos;

    r.name = name;
    r.mode = mode;
    return r;
}

bool parsePositionCommand(const uint8_t* data, uint8_t len, PositionCommand& out)
{
    // One trailing CR is always dropped first, as VICE's command dispatch does
    // (vdrive-command.c) - PRINT# appends one, so BASIC's P"CHR$(98)CHR$(R)
    // CHR$(0) arrives as 5 bytes whose last is the CR, not a position of 13.
    if (data != nullptr && len > 0 && data[len - 1] == 0x0D)
        len--;

    if (data == nullptr || len < 2 || data[0] != 'P')
        return false;

    out.channel = data[1] & 0x0F;
    out.record = 0;
    out.position = 1;

    // Parameters: byte 2 record lo, byte 3 record hi, byte 4 position.
    if (len > 2) out.record = data[2];
    if (len > 3) out.record |= (uint16_t)data[3] << 8;
    if (len > 4) out.position = data[4];
    return true;
}
