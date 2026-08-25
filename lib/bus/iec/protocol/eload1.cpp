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
// -----------------------------------------------------------------------------
//
// ELoad version 1. Ported from sd2iec's fl-eload.c (Ingo Korb, GPL v2).
//
// ELoad speaks ULoad Model 3's bit protocol -- see uload3.cpp, which owns the
// byte routines -- but asks for a FILE rather than a block chain, and the file
// is already open on channel 0 by the time the M-E arrives. So this loader
// never touches the sector hooks: it reads through the normal device
// interface, which means it works on any media Meatloaf can mount, not just on
// something with a track and a sector.
//
// Command 1 loads. Each block goes out as a count byte and then that many data
// bytes; a count of 0 marks the end of the file and 0xFF an error. Anything
// else is answered with 0xFF.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_ELOAD1) && defined(IEC_IMPL_SOFTLOAD)

bool IECBusHandler::runELoad1Loader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  while( true )
    {
      uint8_t cmd;
      noInterrupts();
      bool ok = receiveULoad3Byte(cmd);
      interrupts();
      if( !ok ) break;   // ATN: the computer is done with us

      if( cmd==1 )
        {
          while( true )
            {
              m_currentDevice->talk(0);
              m_inTask = false;
              uint8_t n = m_currentDevice->read(m_buffer, 254);
              m_inTask = true;

              bool last = (n < 254);

              noInterrupts();
              ok = transmitULoad3Byte(n);
              for(uint8_t i=0; i<n && ok; i++)
                ok = transmitULoad3Byte(m_buffer[i]);
              interrupts();
              if( !ok ) { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }

              if( last )
                {
                  noInterrupts();
                  transmitULoad3Byte(0);   // end of file
                  interrupts();

                  // close the file -- ELoad sends no CLOSE of its own
                  m_currentDevice->listen(0xE0);
                  m_currentDevice->unlisten();
                  break;
                }
            }
        }
      else
        {
          noInterrupts();
          transmitULoad3Byte(0xFF);
          interrupts();
        }
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
