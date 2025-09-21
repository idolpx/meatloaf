// -----------------------------------------------------------------------------
// Copyright (C) 2024 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#include "GPIBDevice.h"
#include "GPIBBusHandler.h"

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "../../../include/esp-idf-arduino.h"
#endif

GPIBDevice::GPIBDevice(uint8_t devnr) 
{ 
  m_devnr      = devnr; 
  m_handler    = NULL;
  m_isActive   = true;
  m_flEnabled  = 0;
  m_flFlags    = 0;
  m_flProtocol = GPIB_FL_PROT_NONE;
}

void GPIBDevice::setDeviceNumber(uint8_t devnr)
{
  m_devnr = devnr;
}


void GPIBDevice::sendSRQ()
{
  if( m_handler ) m_handler->sendSRQ();
}




// default implementation of "buffer read" function which can/should be overridden
// (for efficiency) by devices using the JiffyDos, Epyx FastLoad or DolphinDos protocol
#if defined(GPIB_FP_JIFFY) || defined(GPIB_FP_EPYX) || defined(GPIB_FP_DOLPHIN) || defined(GPIB_FP_SPEEDDOS) || defined(GPIB_FP_FC3) || defined(GPIB_FP_AR6)
uint8_t GPIBDevice::read(uint8_t *buffer, uint8_t bufferSize)
{ 
  uint8_t i;
  for(i=0; i<bufferSize; i++)
    {
      int8_t n;
      while( (n = canRead())<0 );

      if( n==0 )
        break;
      else
        buffer[i] = read();
    }

  return i;
}
#endif


#if defined(GPIB_FP_DOLPHIN) || defined(GPIB_FP_FC3) || defined(GPIB_FP_AR6)
// default implementation of "buffer write" function which can/should be overridden
// (for efficiency) by devices using the DolphinDos protocol
uint8_t GPIBDevice::write(uint8_t *buffer, uint8_t bufferSize, bool eoi)
{
  uint8_t i;
  for(i=0; i<bufferSize; i++)
    {
      int8_t n;
      while( (n = canWrite())<0 );
      
      if( n==0 )
        break;
      else
        write(buffer[i], eoi && (i==bufferSize-1));
    }
  
  return i;
}
#endif
