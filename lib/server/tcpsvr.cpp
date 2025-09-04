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

#if defined(ENABLE_CONSOLE) && defined(ENABLE_CONSOLE_TCP)
#include "tcpsvr.h"

#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "../../include/debug.h"
#include "string_utils.h"

#include "../console/ESP32Console.h"

#define MESSAGE "Welcome to Meatloaf!\r\n"
#define LISTENQ 2

TCPServer tcp_server;
int TCPServer::_server_socket = -1;
int TCPServer::_client_socket = -1;
bool TCPServer::_shutdown = false;
TaskHandle_t TCPServer::_htask = NULL;

void TCPServer::task(void *pvParameters)
{
    char rx_buffer[128];    // char array to store received data
    char addr_str[128];     // char array to store client IP
    int bytes_received;     // immediate bytes received
    int addr_family;        // Ipv4 address protocol variable

    int ip_protocol;
    int bind_err = 0;
    int listen_error = 0;

    std::string line;

    while (!_shutdown)
    {
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);     //Change hostname to network byte order
        destAddr.sin_family = AF_INET;                    //Define address family as Ipv4
        destAddr.sin_port = htons(TCP_SERVER_PORT);       //Define PORT
        addr_family = AF_INET;                            //Define address family as Ipv4
        ip_protocol = IPPROTO_TCP;                        //Define protocol as TCP
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

        /* Create TCP socket*/
        _server_socket = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (_server_socket < 0)
        {
            Debug_printv("Unable to create socket: errno %d", errno);
            break;
        }
        //Debug_printv("Socket created");

        /* Bind a socket to a specific IP + port */
        bind_err = bind(_server_socket, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (bind_err != 0)
        {
            Debug_printv("Socket unable to bind: errno %d", errno);
            break;
        }
        //Debug_printv("Socket binded");

        /* Begin listening for clients on socket */
        listen_error = listen(_server_socket, 3);
        if (listen_error != 0)
        {
            Debug_printv("Error occured during listen: errno %d", errno);
            break;
        }
        //Debug_printv("Socket listening");

        while (!_shutdown)
        {
            struct sockaddr_in sourceAddr; // Large enough for IPv4
            socklen_t addrLen = sizeof(sourceAddr);
            /* Accept connection to incoming client */
            _client_socket = accept(_server_socket, (struct sockaddr *)&sourceAddr, &addrLen);
            if (_client_socket < 0)
            {
                Debug_printv("Unable to accept connection: errno %d", errno);
                break;
            }
            Debug_printv("Socket accepted");

            // Send Welcome message
            write(_client_socket, MESSAGE, strlen(MESSAGE));

            //Optionally set O_NONBLOCK
            //If O_NONBLOCK is set then recv() will return, otherwise it will stall until data is received or the connection is lost.
            //fcntl(_client_socket,F_SETFL,O_NONBLOCK);

            // Clear rx_buffer, and fill with zero's
            bzero(rx_buffer, sizeof(rx_buffer));
            vTaskDelay(500 / portTICK_PERIOD_MS);
            while(!_shutdown)
            {
                //Debug_printv("Waiting for data");
                //send("meatloaf[/]# ");
                bytes_received = recv(_client_socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
                //Debug_printv("Received Data");

                // Error occured during receiving
                if (bytes_received < 0)
                {
                    Debug_printv("Waiting for data");
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    break;
                }
                // Connection closed
                else if (bytes_received == 0)
                {
                    Debug_printv("Connection closed");
                    break;
                }
                // Data received
                else
                {
                    // Get the sender's ip address as string
                    if (sourceAddr.sin_family == PF_INET)
                    {
                        inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                    }

                    line += std::string(rx_buffer);
                    //send("[" + mstr::toHex(line) + "]");

                    // break line on newline and send to console
                    if (mstr::endsWith(line, "\r") || mstr::endsWith(line, "\n"))
                    {
                        mstr::rtrim(line);
                        console.execute(line.c_str());  // replace with receive() callback
                        line.clear();
                    } else if (line[0] == 0xBF || line[0] == 0xEF || line[0] == 0xFF) {
                        line.clear();
                    }

                    //rx_buffer[bytes_received] = 0; // Null-terminate whatever we received and treat like a string
                    //Debug_printv("Received %d bytes from %s:", bytes_received, addr_str);
                    //Debug_printf("%s", rx_buffer);

                    // Clear rx_buffer, and fill with zero's
                    bzero(rx_buffer, sizeof(rx_buffer));
                }
            }
            close(_client_socket);
        }
    }
    close(_server_socket);
    vTaskDelete(_htask);
    Debug_printv("TCP Server task ended");
}


void TCPServer::start()
{
    _shutdown = false;

    // Start tcp server task
    if (xTaskCreatePinnedToCore(&TCPServer::task, "console_tcp", 4096, NULL, 5, &_htask, 0) != pdTRUE)
    {
        Debug_printv("Could not start tcp server task!");
    }
}

void TCPServer::stop()
{
    Debug_printv("Stopping tcp server task");
    _shutdown = true;

    // close(_client_socket);
    // close(_server_socket);
    vTaskDelete(_htask);
    Debug_printv("TCP Server task ended");
}

void TCPServer::send(std::string data)
{
    if (_client_socket > 0)
    {
        write(_client_socket, data.c_str(), data.length());
    }
}

#endif // ENABLE_CONSOLE && ENABLE_CONSOLE_TCP