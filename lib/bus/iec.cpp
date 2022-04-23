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

#include "iec.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "../../include/cbmdefines.h"

#include "drive.h"

#include "string_utils.h"

iecBus IEC;
//iecDrive drive;

using namespace CBM;
using namespace Protocol;

/********************************************************
 * 
 *  IEC Device Functions
 * 
 ********************************************************/

iecDevice::iecDevice( void )
{
	reset();
} // ctor


void iecDevice::reset(void)
{
	//m_device.reset();
} // reset


uint8_t iecDevice::process( void )
{
	Debug_printf("DEVICE: [%d] ", IEC.data.device);

	if (IEC.data.command == iecBus::IEC_OPEN || IEC.data.command == iecBus::IEC_SECOND)
	{
		Debug_printf("OPEN CHANNEL %d\r\n", IEC.data.channel);
		if (IEC.data.channel == 0)
			Debug_printf("LOAD \"%s\",%d\r\n", IEC.data.content.c_str(), IEC.data.device);
		else if (IEC.data.channel == 1)
			Debug_printf("SAVE \"%s\",%d\r\n", IEC.data.content.c_str(), IEC.data.device);
		else {
			Debug_printf("OPEN #,%d,%d,\"%s\"\r\n", IEC.data.device, IEC.data.channel, IEC.data.content.c_str());
		}

		// Open Named Channel
		handleOpen();

		// Open either file or prg for reading, writing or single line command on the command channel.
		handleListenCommand();
		
		if (IEC.state == iecBus::BUS_LISTEN)
		{
			// Receive data
			Debug_printv("[Receive data]");
			handleListenData();
		}
		else if (IEC.state == iecBus::BUS_TALK)
		{
			// Send data
			Debug_printv("[Send data]");
			handleTalk(IEC.data.channel);
		}
	}
	else if (IEC.data.command == iecBus::IEC_CLOSE)
	{
		Debug_printf("CLOSE CHANNEL %d\r\n", IEC.data.channel);
		if(IEC.data.channel > 0)
		{
			handleClose();
		}
	}	

	//Debug_printv("command[%.2X] channel[%.2X] state[%d]", IEC.data.command, IEC.data.channel, m_openState);

	return DEVICE_STATE::DEVICE_IDLE;
} // service


Channel iecDevice::channelSelect( void )
{
	size_t key = (IEC.data.device * 100) + IEC.data.channel;
	if(channels.find(key)!=channels.end()) {
		return channels.at(key);
	}

	// create and add channel if not found
	auto newChannel = Channel();
	newChannel.url = IEC.data.content;
	Debug_printv("CHANNEL device[%d] channel[%d] url[%s]", IEC.data.device, IEC.data.channel, IEC.data.content.c_str());

	channels.insert(std::make_pair(key, newChannel));
	return newChannel;
}

bool iecDevice::channelClose( bool close_all )
{
	size_t key = (IEC.data.device * 100) + IEC.data.channel;
	if(channels.find(key)!=channels.end()) {
		return channels.erase(key);
	}

	return false;
}


/********************************************************
 * 
 *  IEC Bus Functions
 * 
 ********************************************************/


iecBus::iecBus( void )
{
	init();
} // ctor

