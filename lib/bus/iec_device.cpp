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

#include "../../include/debug.h"

#include "iec_device.h"
#include "iec.h"
#include "wrappers/iec_buffer.h"

using namespace CBM;
using namespace Protocol;


iecDevice::iecDevice( IEC &iec ) : m_iec(iec), m_device(0)
{
	reset();
} // ctor


void iecDevice::reset(void)
{
	//m_device.reset();
} // reset


uint8_t iecDevice::process( void )
{
	//iecDevice::DeviceState r = DEVICE_IDLE;

	if (this->m_iec.state == IEC::BUS_ERROR)
	{
		reset();
		this->m_iec.state = IEC::BUS_IDLE;
	}

	// Did anything happen from the controller side?
	else if (this->m_iec.state not_eq IEC::BUS_IDLE)
	{
		Debug_printf("DEVICE: [%d] ", this->m_iec.data.device);

		if (this->m_iec.data.command == IEC::IEC_OPEN)
		{
			Debug_printf("OPEN CHANNEL %d\r\n", this->m_iec.data.channel);
			if (this->m_iec.data.channel == 0)
				Debug_printf("LOAD \"%s\",%d\r\n", this->m_iec.data.content.c_str(), this->m_iec.data.device);
			else if (this->m_iec.data.channel == 1)
				Debug_printf("SAVE \"%s\",%d\r\n", this->m_iec.data.content.c_str(), this->m_iec.data.device);
			else {
				Debug_printf("OPEN #,%d,%d,\"%s\"\r\n", this->m_iec.data.device, this->m_iec.data.channel, this->m_iec.data.content.c_str());
			}

			// Open Named Channel
			handleOpen();

			// Open either file or prg for reading, writing or single line command on the command channel.
			if (this->m_iec.state == IEC::BUS_COMMAND)
			{
				// Process a command
				Debug_printv("[Process a command]");
				handleListenCommand();
			}
			else if (this->m_iec.state == IEC::BUS_LISTEN)
			{
				// Receive data
				Debug_printv("[Receive data]");
				handleListenData();
			}
		}
		else if (this->m_iec.data.command == IEC::IEC_SECOND) // data channel opened
		{
			Debug_printf("DATA CHANNEL %d\r\n", this->m_iec.data.channel);
			if (this->m_iec.state == IEC::BUS_COMMAND)
			{
				// Process a command
				Debug_printv("[Process a command]");
				handleListenCommand();
			}
			else if (this->m_iec.state == IEC::BUS_LISTEN)
			{
				// Receive data
				Debug_printv("[Receive data]");
				handleListenData();
			}
			else if (this->m_iec.state == IEC::BUS_TALK)
			{
				// Send data
				Debug_printv("[Send data]");
				if (this->m_iec.data.channel == CMD_CHANNEL)
				{
					handleListenCommand();		 // This is typically an empty command,
				}

				handleTalk(this->m_iec.data.channel);
			}
		}
		else if (this->m_iec.data.command == IEC::IEC_CLOSE)
		{
			Debug_printf("CLOSE CHANNEL %d\r\n", this->m_iec.data.channel);
			if(this->m_iec.data.channel > 0)
			{
				handleClose();
			}
		}
	}
	//Debug_printv("mode[%d] command[%.2X] channel[%.2X] state[%d]", mode, this->m_iec.data.command, this->m_iec.data.channel, m_openState);

	return DEVICE_STATE::DEVICE_IDLE;
} // service


Channel iecDevice::channelSelect( void )
{
	size_t key = (this->m_iec.data.device * 100) + this->m_iec.data.channel;
	if(channels.find(key)!=channels.end()) {
		return channels.at(key);
	}

	// create and add channel if not found
	auto newChannel = Channel();
	newChannel.url = this->m_iec.data.content;
	Debug_printv("CHANNEL device[%d] channel[%d] url[%s]", this->m_iec.data.device, this->m_iec.data.channel, this->m_iec.data.content.c_str());

	channels.insert(std::make_pair(key, newChannel));
	return newChannel;
}

bool iecDevice::channelClose( bool close_all )
{
	size_t key = (this->m_iec.data.device * 100) + this->m_iec.data.channel;
	if(channels.find(key)!=channels.end()) {
		return channels.erase(key);
	}

	return false;
}
