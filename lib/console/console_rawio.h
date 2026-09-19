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

// Raw byte mode for the console, as an RAII guard.
//
// The console driver is configured (console_settings.c) for interactive
// terminal use: RX turns a bare CR into LF (ESP_LINE_ENDINGS_CR) and TX expands
// LF to CRLF (ESP_LINE_ENDINGS_CRLF). Both silently corrupt raw bytes that
// happen to contain CR or LF, so anything moving binary or a remote terminal
// stream over the console has to turn them off for its duration and restore the
// interactive defaults on every exit path, early returns included.
//
// Two callers need this and neither can see the other's copy: the rx/tx
// transfer commands in Commands/XFERCommands.cpp, and modem mode in Console.cpp
// (a BBS stream is corrupted by the same translation, and the modem emits its
// own S3/S4 terminators). It lives here so there is one definition rather than
// two that can drift.

#ifndef MEATLOAF_CONSOLE_RAWIO
#define MEATLOAF_CONSOLE_RAWIO

#include "driver/uart_vfs.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_vfs_cdcacm.h"

static inline void set_console_rx_line_endings(esp_line_endings_t mode)
{
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, mode);
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_vfs_dev_cdcacm_set_rx_line_endings(mode);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    usb_serial_jtag_vfs_set_rx_line_endings(mode);
#endif
}

static inline void set_console_tx_line_endings(esp_line_endings_t mode)
{
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, mode);
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_vfs_dev_cdcacm_set_tx_line_endings(mode);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    usb_serial_jtag_vfs_set_tx_line_endings(mode);
#endif
}

class ConsoleRawIOGuard
{
public:
    ConsoleRawIOGuard()
    {
        set_console_rx_line_endings(ESP_LINE_ENDINGS_LF);
        set_console_tx_line_endings(ESP_LINE_ENDINGS_LF);
    }
    ~ConsoleRawIOGuard()
    {
        set_console_rx_line_endings(ESP_LINE_ENDINGS_CR);
        set_console_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    }
};

#endif // MEATLOAF_CONSOLE_RAWIO
