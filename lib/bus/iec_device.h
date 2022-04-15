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

#ifndef IECDEVICE_H
#define IECDEVICE_H


#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#endif

#include <unordered_map>

#include "../../include/global_defines.h"
#include "../../include/cbmdefines.h"
#include "../../include/petscii.h"

#include "iec.h"
#include "device_db.h"
#include "meat_io.h"
//#include "MemoryInfo.h"
//#include "helpers.h"
#include "utils.h"
#include "string_utils.h"

class CommandPathTuple {
public:
	std::string command;
	std::string fullPath;
	std::string rawPath;
};

class Channel
{
public:
	std::string url;
	uint32_t cursor;
	bool writing;
};

class iecDevice
{
public:
	// Return values for service:
	typedef enum
	{
		DEVICE_IDLE = 0,       // Ready and waiting
		DEVICE_OPEN,           // Command received and channel opened
		DEVICE_DATA,           // Data sent or received
	} DEVICE_STATE;

	std::unordered_map<uint16_t, Channel> channels;

	iecDevice(IEC &iec);
	~iecDevice() {};

	uint8_t process( void );
	
	virtual uint8_t command( void ) = 0;
	virtual uint8_t execute( void ) = 0;
	virtual uint8_t status(void) = 0;

	uint8_t device_id;


protected:
	void reset(void);

	// handler helpers.
	virtual void handleListenCommand( void ) = 0;
	virtual void handleListenData( void ) = 0;
	virtual void handleTalk(uint8_t chan) = 0;
	virtual void handleOpen( void ) = 0;
	virtual void handleClose( void ) = 0;

	IEC &m_iec;	// IEC driver

	// This is set after an open command and determines what to send next
	uint8_t m_openState;
	
	DeviceDB m_device;

private:
	Channel channelSelect( void );
	bool channelClose( bool close_all = false );

};


#endif
