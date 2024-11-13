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

#ifdef PARALLEL_BUS

#include "dolphindos.h"

#include "bus.h"
#include "_protocol.h"

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

using namespace Protocol;

// STEP 1: READY TO RECEIVE
// Sooner or later, the talker will want to talk, and send a character.
// When it's ready to go, it releases the Clock line to false.  This signal change might be
// translated as "I'm ready to send a character." The listener must detect this and 
// immediately start receiving data on both the clock and data lines.
uint8_t  DolphinDOS::receiveByte ()
{
    IEC.flags &= CLEAR_LOW;

    usleep( 65 );
    IEC_RELEASE ( PIN_IEC_DATA_OUT );
    usleep( 65 ); // Wait for 


    // STEP 2: RECEIVING THE BYTE
    uint8_t data = PARALLEL.readByte();

    // If clock is released this was last byte
    if ( !IEC_IS_ASSERTED(PIN_IEC_CLK_IN) )
        IEC.flags |= EOI_RECVD;

    // STEP 4: FRAME HANDSHAKE
    IEC_ASSERT ( PIN_IEC_DATA_OUT );

    return data;
} // receiveByte


// STEP 1: READY TO SEND
// Sooner or later, the talker will want to talk, and send a character.
// When it's ready to go, it releases the Clock line to false.  This signal change might be
// translated as "I'm ready to send a character." The listener must detect this and respond,
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's
// a printer chugging out a line of print, or a disk drive with a formatting job in progress,
// it might holdback for quite a while; there's no time limit.
bool DolphinDOS::sendByte ( uint8_t data, bool eoi )
{
    IEC.flags &= CLEAR_LOW;

    // Set the data on the user port
    PARALLEL.data = data;

    // Say we're ready
    IEC_RELEASE ( PIN_IEC_CLK_OUT );

    // STEP 5: START OVER
    if ( eoi )
        IEC_RELEASE ( PIN_IEC_CLK_OUT );

    return true;
} // sendByte

#endif // PARALLEL_BUS