// Set all IEC_signal lines in the correct mode
//
bool iecBus::init()
{

#ifndef IEC_SPLIT_LINES
	// make sure the output states are initially LOW
//	protocol.release(PIN_IEC_ATN);
	protocol.release(PIN_IEC_CLK_OUT);
	protocol.release(PIN_IEC_DATA_OUT);
	protocol.release(PIN_IEC_SRQ);

	// initial pin modes in GPIO
	protocol.set_pin_mode(PIN_IEC_ATN, INPUT);
	protocol.set_pin_mode(PIN_IEC_CLK_IN, INPUT);
	protocol.set_pin_mode(PIN_IEC_DATA_IN, INPUT);
	protocol.set_pin_mode(PIN_IEC_SRQ, INPUT);
	protocol.set_pin_mode(PIN_IEC_RESET, INPUT);
#else
	// make sure the output states are initially LOW
	// protocol.release(PIN_IEC_ATN);
	// protocol.release(PIN_IEC_CLK_IN);
	// protocol.release(PIN_IEC_CLK_OUT);
	// protocol.release(PIN_IEC_DATA_IN);
	// protocol.release(PIN_IEC_DATA_OUT);
	// protocol.release(PIN_IEC_SRQ);

	// initial pin modes in GPIO
	protocol.set_pin_mode(PIN_IEC_ATN, INPUT);
	protocol.set_pin_mode(PIN_IEC_CLK_IN, INPUT);
	protocol.set_pin_mode(PIN_IEC_CLK_OUT, OUTPUT);
	protocol.set_pin_mode(PIN_IEC_DATA_IN, INPUT);
	protocol.set_pin_mode(PIN_IEC_DATA_OUT, OUTPUT);
	protocol.set_pin_mode(PIN_IEC_SRQ, OUTPUT);
	protocol.set_pin_mode(PIN_IEC_RESET, INPUT);
#endif

	protocol.flags = CLEAR;

	return true;
} // init



/******************************************************************************
 *                                                                             *
 *                               Public functions                              *
 *                                                                             *
 ******************************************************************************/

// This function checks and deals with atn signal commands
//
// If a command is recieved, the this->data.string is saved in this->data. Only commands
// for *this* device are dealt with.
//
/** from Derogee's "IEC Disected"
 * ATN SEQUENCES
 * When ATN is PULLED true, everybody stops what they are doing. The processor will quickly protocol.pull the
 * Clock line true (it's going to send soon), so it may be hard to notice that all other devices protocol.release the
 * Clock line. At the same time, the processor protocol.releases the Data line to false, but all other devices are
 * getting ready to listen and will each protocol.pull Data to true. They had better do this within one
 * millisecond (1000 microseconds), since the processor is watching and may sound an alarm ("device
 * not available") if it doesn't see this take place. Under normal circumstances, transmission now
 * takes place as previously described. The computer is sending commands rather than data, but the
 * characters are exchanged with exactly the same timing and handshakes as before. All devices
 * receive the commands, but only the specified device acts upon it. This results in a curious
 * situation: you can send a command to a nonexistent device (try "OPEN 6,6") - and the computer
 * will not know that there is a problem, since it receives valid handshakes from the other devices.
 * The computer will notice a problem when you try to send or receive data from the nonexistent
 * device, since the unselected devices will have dropped off when ATN ceased, leaving you with
 * nobody to talk to.
 */
