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

#include "console_baud.h"

#include <stdio.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#include "mlConfig.h"

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#define CONSOLE_HAS_UART 1
#else
#define CONSOLE_HAS_UART 0
#endif

namespace ESP32Console
{
    // Written by whichever task runs an AT+IPR, read by the shell pump. A plain
    // int is enough: it is a single aligned word, one writer at a time, and a
    // missed pending rate would simply be applied on the next pass.
    static volatile int s_pending = 0;

    bool consoleBaudSupported()
    {
        return CONSOLE_HAS_UART ? true : false;
    }

    const char *consoleBaudTransportName()
    {
#if CONSOLE_HAS_UART
        return "UART";
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
        return "USB-Serial-JTAG";
#else
        return "USB-CDC";
#endif
    }

    int consoleBaudGet()
    {
#if CONSOLE_HAS_UART
        uint32_t rate = 0;
        if (uart_get_baudrate((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, &rate) == ESP_OK)
            return (int)rate;
#endif
        return 0;
    }

    esp_err_t consoleBaudSet(int baud)
    {
#if !CONSOLE_HAS_UART
        (void)baud;
        return ESP_ERR_NOT_SUPPORTED;
#else
        if (baud < BAUD_MIN || baud > BAUD_MAX)
            return ESP_ERR_INVALID_ARG;

        const uart_port_t port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
        const int previous = consoleBaudGet();

        // The notice the user is reading was written at the OLD rate. Flushing
        // only moves it into the driver; uart_wait_tx_done() is what puts it on
        // the wire. Switching before that clocks the last characters out at the
        // new rate, which is exactly the garbage this whole design avoids.
        fflush(stdout);
        fsync(fileno(stdout));
        uart_wait_tx_done(port, pdMS_TO_TICKS(200));

        esp_err_t err = uart_set_baudrate(port, baud);
        if (err != ESP_OK)
        {
            // Persist NOTHING. A rate the driver refuses must never become the
            // rate the next boot reads -- that is the one failure this design
            // exists to make impossible.
            if (previous > 0)
                uart_set_baudrate(port, previous);
            return err;
        }

        auto &root = mlConfig.data();
        if (!root.is_object())
            return ESP_OK;
        if (!root.contains("preferences") || !root["preferences"].is_object())
            root["preferences"] = psram_json::object();
        root["preferences"]["baud"] = baud;
        // The same call AT&W already makes through Modem::saveConfig(). save()
        // hashes the sections and writes nothing when nothing changed.
        mlConfig.save();
        return ESP_OK;
#endif
    }

    void consoleBaudSetPending(int baud)
    {
        if (baud >= BAUD_MIN && baud <= BAUD_MAX)
            s_pending = baud;
    }

    void consoleBaudApplyPending()
    {
        int baud = s_pending;
        if (baud == 0)
            return;
        s_pending = 0;
        consoleBaudSet(baud);
    }

    void consoleBaudRestore()
    {
        auto &root = mlConfig.data();
        if (!root.is_object() || !root.contains("preferences"))
            return;
        // json_int() checks is_object() and then the field's own type. value()
        // would abort() on a hand-edited config, and this runs too early in
        // boot for that to be recoverable.
        int baud = json_int(root.at("preferences"), "baud", 0);
        if (baud <= 0)
            return; // 0 means "use the build's DEBUG_SPEED"
        if (baud == consoleBaudGet())
            return;
        consoleBaudSet(baud);
    }
}
