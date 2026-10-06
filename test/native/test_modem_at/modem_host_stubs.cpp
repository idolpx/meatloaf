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

// Definitions for the handful of globals lib/modem/modem.cpp links against.
//
// The headers these satisfy are host stubs in
// test/native/test_archive_extract/host (the shared shim directory), except
// mlConfig.h and console_baud.h, which are the REAL headers -- so modem.cpp
// compiles here against the same declarations it compiles against on the
// device. Only the bodies are substituted, and only for the things that would
// otherwise drag in a filesystem, a UART or a radio.

#define ENABLE_MODEM 1

#include "fnWiFi.h"
#include "fnSystem.h"
#include "mlConfig.h"
#include "console_baud.h"
#include "modem_baud_fake.h"

MlStubWiFi   fnWiFi;
MlStubSystem fnSystem;

// The config tree itself is real (psram_json through the heap_caps stub), so
// Modem::loadConfig()/saveConfig() run their actual nlohmann walks against it,
// malformed shapes included. Only the two filesystem ends are stubbed.
MeatloafConfig mlConfig;

bool MeatloafConfig::load() { return true; }
void MeatloafConfig::save() {}

// iana_to_posix_tz lives in mlConfig.cpp, which needs a filesystem. Nothing in
// lib/modem calls it; this satisfies the declaration for any translation unit
// that pulls the header in.
const char *iana_to_posix_tz(const std::string &iana) { (void)iana; return nullptr; }

namespace ModemBaudFake
{
    bool supported = true;
    int  current = 2000000;
    int  pending = 0;
    int  promote_count = 0;
    std::function<void()> on_promote;

    void reset()
    {
        supported = true;
        current = 2000000;
        pending = 0;
        promote_count = 0;
        on_promote = nullptr;
    }
}

namespace ESP32Console
{
    bool consoleBaudSupported() { return ModemBaudFake::supported; }

    const char *consoleBaudTransportName()
    {
        return ModemBaudFake::supported ? "UART" : "USB-Serial-JTAG";
    }

    int consoleBaudGet() { return ModemBaudFake::supported ? ModemBaudFake::current : 0; }

    void consoleBaudSetPending(int baud)
    {
        // The snapshot runs BEFORE the record, so a test observing the world
        // here sees it exactly as the firmware left it at the moment of the
        // publish -- which is the whole point of this fake.
        if (ModemBaudFake::on_promote)
            ModemBaudFake::on_promote();
        ModemBaudFake::pending = baud;
        ModemBaudFake::promote_count++;
    }

    // Not reached from lib/modem: the modem stages a rate and the shell pump
    // applies it. Present so the real header's declarations all resolve.
    esp_err_t consoleBaudSet(int baud) { ModemBaudFake::current = baud; return ESP_OK; }
    void consoleBaudApplyPending() {}
    void consoleBaudRestore() {}
}