// Return value, see iecBus::BUS_STATE definition.
iecBus::BUS_STATE iecBus::service( void )
{
	iecBus::BUS_STATE r = BUS_ACTIVE;

#ifdef IEC_HAS_RESET
	// Check if CBM is sending a reset (setting the RESET line high). This is typically
	// when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
	if(protocol.status(PIN_IEC_RESET) == PULLED)
	{
		if (protocol.status(PIN_IEC_ATN) == PULLED)
		{
			// If RESET & ATN are both PULLED then CBM is off
			return BUS_IDLE;
		}
		Debug_printv("IEC Reset!");
		return BUS_RESET;
	}
#endif

	// Attention line is PULLED, go to listener mode and get message.
	// Being fast with the next two lines here is CRITICAL!
	protocol.release(PIN_IEC_CLK_OUT);
	protocol.pull(PIN_IEC_DATA_OUT);
	delayMicroseconds(TIMING_Tne);

    //IEC.protocol.pull(PIN_IEC_SRQ);

	// Get command
	uint8_t previous_command = this->data.command;
	int16_t c = (iecBus::COMMAND)receive(this->data.device);
	this->data.command = c;	

	//IEC.protocol.release(PIN_IEC_SRQ);

	Debug_printf("   IEC: [%.2X]", c);

	// Check for error
	if(protocol.flags bitand ERROR)
	{
		Debug_printv("Get first ATN byte");
		releaseLines(false);
		r = BUS_ERROR;
	}

	// // Check for EOI
	// if(protocol.flags bitand EOI_RECVD)
	// {
	// 	Debug_printf("[EOI]");
	// }

	// Check for JiffyDOS
	if(protocol.flags bitand JIFFY_ACTIVE)
	{
		Debug_printf("[JIFFY]");
	}


	// Decode command byte
	uint8_t command = c bitand 0xF0;
	if(command == IEC_GLOBAL)
	{
		this->data.command = IEC_GLOBAL;
		this->data.device = c xor IEC_GLOBAL;
		this->data.channel = 0;
		Debug_printf(" (00 GLOBAL %.2d COMMAND)\r\n", this->data.device);
		r = BUS_IDLE;
	}
	else if(command == IEC_LISTEN)
	{
		this->data.command = IEC_LISTEN;
		this->data.device = c xor IEC_LISTEN;
		this->data.channel = 0;
		Debug_printf(" (20 LISTEN %.2d DEVICE)\r\n", this->data.device);
		r = BUS_ACTIVE;
	}
	else if(c == IEC_UNLISTEN)
	{
		Debug_printf(" (3F UNLISTEN)\r\n");
		//releaseLines(false);
		r = BUS_IDLE;
	}
	else if(command == IEC_TALK)
	{
		this->data.command = IEC_TALK;
		this->data.device = c xor IEC_TALK;
		this->data.channel = 0;
		Debug_printf(" (40 TALK   %.2d DEVICE)\r\n", this->data.device);
		r = BUS_ACTIVE;
	}
	else if(c == IEC_UNTALK)
	{
		Debug_printf(" (5F UNTALK)\r\n");
		//releaseLines(false);
		r = BUS_IDLE;
	}
	else if(command == IEC_OPEN)
	{
		this->data.command = IEC_OPEN;
		this->data.channel = c xor IEC_OPEN;
		Debug_printf(" (F0 OPEN   %.2d CHANNEL) ", this->data.channel);
	}
	else if(command == IEC_SECOND)
	{
		this->data.command = IEC_SECOND;
		this->data.channel = c xor IEC_SECOND;
		Debug_printf(" (60 DATA   %.2d CHANNEL) ", this->data.channel);
	}
	else if(command == IEC_CLOSE)
	{
		this->data.command = IEC_CLOSE;
		this->data.channel = c xor IEC_CLOSE;
		Debug_printf(" (E0 CLOSE  %.2d CHANNEL)\r\n", this->data.channel);
	}


	if(command == IEC_SECOND || command == IEC_CLOSE || command == IEC_OPEN)
	{
		if(previous_command == IEC_LISTEN)
		{
			r = deviceListen();
		}
		else
		{
			r = deviceTalk();
		}

		if(protocol.flags bitand ERROR)
		{
			Debug_printv("Listen/Talk ERROR");
			r = BUS_ERROR;
		}
	}

	//Debug_printv("command[%.2X] device[%.2d] secondary[%.2d] channel[%.2d]", this->data.command, this->data.device, this->data.secondary, this->data.channel);

	// Was there an error?
	this->state = r;
	if(r == BUS_ERROR)
	{
		releaseLines(true);
	}


	if (protocol.status(PIN_IEC_ATN) == RELEASED)
	{
		// Process commands when ATN is Released
		if(r == BUS_COMMAND || r == BUS_LISTEN || r == BUS_TALK)
		{
			// Send data to device to process
			//Debug_printv("Send Command [%.2X]{%s} to virtual device", IEC.data.command, IEC.data.content.c_str());
			drive.process();

			releaseLines(false);
			r = BUS_IDLE;
		}
	}

	return r;
} // service

