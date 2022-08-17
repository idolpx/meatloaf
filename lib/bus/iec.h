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

#ifndef MEATLIB_BUS_IEC
#define MEATLIB_BUS_IEC

#include <forward_list>
#include <unordered_map>

#include "meat_io.h"
#include "meat_stream.h"

#include "protocol/cbmstandardserial.h"
//#include "protocol/jiffydos.h"

#include "../../include/debug.h"


#define IEC_CMD_MAX_LENGTH  100

using namespace Protocol;


typedef enum
{
    DEVICE_ERROR = -1,
    DEVICE_IDLE = 0,       // Ready and waiting
    DEVICE_ACTIVE = 1,
    DEVICE_LISTEN = 2,     // A command is recieved and data is coming to us
    DEVICE_TALK = 3,       // A command is recieved and we must talk now
    DEVICE_PROCESS = 4,    // Execute device command
} device_state_t;

class IECData
{
    public:
        uint8_t primary = 0;
        uint8_t device = 0;
        uint8_t secondary = 0;
        uint8_t channel = 0;
        std::string device_command = "";

		void init ( void ) {
			primary = 0;
			device = 0;
			secondary = 0;
			channel = 0;
			device_command = "";
		}
};


class CommandPathTuple
{
    public:
        std::string command = "";
        std::string fullPath = "";
        std::string rawPath = "";
};

//
class iecBus; // declare early so can be friend

class iecDevice
{
    protected:
        friend iecBus;

    public:
        // Return values for service:

        std::unordered_map<uint16_t, std::shared_ptr<MStream>> streams;

        iecDevice();
        ~iecDevice() {};

        device_state_t queue_command ( void );
        virtual device_state_t process ( void ) = 0;

        virtual uint8_t command ( void ) = 0;
        virtual uint8_t execute ( void ) = 0;
        virtual uint8_t status ( void ) = 0;
        virtual void reset ( void ) = 0;

        uint8_t device_id;
        IECData data;
		device_state_t device_state;

    protected:

        // handler helpers.
        virtual void handleListenCommand ( void ) = 0;
        virtual void handleListenData ( void ) = 0;
        virtual void handleTalk ( uint8_t chan ) = 0;

        // Named Channel functions
        std::shared_ptr<MStream> currentStream;
        bool registerStream (int mode, std::string m_filename);
        std::shared_ptr<MStream> retrieveStream ( void );
        bool closeStream ( bool close_all = false );

        // This is set after an open command and determines what to send next
        uint8_t m_openState;

};


// Return values for service:
typedef enum
{
    BUS_RESET = -2,   // The IEC bus is in a reset state (RESET line).    
    BUS_ERROR = -1,   // A problem occoured, reset communication
    BUS_IDLE = 0,     // Nothing recieved of our concern
    BUS_ACTIVE = 1,   // ATN is pulled and a command byte is expected
    BUS_PROCESS = 2,  // A command is ready to be processed
} bus_state_t;

// IEC commands:
typedef enum
{
    IEC_GLOBAL = 0x00,     // 0x00 + cmd (global command)
    IEC_LISTEN = 0x20,     // 0x20 + device_id (LISTEN) (0-30)
    IEC_UNLISTEN = 0x3F,   // 0x3F (UNLISTEN)
    IEC_TALK = 0x40,       // 0x40 + device_id (TALK) (0-30)
    IEC_UNTALK = 0x5F,     // 0x5F (UNTALK)
    IEC_DATA = 0x60,     // 0x60 + channel (OPEN CHANNEL) (0-15)
    IEC_CLOSE = 0xE0,      // 0xE0 + channel (CLOSE NAMED CHANNEL) (0-15)
    IEC_OPEN = 0xF0        // 0xF0 + channel (OPEN NAMED CHANNEL) (0-15)
} bus_command_t;

class iecBus
{
    private:
        std::forward_list<iecDevice *> _daisyChain;

        iecDevice *_activeDev = nullptr;
        // sioModem *_modemDev = nullptr;
        // sioFuji *_fujiDev = nullptr;
        // sioNetwork *_netDev[8] = {nullptr};
        // sioMIDIMaze *_midiDev = nullptr;
        // sioCassette *_cassetteDev = nullptr;
        // sioCPM *_cpmDev = nullptr;
        // sioPrinter *_printerdev = nullptr;

    public:
        bus_state_t bus_state;
        IECData data;

        CBMStandardSerial protocol;

        iecBus ( void );

        // Initialise iec driver
        bool init();
        void setup();
        void shutdown();
        

        // Checks if CBM is sending an attention message. If this is the case,
        // the message is recieved and stored in iec_data.
        void service ( void );

        void receiveCommand ( void );


        // Checks if CBM is sending a reset (setting the RESET line high). This is typicall
        // when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
//  bool checkRESET();

        // Sends a byte. The communication must be in the correct state: a load command
        // must just have been recieved. If something is not OK, FALSE is returned.
        bool send ( uint8_t data );
        size_t send ( std::string data, size_t offset = 0 );

        // Same as IEC_send, but indicating that this is the last byte.
        bool sendEOI ( uint8_t data );

        // A special send command that informs file not found condition
        bool senderTimeout();

        // Recieves a byte
        int16_t receive ( uint8_t device = 0 );

        // Enabled Device Bit Mask
        uint32_t enabledDevices;
        bool isDeviceEnabled ( const uint8_t deviceNumber );
        void enableDevice ( const uint8_t deviceNumber );
        void disableDevice ( const uint8_t deviceNumber );

        uint8_t numDevices();
        void addDevice ( iecDevice *pDevice, uint8_t device_id );
        void remDevice ( iecDevice *pDevice );
        iecDevice *deviceById ( uint8_t device_id );
        void changeDeviceId ( iecDevice *pDevice, uint8_t device_id );

        void debugTiming();

        void releaseLines ( bool wait = false );

    private:
        // IEC Bus Commands
        bus_state_t deviceListen ( void ); // 0x20 + device_id   Listen, device (0–30)
		// void deviceUnListen(void);            // 0x3F               Unlisten, all devices
        bus_state_t deviceTalk ( void );   // 0x40 + device_id   Talk, device (0–30)
		// void deviceUnTalk(void);              // 0x5F               Untalk, all devices
		// device_state_t deviceSecond(void);    // 0x60 + channel     Reopen, channel (0–15)
		// device_state_t deviceClose(void);     // 0xE0 + channel     Close, channel (0–15)
		// device_state_t deviceOpen(void);      // 0xF0 + channel     Open, channel (0–15)
		bool turnAround( void );
        bool undoTurnAround ( void );
};

extern iecBus IEC;

#endif /* MEATLIB_BUS_IEC */
