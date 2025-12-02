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
#include "../../../include/esp-idf-arduino.h"
#endif

IECDevice::IECDevice(uint8_t devnr) 
{ 
  m_devnr      = devnr; 
  m_handler    = NULL;
  m_isActive   = true;
  m_flEnabled  = 0;
  m_flFlags    = 0;
  m_flProtocol = IEC_FL_PROT_NONE;
}

void IECDevice::setDeviceNumber(uint8_t devnr)
{
  m_devnr = devnr;
}


void IECDevice::sendSRQ()
{
  if( m_handler ) m_handler->sendSRQ();
}

bool IECDevice::enableFastLoader(uint8_t loader, bool enable)
{
  // cancel any current fast-load activities
  m_flProtocol = IEC_FL_PROT_NONE;

  if( loader<=7 && m_handler!=NULL )
    {
      // must set the bit BEFORE calling IECBusHandler::enableFastLoader, otherwise
      // "enableParallelPins()" will not be called for parallel loaders.
      if( enable )
        m_flEnabled |= bit(loader);
      else 
        m_flEnabled &= ~bit(loader);

      if( !m_handler->enableFastLoader(this, loader, enable) ) 
        m_flEnabled &= ~bit(loader);
    }

  return (m_flEnabled & bit(loader))!=0;
}

bool IECDevice::isFastLoaderEnabled(uint8_t loader)
{
  return loader<=7 && (m_flEnabled & bit(loader))!=0;
}

bool IECDevice::fastLoadRequest(uint8_t loader, uint8_t request)
{
  if( m_handler!=NULL && isFastLoaderEnabled(loader) )
    {
      m_flProtocol = (loader<<3) | request;
      m_handler->fastLoadRequest(this, loader, request);
      return true;
    }
  else
    return false;
}

#ifdef IEC_FP_DOLPHIN 
void IECDevice::enableDolphinBurstMode(bool enable)
{
  if( m_handler ) m_handler->enableDolphinBurstMode(this, enable);
}
#endif

// default implementation of "buffer read" function which can/should be overridden
// (for efficiency) by devices using the JiffyDos, Epyx FastLoad or DolphinDos protocol
#if defined(IEC_FP_JIFFY) || defined(IEC_FP_EPYX) || defined(IEC_FP_DOLPHIN) || defined(IEC_FP_SPEEDDOS) || defined(IEC_FP_FC3) || defined(IEC_FP_AR6)
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


#if defined(IEC_FP_DOLPHIN) || defined(IEC_FP_FC3) || defined(IEC_FP_AR6)
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