iecBus::BUS_STATE iecBus::deviceListen( void )
{
	// Okay, we will listen.

	// If the command is SECONDARY and it is not to expect just a small command on the command channel, then
	// we're into something more heavy. Otherwise read it all out right here until UNLISTEN is received.
	if(this->data.command == IEC_SECOND && this->data.channel not_eq CMD_CHANNEL)
	{
		// A heapload of data might come now, too big for this context to handle so the caller handles this, we're done here.
		// Debug_printf(" (%.2X SECONDARY) (%.2X CHANNEL)\r\n", this->data.command, this->data.channel);
		Debug_printf(BACKSPACE "\r\n");
		return BUS_LISTEN;
	}

	// OPEN
	else if(this->data.command == IEC_SECOND || this->data.command == IEC_OPEN)
	{
		// Debug_printf(" (%.2X OPEN) (%.2X CHANNEL) [", this->data.command, this->data.channel);
		Debug_printf(" [");

		// Some other command. Record the cmd string until ATN is PULLED
		std::string listen_command;
		while (protocol.status(PIN_IEC_ATN) != PULLED)
		{
			int16_t c = receive();

			if(protocol.flags bitand ERROR)
			{
				Debug_printv("Some other command [%.2X]", c);
				return BUS_ERROR;
			}

			if(c != 0x0D)
			{
				listen_command += (uint8_t)c;
			}
		}

		if (listen_command.length()) {
			this->data.content = listen_command;
			mstr::rtrimA0(this->data.content);
			Debug_printf(BACKSPACE "] {%s}\r\n", this->data.content.c_str());			
		} else {
			Debug_printf(BACKSPACE "\r\n");
		}

		return BUS_COMMAND;
	}

	// CLOSE Named Channel
	else if(this->data.command == IEC_CLOSE)
	{
		// Debug_printf(" (E0 CLOSE) (%.2X CHANNEL)\r\n", this->data.channel);
		return BUS_COMMAND;
	}

	// Unknown
	else
	{
		Debug_printv(" OTHER (%.2X COMMAND) (%.2X CHANNEL) ", this->data.command, this->data.channel);
		return BUS_ERROR;
	}
}

// void iecBus::deviceUnListen(void)
// {
// 	Debug_printv("");

// 	// Release lines
// 	protocol.release(PIN_IEC_CLK_OUT);
// 	protocol.release(PIN_IEC_DATA_OUT);

// 	// Wait for ATN to protocol.release and quit
// 	while(protocol.status(PIN_IEC_ATN) == PULLED)
// 	{
// 		ESP.wdtFeed();
// 	}
// }

iecBus::BUS_STATE iecBus::deviceTalk( void )
{
	// Okay, we will talk soon
	// Debug_printf(" (%.2X SECONDARY) (%.2X CHANNEL)\r\n", this->data.command, this->data.channel);

	// Delay after ATN is RELEASED
	//delayMicroseconds(TIMING_BIT);

	// Now do bus turnaround
	if(not protocol.turnAround())
		return BUS_ERROR;

	// We have recieved a CMD and we should talk now:
	return BUS_TALK;
}

// void iecBus::deviceUnTalk(void)
// {
// 	Debug_printv("");

// 	// Release lines
// 	protocol.release(PIN_IEC_CLK_OUT);
// 	protocol.release(PIN_IEC_DATA_OUT);

// 	// Wait for ATN to protocol.release and quit
// 	while(protocol.status(PIN_IEC_ATN) == PULLED)
// 	{
// 		ESP.wdtFeed();
// 	}
// }


void iecBus::releaseLines(bool wait)
{
	//Debug_printv("");

	// Release lines
	protocol.release(PIN_IEC_CLK_OUT);
	protocol.release(PIN_IEC_DATA_OUT);

	// Wait for ATN to release and quit
	if ( wait )
	{
		//Debug_printv("Waiting for ATN to release");
		while(protocol.status(PIN_IEC_ATN) == PULLED)
		{
//			ESP.wdtFeed();
		}
	}
}


// boolean  iecBus::checkRESET()
// {
// 	return readRESET();
// 	return false;
// } // checkRESET


// IEC_receive receives a byte
//
int16_t iecBus::receive(uint8_t device)
{
	int16_t data;

	data = protocol.receiveByte(device); // Standard CBM Timing
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif
	// if(data < 0)
	// 	protocol.flags = errorFlag;

	return data;
} // receive


// IEC_send sends a byte
//
bool iecBus::send(uint8_t data)
{
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif
	IEC.protocol.pull(PIN_IEC_SRQ);
	bool r = protocol.sendByte(data, false); // Standard CBM Timing
	IEC.protocol.release(PIN_IEC_SRQ);
	return r;
} // send

