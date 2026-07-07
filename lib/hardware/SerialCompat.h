/*
 SerialCompat.h - Minimal Arduino-style Serial compatibility shim

 The console commands ported from Meatloaf/ESP32Console (lib/console/Commands)
 were written against Arduino's global `Serial` object. FujiNet doesn't have
 an Arduino core, so this provides just enough of the API (printf/print/println)
 for that ported code to compile and print to the console's stdout, mirroring
 the existing `EspClass ESP` compatibility shim in Esp.h.
 */

#ifndef SERIAL_COMPAT_H
#define SERIAL_COMPAT_H

#include <cstdarg>
#include <cstdio>
#include <string>

class SerialClass
{
public:
    int printf(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int ret = vprintf(fmt, args);
        va_end(args);
        return ret;
    }

    void print(const char *s) { fputs(s, stdout); }
    void print(const std::string &s) { fputs(s.c_str(), stdout); }

    void println(const char *s) { fputs(s, stdout); fputc('\n', stdout); }
    void println(const std::string &s) { fputs(s.c_str(), stdout); fputc('\n', stdout); }
    void println() { fputc('\n', stdout); }
};

inline SerialClass Serial;

#endif // SERIAL_COMPAT_H
