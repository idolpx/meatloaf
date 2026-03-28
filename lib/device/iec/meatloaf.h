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

#ifndef MEATLOAF_H
#define MEATLOAF_H

#include "drive.h"
#include "fuji.h"

class iecMeatloaf : public iecDrive, public iecFuji
{
private:

protected:

public:
    iecMeatloaf(uint8_t id) : iecDrive(id) {};

// Default VDRIVE state
#ifdef USE_VDRIVE
    bool use_vdrive = true;
#else
    bool use_vdrive = false;
#endif

    void setup(systemBus *bus) override {
        iecFuji::setup(bus);

        if (bus->attachDevice(this)) {
            Debug_printf("Attached Meatloaf device #%d\r\n", id());
        }
    }

    // // open file "name" on channel
    // bool open(uint8_t channel, const char *name, uint8_t nameLen) override {
    //     payload = std::string((const char *) name, nameLen);
    //     iecFuji::process_cmd(); // process FujiNet commands

    //     if ( !responseV.empty() )
    //     {
    //         // new_stream will be deleted in iecChannelHandlerFile destructor
    //         m_channels[channel] = new iecChannelHandlerFile(this, cmd_stream);  // Stream command response
    //         m_numOpenChannels++;
    //         setStatusCode(ST_OK);

    //         m_channels[channel]->writeBufferData(); // preload buffer
    //         setStatus((const char *)responseV.data(), responseV.size());
    //     }
    //     else
    //         iecDrive::open(channel, name, nameLen); // process file open
    // }

    // // close file on channel
    // void close(uint8_t channel) override {

    // }

    // // write bufferSize bytes to file on channel, returning the number of bytes written
    // // Returning less than bufferSize signals "cannot receive more data" for this file
    // uint8_t write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi) {

    // }

    // // read up to bufferSize bytes from file in channel, returning the number of bytes read
    // // returning 0 will signal end-of-file to the receiver. Returning 0
    // // for the FIRST call after open() signals an error condition
    // // (e.g. C64 load command will show "file not found")
    // uint8_t read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi) override {

    //     iecDrive::getStatusData((char *)buffer, bufferSize, eoi);

    // }

    void executeData(const uint8_t *data, uint8_t dataLen) override {
        payload = std::string((const char *) data, dataLen);
        iecFuji::process_cmd(); // process FujiNet commands

        if ( !responseV.empty() )
        {
            setStatus((const char *)responseV.data(), responseV.size());
        }
        else
            iecDrive::executeData(data, dataLen); // process CBM DOS commands

    }

    void enable(std::string deviceids) {
        enable_device_basic(deviceids);
    }
    void disable(std::string deviceids) {
        disable_device_basic(deviceids);
    }
};

extern iecMeatloaf Meatloaf;

#endif // MEATLOAF_H