bool iecBus::send(std::string data)
{
	for (size_t i = 0; i < data.length(); ++i)
		if (!send((uint8_t)data[i]))
			return false;

	return true;
}


// Same as IEC_send, but indicating that this is the last byte.
//
bool iecBus::sendEOI(uint8_t data)
{
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif
	Debug_println("\r\nEOI Sent!");
	if(protocol.sendByte(data, true))
	{
		// As we have just send last byte, turn bus back around
		if(protocol.undoTurnAround())
		{
			return true;
		}
	}

	return false;
} // sendEOI


// A special send command that informs file not found condition
//
bool iecBus::sendFNF()
{
	// Message file not found by just releasing lines
	protocol.release(PIN_IEC_DATA_OUT);
	protocol.release(PIN_IEC_CLK_OUT);

	// BETWEEN BYTES TIME
	delayMicroseconds(TIMING_Tbb);

	Debug_println("\r\nFNF Sent!");
	return true;
} // sendFNF


bool iecBus::isDeviceEnabled(const uint8_t deviceNumber)
{
	return (enabledDevices & (1<<deviceNumber));
} // isDeviceEnabled

void iecBus::enableDevice(const uint8_t deviceNumber)
{
	enabledDevices |= 1UL << deviceNumber;
} // enableDevice

void iecBus::disableDevice(const uint8_t deviceNumber)
{
	enabledDevices &= ~(1UL << deviceNumber);
} // disableDevice


// Add device to SIO bus
void iecBus::addDevice(iecDevice *pDevice, uint8_t device_id)
{
    // if (device_id == SIO_DEVICEID_FUJINET)
    // {
    //     _fujiDev = (sioFuji *)pDevice;
    // }
    // else if (device_id == SIO_DEVICEID_RS232)
    // {
    //     _modemDev = (sioModem *)pDevice;
    // }
    // else if (device_id >= SIO_DEVICEID_FN_NETWORK && device_id <= SIO_DEVICEID_FN_NETWORK_LAST)
    // {
    //     _netDev[device_id - SIO_DEVICEID_FN_NETWORK] = (sioNetwork *)pDevice;
    // }
    // else if (device_id == SIO_DEVICEID_MIDI)
    // {
    //     _midiDev = (sioMIDIMaze *)pDevice;
    // }
    // else if (device_id == SIO_DEVICEID_CASSETTE)
    // {
    //     _cassetteDev = (sioCassette *)pDevice;
    // }
    // else if (device_id == SIO_DEVICEID_CPM)
    // {
    //     _cpmDev = (sioCPM *)pDevice;
    // }
    // else if (device_id == SIO_DEVICEID_PRINTER)
    // {
    //     _printerdev = (sioPrinter *)pDevice;
    // }

    pDevice->device_id = device_id;

    _daisyChain.push_front(pDevice);
}

// Removes device from the SIO bus.
// Note that the destructor is called on the device!
void iecBus::remDevice(iecDevice *p)
{
    _daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
uint8_t iecBus::numDevices()
{
    int i = 0;

    for (auto devicep : _daisyChain)
        i++;
    return i;
}

void iecBus::changeDeviceId(iecDevice *p, uint8_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->device_id = device_id;
    }
}

iecDevice *iecBus::deviceById(uint8_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->device_id == device_id)
            return devicep;
    }
    return nullptr;
}



void iecBus::debugTiming()
{
	uint8_t pin = PIN_IEC_ATN;

#ifndef SPILIT_LINES
	protocol.pull(pin);
	delayMicroseconds(1000); // 1000
	protocol.release(pin);
	delayMicroseconds(1000);
#endif

	pin = PIN_IEC_CLK_OUT;
	protocol.pull(pin);
	delayMicroseconds(20); // 20
	protocol.release(pin);
	delayMicroseconds(20);

	pin = PIN_IEC_DATA_OUT;
	protocol.pull(pin);
	delayMicroseconds(40); // 40
	protocol.release(pin);
	delayMicroseconds(40);

	pin = PIN_IEC_SRQ;
	protocol.pull(pin);
	delayMicroseconds(60); // 60
	protocol.release(pin);
	delayMicroseconds(60);
}