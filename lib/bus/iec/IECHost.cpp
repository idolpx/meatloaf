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
// You should have receikved a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#include "IECDevice.h"
#include "IECBusHandler.h"

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "IECespidf.h"
#endif

IECDevice::IECDevice(uint8_t devnr) 
{ 
  m_devnr   = devnr; 
  m_handler = NULL;
  m_sflags  = 0;
  m_isActive = true;
}

void IECDevice::setDeviceNumber(uint8_t devnr)
{
  m_devnr = devnr;
}


void IECDevice::sendSRQ() 
{ 
  if( m_handler ) m_handler->sendSRQ(); 
}


#ifdef SUPPORT_JIFFY
bool IECDevice::enableJiffyDosSupport(bool enable)
{
  return m_handler ? m_handler->enableJiffyDosSupport(this, enable) : false;
}
#endif


#ifdef SUPPORT_DOLPHIN 
bool IECDevice::enableDolphinDosSupport(bool enable)
{
  return m_handler ? m_handler->enableDolphinDosSupport(this, enable) : false;
}

void IECDevice::enableDolphinBurstMode(bool enable)
{
  if( m_handler ) m_handler->enableDolphinBurstMode(this, enable);
}

void IECDevice::dolphinBurstReceiveRequest()
{
  if( m_handler ) m_handler->dolphinBurstReceiveRequest(this);
}

void IECDevice::dolphinBurstTransmitRequest()
{
  if( m_handler ) m_handler->dolphinBurstTransmitRequest(this);
}
#endif

#ifdef SUPPORT_EPYX
bool IECDevice::enableEpyxFastLoadSupport(bool enable)
{
  return m_handler ? m_handler->enableEpyxFastLoadSupport(this, enable) : false;
}

void IECDevice::epyxLoadRequest()
{
  if( m_handler ) m_handler->epyxLoadRequest(this);
}

#endif  


// default implementation of "buffer read" function which can/should be overridden
// (for efficiency) by devices using the JiffyDos, Epyx FastLoad or DolphinDos protocol
#if defined(SUPPORT_JIFFY) || defined(SUPPORT_EPYX) || defined(SUPPORT_DOLPHIN)
uint8_t IECDevice::read(uint8_t *buffer, uint8_t bufferSize)
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


#if defined(SUPPORT_DOLPHIN)
// default implementation of "buffer write" function which can/should be overridden
// (for efficiency) by devices using the DolphinDos protocol
uint8_t IECDevice::write(uint8_t *buffer, uint8_t bufferSize, bool eoi)
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


// bool iecHost::deviceExists(uint8_t deviceID)
// {
//     bool device_status = false;

//     Debug_printf("device [%d] ", deviceID);

//     IEC.enabled = false; // Disable IEC Service loop

//     // Get Bus Attention
//     protocol->pull(PIN_IEC_ATN);
//     protocol->pull(PIN_IEC_CLK_OUT);
//     protocol->release(PIN_IEC_DATA_OUT);
//     delayMicroseconds(5);

//     // Wait for listeners to be ready
//     if(protocol->timeoutWait(PIN_IEC_DATA_IN, PULLED) == TIMED_OUT)
//     {
//         Debug_println(" inactive - bus empty");
//         device_status = false;
//     }
//     else
//     {
//         // Send Listen Command & Device ID
//         Debug_printf( "%.2X", (IEC_LISTEN & deviceID));
//         send( IEC_LISTEN & deviceID );
//         delayMicroseconds(TIMING_Tbb);

//         // If Data line is being pulled the device id is active on the bus
//         if ( protocol->status( PIN_IEC_DATA_IN ) )
//         {
//             device_status = true;
//             Debug_println("active");
//         }
//         else
//         {
//             device_status = false;
//             Debug_println("inactive");
//         }

//         // Send UnListen
//         send( IEC_UNLISTEN );
//         delayMicroseconds(TIMING_Tbb);
//     }

//     // Release ATN and Clock
//     protocol->release(PIN_IEC_ATN);
//     protocol->release(PIN_IEC_CLK_OUT);

//     IEC.enabled = true; // Re-enable IEC Service loop

//     return device_status;
// }