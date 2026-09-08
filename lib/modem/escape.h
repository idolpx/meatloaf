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

// The +++ escape detector.
//
// The rule is not "three escape characters". It is: at least S12 of silence,
// then exactly three S2 characters each less than S12 apart, then at least S12
// of silence. Until that trailing guard expires the three bytes are HELD --
// if data follows, they were ordinary data and must be forwarded intact, in
// order. A user typing "+++" mid-sentence must neither drop into command mode
// nor lose the characters.
//
// S12 is in fiftieths of a second, per Hayes convention, so the default of 50
// is one second.
//
// The clock is injected, so this is a pure function of (bytes, timestamps) and
// is fully covered by test/native/test_modem_at.

#ifndef MEATLOAF_MODEM_ESCAPE
#define MEATLOAF_MODEM_ESCAPE

#ifdef ENABLE_MODEM

#include <cstdint>
#include <string>

class EscapeDetector
{
public:
    enum class Verdict
    {
        NONE,     // nothing to report; check `forward` for bytes to send on
        ESCAPED,  // a complete escape sequence completed its trailing guard
    };

    // guard_units is S12, in fiftieths of a second. An esc_char above 127
    // disables escape detection entirely, per the Hayes convention.
    void configure(uint8_t esc_char, uint16_t guard_units);

    // Clears held bytes and returns to the idle state. Call on connect and on
    // return to stream mode, so a sequence cannot span two sessions.
    void reset();

    // One byte arrived from the terminal at now_ms. Appends any bytes that
    // should reach the remote to `forward` (which is never cleared here, so a
    // caller may batch). Usually returns NONE -- tick() is what normally
    // completes a waiting escape. But if this byte arrives after the
    // trailing guard has already fully elapsed (tick() just hasn't been
    // polled yet), the escape has already won: this returns ESCAPED
    // immediately, and the byte itself is DROPPED -- not forwarded, not held
    // -- because by the time it arrived the modem had already switched to
    // command mode.
    Verdict feed(uint8_t b, uint32_t now_ms, std::string &forward);

    // No byte arrived. Call this regularly -- it is what completes the
    // trailing guard and what releases held bytes that turned out to be data.
    Verdict tick(uint32_t now_ms, std::string &forward);

private:
    enum class State
    {
        IDLE,      // no candidate in progress
        COUNTING,  // one or two escape characters held
        TRAILING,  // three held, waiting out the trailing guard
    };

    void release(std::string &forward);

    uint8_t  esc_char_     = 43;   // '+'
    uint32_t guard_ms_     = 1000; // S12 default of 50 fiftieths
    State    state_        = State::IDLE;
    uint8_t  held_         = 0;    // escape characters currently held (0-3)
    uint32_t last_byte_ms_ = 0;    // when the last byte of ANY kind arrived
    bool     seen_any_     = false;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_ESCAPE
