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

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TCP_SERVER_PORT 23 // 3000

class TCPServer 
{
private:
    static int _server_socket;
    static int _client_socket;
    static bool _shutdown;
    static void task(void *pvParameters);
    static void session_task(void *pvParameters);

    static TaskHandle_t _htask;
    static TaskHandle_t _session_htask;

#ifdef ENABLE_MODEM
    // The port a console in modem mode wants this session's raw bytes fed to,
    // plus the mutex that makes handing it over safe. See tcpsvr.cpp.
    static class ModemPort *_modem_sink;
    static SemaphoreHandle_t _modem_sink_lock;
    static StaticSemaphore_t _modem_sink_lock_storage;
#endif

public:
    void start();
    void stop();

    static void send(std::string data);
    static void disconnect();

    // Non-blocking check for ESC (0x1B) from the connected client, so a long
    // console command can be cancelled over TCP the way it can over serial.
    //
    // Safe to call from the console executor task, and ONLY from there: while a
    // command runs, session_task() is blocked inside console.execute() and is
    // not reading the socket, so there is no second reader to race. Bytes that
    // are not ESC are discarded -- see console_cancel.h.
    static bool pollCancel();

#ifdef ENABLE_MODEM
    // Route this session's received bytes to a modem port instead of the
    // command shell. setModemSink(nullptr) ends that routing and does not
    // return until any feed already in progress has finished, which is what
    // lets the caller then destroy the port safely.
    static void setModemSink(class ModemPort *port);
    static bool modemFeed(const char *buf, size_t n);
#endif
};

extern TCPServer tcp_server;

#endif