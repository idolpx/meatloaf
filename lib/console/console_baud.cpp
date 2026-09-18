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

#include <atomic>
#include <mutex>
#include <stdio.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#include "mlConfig.h"
#include "../../include/debug.h"

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#define CONSOLE_HAS_UART 1
#else
#define CONSOLE_HAS_UART 0
#endif

namespace ESP32Console
{
    // Written by whichever task runs an AT+IPR, read by the shell pump. The
    // read-and-clear in consoleBaudApplyPending() has to be one indivisible
    // step -- a plain load followed by a plain store would let a writer land
    // between them and have its rate silently overwritten by the clear. The
    // exchange() makes it atomic as a unit, so a rate set while the pump is
    // mid-apply is either taken by this pass or left for the next one, never
    // dropped.
    static std::atomic<int> s_pending{0};

    // Serializes the whole of consoleBaudSet(). s_pending's exchange() decides
    // which pump WINS a pending rate; it says nothing about two tasks being
    // inside the apply itself. "baud 9600" from the TCP console runs on
    // console_exec while a serial pump can concurrently apply a pending rate,
    // and both would reach uart_set_baudrate() and mlConfig.save(). mlConfig
    // has no locking of any kind, so that is a concurrent mutate-and-serialize
    // of one nlohmann tree -- a corrupted config.json, which is the worse half.
    //
    // Held across the drain as well as the persist, deliberately. Narrowing it
    // to the persist would leave two uart_set_baudrate() calls racing, which is
    // the same defect one layer down. The only task that can ever block on it
    // is another consoleBaudSet() caller, which is exactly what must serialize.
    static std::mutex s_set_mutex;

    bool consoleBaudSupported()
    {
        return CONSOLE_HAS_UART;
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

        std::lock_guard<std::mutex> guard(s_set_mutex);

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

        // json_object_at() replaces a "preferences" node an older firmware left
        // as a non-object rather than indexing into it -- operator[] on a
        // number or a string is type_error.305, the same abort() the rest of
        // this file avoids on the read side.
        json_object_at(mlConfig.data(), "preferences")["baud"] = baud;
        // The same call AT&W already makes through Modem::saveConfig(). save()
        // hashes the sections and writes nothing when nothing changed.
        mlConfig.save();
        return ESP_OK;
#endif
    }

    void consoleBaudSetPending(int baud)
    {
        if (baud >= BAUD_MIN && baud <= BAUD_MAX)
            s_pending.store(baud);
    }

    void consoleBaudApplyPending()
    {
        int baud = s_pending.exchange(0);
        if (baud == 0)
            return;
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
        esp_err_t err = consoleBaudSet(baud);
        if (err != ESP_OK)
        {
            // Booting at DEBUG_SPEED is the safe outcome, but silence leaves a
            // hand-edited preferences.baud (the documented WebDAV recovery
            // route) looking as though it had been honoured. Say so: this line
            // goes out at DEBUG_SPEED, which is where anyone reading the boot
            // log already is.
            Debug_printv("console baud %d from config refused (%s), staying at %d",
                         baud, esp_err_to_name(err), consoleBaudGet());
        }
    }
}
