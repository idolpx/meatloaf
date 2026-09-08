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

#include "escape.h"

#ifdef ENABLE_MODEM

void EscapeDetector::configure(uint8_t esc_char, uint16_t guard_units)
{
    esc_char_ = esc_char;
    // One fiftieth of a second is 20 ms.
    guard_ms_ = (uint32_t)guard_units * 20u;
}

void EscapeDetector::reset()
{
    state_ = State::IDLE;
    held_ = 0;
    last_byte_ms_ = 0;
    seen_any_ = false;
}

void EscapeDetector::release(std::string &forward)
{
    for (uint8_t i = 0; i < held_; ++i)
        forward.push_back((char)esc_char_);
    held_ = 0;
    state_ = State::IDLE;
}

EscapeDetector::Verdict EscapeDetector::feed(uint8_t b, uint32_t now_ms,
                                             std::string &forward)
{
    // S2 above 127 disables the escape. Everything is data.
    if (esc_char_ > 127)
    {
        forward.push_back((char)b);
        last_byte_ms_ = now_ms;
        seen_any_ = true;
        return Verdict::NONE;
    }

    // Before any byte at all, treat the line as having been silent forever --
    // otherwise an escape typed as the very first thing after connect could
    // never start a candidate.
    uint32_t gap = seen_any_ ? (now_ms - last_byte_ms_) : guard_ms_;

    // If the trailing guard has already fully elapsed by the time this byte
    // arrives, the escape completed before tick() got a chance to say so --
    // this is just polling latency, not a reason to downgrade it. The escape
    // wins: the held bytes are consumed (never forwarded), and the racing
    // byte itself is dropped rather than reaching the remote, because by
    // Hayes semantics command mode has already taken over by the time it
    // arrived. This must be checked before the blanket release below, or a
    // completed escape would be silently turned back into data.
    if (state_ == State::TRAILING && gap >= guard_ms_)
    {
        held_ = 0;
        state_ = State::IDLE;
        last_byte_ms_ = now_ms;
        seen_any_ = true;
        return Verdict::ESCAPED;
    }

    // A gap at or past the guard retires whatever candidate was in progress:
    // those bytes were data after all. Do this before classifying the new byte
    // so their order is preserved.
    if (state_ != State::IDLE && gap >= guard_ms_)
        release(forward);

    if (b == esc_char_)
    {
        switch (state_)
        {
        case State::IDLE:
            // A candidate can only START after a full guard of silence.
            if (gap >= guard_ms_)
            {
                held_ = 1;
                state_ = State::COUNTING;
            }
            else
            {
                forward.push_back((char)b);
            }
            break;

        case State::COUNTING:
            ++held_;
            if (held_ == 3)
                state_ = State::TRAILING;
            break;

        case State::TRAILING:
            // A fourth character means this was never an escape.
            release(forward);
            forward.push_back((char)b);
            break;
        }
    }
    else
    {
        // Any non-escape byte ends a candidate; the held bytes were data.
        if (state_ != State::IDLE)
            release(forward);
        forward.push_back((char)b);
    }

    last_byte_ms_ = now_ms;
    seen_any_ = true;
    return Verdict::NONE;
}

EscapeDetector::Verdict EscapeDetector::tick(uint32_t now_ms,
                                             std::string &forward)
{
    if (state_ == State::IDLE || !seen_any_)
        return Verdict::NONE;

    if (now_ms - last_byte_ms_ < guard_ms_)
        return Verdict::NONE;

    if (state_ == State::TRAILING)
    {
        // The trailing guard completed: a real escape. The held bytes are
        // consumed, never forwarded.
        held_ = 0;
        state_ = State::IDLE;
        return Verdict::ESCAPED;
    }

    // One or two characters that timed out. They were data.
    release(forward);
    return Verdict::NONE;
}

#endif // ENABLE_MODEM
