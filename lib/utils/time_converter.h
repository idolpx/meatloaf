#ifndef TIMECONVERTER_H
#define TIMECONVERTER_H

#include <cstdint>
#include <ctime>

class TimeConverter
{
public:
    // Convert to ASCII time format: "SUN. 01/20/08 01:23:45 PM"+CHR$(13)
    // Returns the length of the data written to buffer, or 0 if error
    static size_t toAsciiTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize);

    // Convert to BCD time format: 9 bytes with BCD-encoded values
    // Returns the length of the data written to buffer (always 9 if successful), or 0 if error
    static size_t toBcdTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize);

    // Convert to Decimal time format: 9 bytes with decimal values
    // Returns the length of the data written to buffer (always 9 if successful), or 0 if error
    static size_t toDecimalTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize);

    // Convert to ISO 8601 subset format: "2008-01-20T13:23:45 SUN"+ASCII 13
    // Returns the length of the data written to buffer, or 0 if error
    static size_t toIsoTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize);

    // The T-W counterparts (sd2iec's T-WA/T-WI/T-WD/T-WB): parse the same
    // layouts back into a struct tm (local time, tm_isdst = -1). The binary
    // forms take the 8 bytes before the CR. False on anything out of range.
    static bool fromAsciiTime(const uint8_t* buf, size_t len, struct tm* out);
    static bool fromIsoTime(const uint8_t* buf, size_t len, struct tm* out);
    static bool fromDecimalTime(const uint8_t* buf, size_t len, struct tm* out);
    static bool fromBcdTime(const uint8_t* buf, size_t len, struct tm* out);

private:
    // Convert a decimal number to BCD (Binary Coded Decimal)
    static uint8_t toBcd(uint8_t decimal);

    // Day of week abbreviations for ASCII format
    static const char* DAY_NAMES_FULL[];
    
    // Day of week abbreviations for ISO format (shorter)
    static const char* DAY_NAMES_SHORT[];
};

#endif // TIMECONVERTER_H