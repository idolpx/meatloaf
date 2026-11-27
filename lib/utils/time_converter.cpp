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
// --- T-W: the parsers -------------------------------------------------------

static bool tc_fill(struct tm* out, int year, int mon, int mday, int hour, int min, int sec)
{
    if (mon < 1 || mon > 12 || mday < 1 || mday > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60)
        return false;
    memset(out, 0, sizeof(*out));
    out->tm_year = year - 1900;
    out->tm_mon = mon - 1;
    out->tm_mday = mday;
    out->tm_hour = hour;
    out->tm_min = min;
    out->tm_sec = sec;
    out->tm_isdst = -1;
    return true;
}

// 12-hour clock to 24: 12 AM is midnight, 12 PM is noon.
static int tc_hour24(int hour12, bool pm)
{
    if (hour12 < 1 || hour12 > 12) return -1;
    return (hour12 % 12) + (pm ? 12 : 0);
}

static int tc_fromBcd(uint8_t b)
{
    if ((b >> 4) > 9 || (b & 0x0F) > 9) return -1;
    return (b >> 4) * 10 + (b & 0x0F);
}

// Two-digit years: 80-99 are 19xx, 00-79 20xx (the CMD RTC convention).
static int tc_year2(int yy) { return yy < 80 ? 2000 + yy : 1900 + yy; }

bool TimeConverter::fromAsciiTime(const uint8_t* buf, size_t len, struct tm* out)
{
    // "SUN. 01/20/08 01:23:45 PM": the day name is four characters
    char s[40];
    if (!buf || !out || len < 5 || len >= sizeof(s)) return false;
    memcpy(s, buf, len);
    s[len] = 0;
    int mon, mday, yy, h, mi, se;
    char ap[3] = { 0 };
    if (sscanf(s + 4, " %d/%d/%d %d:%d:%d %2s", &mon, &mday, &yy, &h, &mi, &se, ap) != 7)
        return false;
    bool pm = (ap[0] == 'P' || ap[0] == 'p');
    if (!pm && ap[0] != 'A' && ap[0] != 'a') return false;
    int h24 = tc_hour24(h, pm);
    return h24 >= 0 && tc_fill(out, tc_year2(yy), mon, mday, h24, mi, se);
}

bool TimeConverter::fromIsoTime(const uint8_t* buf, size_t len, struct tm* out)
{
    // "2008-01-20T13:23:45 SUN"; the day name is optional
    char s[40];
    if (!buf || !out || len >= sizeof(s)) return false;
    memcpy(s, buf, len);
    s[len] = 0;
    int y, mon, mday, h, mi, se;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mon, &mday, &h, &mi, &se) != 6)
        return false;
    return tc_fill(out, y, mon, mday, h, mi, se);
}

bool TimeConverter::fromDecimalTime(const uint8_t* buf, size_t len, struct tm* out)
{
    // weekday, year-1900, month, day, hour (1-12), minute, second, AM/PM
    if (!buf || !out || len < 8) return false;
    int h24 = tc_hour24(buf[4], buf[7] != 0);
    return h24 >= 0 && tc_fill(out, 1900 + buf[1], buf[2], buf[3], h24, buf[5], buf[6]);
}

bool TimeConverter::fromBcdTime(const uint8_t* buf, size_t len, struct tm* out)
{
    // weekday, then year (mod 100), month, day, hour (1-12), minute, second
    // in BCD, then AM/PM
    if (!buf || !out || len < 8) return false;
    int yy = tc_fromBcd(buf[1]), mon = tc_fromBcd(buf[2]), mday = tc_fromBcd(buf[3]);
    int h = tc_fromBcd(buf[4]), mi = tc_fromBcd(buf[5]), se = tc_fromBcd(buf[6]);
    if (yy < 0 || mon < 0 || mday < 0 || h < 0 || mi < 0 || se < 0) return false;
    int h24 = tc_hour24(h, buf[7] != 0);
    return h24 >= 0 && tc_fill(out, tc_year2(yy), mon, mday, h24, mi, se);
}
