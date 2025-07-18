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

#define TCP_SERVER_PORT 23 // 3000

class TCPServer 
{
private:
    static int _client_socket;
    static bool _shutdown;
    static void task(void *pvParameters);

public:
    void start();
    void stop();

    static void send(std::string data);
};

extern TCPServer tcp_server;

#endif