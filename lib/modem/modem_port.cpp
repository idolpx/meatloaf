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

#include "modem_port.h"

#ifdef ENABLE_MODEM

#include "../../include/debug.h"

ModemPort::~ModemPort()
{
    end();
}

bool ModemPort::begin()
{
    if (isOpen())
        return true;

    // Trigger level 1: wake the reader as soon as any byte is available. A
    // terminal session is interactive; batching would add latency for nothing.
    rx_ = xStreamBufferCreate(RX_BYTES, 1);
    tx_ = xStreamBufferCreate(TX_BYTES, 1);

    if (rx_ == nullptr || tx_ == nullptr)
    {
        Debug_printv("modem: port buffers failed to allocate");
        end();
        return false;
    }
    return true;
}

void ModemPort::end()
{
    if (rx_ != nullptr)
    {
        vStreamBufferDelete(rx_);
        rx_ = nullptr;
    }
    if (tx_ != nullptr)
    {
        vStreamBufferDelete(tx_);
        tx_ = nullptr;
    }
    attached_ = false;
}

size_t ModemPort::pushRx(const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (rx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferSend(rx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

size_t ModemPort::popRx(uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (rx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferReceive(rx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

size_t ModemPort::pushTx(const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (tx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferSend(tx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

size_t ModemPort::popTx(uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (tx_ == nullptr || buf == nullptr || n == 0)
        return 0;
    return xStreamBufferReceive(tx_, buf, n, pdMS_TO_TICKS(timeout_ms));
}

#endif // ENABLE_MODEM
