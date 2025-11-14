#include "time_converter.h"
#include <cstring>
#include <stdio.h>

// Day of week abbreviations for ASCII format
const char* TimeConverter::DAY_NAMES_FULL[] = {
    "SUN.", "MON.", "TUES", "WED.", "THUR", "FRI.", "SAT."
};

// Day of week abbreviations for ISO format (shorter)
const char* TimeConverter::DAY_NAMES_SHORT[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

size_t TimeConverter::toAsciiTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize)
{
    if (!tinfo || !buf || bufSize < 25) return 0;
    
    int hour12 = tinfo->tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = (tinfo->tm_hour >= 12) ? "PM" : "AM";
    
    // Format: "SUN. 01/20/08 01:23:45 PM"+CHR$(13)
    size_t len = snprintf(reinterpret_cast<char*>(buf), bufSize, 
                         "%s %02d/%02d/%02d %02d:%02d:%02d %s%c",
                         DAY_NAMES_FULL[tinfo->tm_wday],
                         tinfo->tm_mon + 1,
                         tinfo->tm_mday,
                         tinfo->tm_year % 100,
                         hour12,
                         tinfo->tm_min,
                         tinfo->tm_sec,
                         ampm,
                         13); // CR
    
    return (len < bufSize) ? len : 0;
}

size_t TimeConverter::toBcdTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize)
{
    if (!tinfo || !buf || bufSize < 9) return 0;
    
    int hour12 = tinfo->tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    
    buf[0] = tinfo->tm_wday; // Day of week (0-6)
    buf[1] = toBcd(tinfo->tm_year % 100); // Year modulo 100 in BCD
    buf[2] = toBcd(tinfo->tm_mon + 1); // Month (1-12)
    buf[3] = toBcd(tinfo->tm_mday); // Day (1-31)
    buf[4] = toBcd(hour12); // Hour (1-12)
    buf[5] = toBcd(tinfo->tm_min); // Minute (0-59)
    buf[6] = toBcd(tinfo->tm_sec); // Second (0-59)
    buf[7] = (tinfo->tm_hour >= 12) ? 1 : 0; // AM/PM (0=AM, 1=PM)
    buf[8] = 13; // CR
    
    return 9;
}

size_t TimeConverter::toDecimalTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize)
{
    if (!tinfo || !buf || bufSize < 9) return 0;
    
    int hour12 = tinfo->tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    
    buf[0] = tinfo->tm_wday; // Day of week (0-6)
    buf[1] = tinfo->tm_year; // Year (-1900)
    buf[2] = tinfo->tm_mon + 1; // Month (1-12)
    buf[3] = tinfo->tm_mday; // Day (1-31)
    buf[4] = hour12; // Hour (1-12)
    buf[5] = tinfo->tm_min; // Minute (0-59)
    buf[6] = tinfo->tm_sec; // Second (0-59)
    buf[7] = (tinfo->tm_hour >= 12) ? 1 : 0; // AM/PM (0=AM, 1=PM)
    buf[8] = 13; // CR
    
    return 9;
}

size_t TimeConverter::toIsoTime(const struct tm* tinfo, uint8_t* buf, size_t bufSize)
{
    if (!tinfo || !buf || bufSize < 25) return 0;
    
    // Format: "2008-01-20T13:23:45 SUN"+ASCII 13
    size_t len = snprintf(reinterpret_cast<char*>(buf), bufSize,
                         "%04d-%02d-%02dT%02d:%02d:%02d %s%c",
                         tinfo->tm_year + 1900, // Full year
                         tinfo->tm_mon + 1,
                         tinfo->tm_mday,
                         tinfo->tm_hour,
                         tinfo->tm_min,
                         tinfo->tm_sec,
                         DAY_NAMES_SHORT[tinfo->tm_wday],
                         13); // CR
    
    return (len < bufSize) ? len : 0;
}

uint8_t TimeConverter::toBcd(uint8_t decimal)
{
    return ((decimal / 10) << 4) | (decimal % 10);
}