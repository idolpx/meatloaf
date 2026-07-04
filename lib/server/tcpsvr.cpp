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

#define MESSAGE "Welcome to Meatloaf!\r\n\r\nmeatloaf[/]# "
#define LISTENQ 2

TCPServer tcp_server;
int TCPServer::_server_socket = -1;
int TCPServer::_client_socket = -1;
bool TCPServer::_shutdown = false;
TaskHandle_t TCPServer::_htask = NULL;
TaskHandle_t TCPServer::_session_htask = NULL;

// Persistent per-client session worker: runs the console command loop for
// one connected client per wakeup. Created ONCE in start() while internal
// RAM is still plentiful — task stacks must be contiguous internal DRAM (no
// PSRAM fallback), and allocating 16 KB on demand at connect time fails once
// the heap fragments ("Could not start tcp session task!" → accepted
// connections were dropped immediately). The listener wakes it for each
// accepted client; it handles the session, signals completion, and sleeps.
void TCPServer::session_task(void *pvParameters)
{
    while (true)
    {
        // Wait for the listener to hand over an accepted client.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        char rx_buffer[128];    // char array to store received data
        int bytes_received;     // immediate bytes received
        std::string line;

        // Disable Nagle so small console writes (prompts, command output) are
        // sent immediately rather than being held for a full segment.
        {
            int flag = 1;
            setsockopt(_client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        }

        // Install stdout tee for this client session.
        console.tcpBegin();

        // Send Welcome message
        {
            int n = write(_client_socket, MESSAGE, strlen(MESSAGE));
            if (n < 0)
                Debug_printv("Welcome write failed: errno %d", errno);
        }

        // Clear rx_buffer, and fill with zero's
        bzero(rx_buffer, sizeof(rx_buffer));
        vTaskDelay(500 / portTICK_PERIOD_MS);
        while (!_shutdown)
        {
            bytes_received = recv(_client_socket, rx_buffer, sizeof(rx_buffer) - 1, 0);

            // Error occured during receiving
            if (bytes_received < 0)
            {
                Debug_printv("recv failed: errno %d", errno);
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
                line += std::string(rx_buffer);

                // break line on newline and send to console
                if (mstr::endsWith(line, "\r") || mstr::endsWith(line, "\n"))
                {
                    mstr::rtrim(line);
                    console.execute(line.c_str());  // replace with receive() callback
                    line.clear();
                } else if (line[0] == 0xBF || line[0] == 0xEF || line[0] == 0xFF) {
                    line.clear();
                }

                // Clear rx_buffer, and fill with zero's
                bzero(rx_buffer, sizeof(rx_buffer));
            }
        }
        // Tear down the stdout tee when the client disconnects.
        console.tcpEnd();
        close(_client_socket);
        _client_socket = -1;

        // The listener is blocked on this notification until we send it, so
        // _htask is guaranteed valid here.
        xTaskNotifyGive(_htask);
    }
}

void TCPServer::task(void *pvParameters)
{
    char addr_str[128];     // char array to store client IP
    int addr_family;        // Ipv4 address protocol variable

    int ip_protocol;
    int bind_err = 0;
    int listen_error = 0;

    while (!_shutdown)
    {
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);     //Change hostname to network byte order
        destAddr.sin_family = AF_INET;                    //Define address family as Ipv4
        destAddr.sin_port = htons(TCP_SERVER_PORT);       //Define PORT
        addr_family = AF_INET;                            //Define address family as Ipv4
        ip_protocol = IPPROTO_TCP;                        //Define protocol as TCP
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

        // Release the previous listen socket (if any) before creating a new
        // one. Rebinding after a transient accept() failure would otherwise
        // hit EADDRINUSE forever: the leaked fd keeps the port in LISTEN
        // state while nobody accepts on it.
        if (_server_socket >= 0)
        {
            close(_server_socket);
            _server_socket = -1;
        }

        /* Create TCP socket*/
        _server_socket = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (_server_socket < 0)
        {
            // Transient failure (e.g. lwip socket/heap pressure): retry.
            // Exiting here would leave port 23 dead forever.
            Debug_printv("Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        //Debug_printv("Socket created");

        /* Bind a socket to a specific IP + port */
        bind_err = bind(_server_socket, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (bind_err != 0)
        {
            Debug_printv("Socket unable to bind: errno %d", errno);
            close(_server_socket);
            _server_socket = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        //Debug_printv("Socket binded");

        /* Begin listening for clients on socket */
        listen_error = listen(_server_socket, 3);
        if (listen_error != 0)
        {
            Debug_printv("Error occured during listen: errno %d", errno);
            close(_server_socket);
            _server_socket = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        Debug_printv("TCP console listening on port %d", TCP_SERVER_PORT);

        while (!_shutdown)
        {
            struct sockaddr_in sourceAddr; // Large enough for IPv4
            socklen_t addrLen = sizeof(sourceAddr);
            /* Accept connection to incoming client */
            _client_socket = accept(_server_socket, (struct sockaddr *)&sourceAddr, &addrLen);
            if (_client_socket < 0)
            {
                // Socket closed by stop() (WiFi drop) or transient failure:
                // fall out to the outer loop, which rebinds and keeps
                // listening. The task never exits on errors.
                //Debug_printv("Unable to accept connection: errno %d", errno);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
            }
            Debug_printv("Socket accepted");

            // Hand the client to the persistent session worker and wait
            // until the session finishes (one client at a time).
            if (_session_htask == NULL)
            {
                Debug_printv("No tcp session task - dropping client!");
                close(_client_socket);
                _client_socket = -1;
                continue;
            }
            xTaskNotifyGive(_session_htask);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
    close(_server_socket);
    //Debug_printv("TCP Server task ended (task)");
    _htask = NULL;       // Clear before self-delete so stop() won't double-delete
    vTaskDelete(NULL);   // Self-delete (NULL = calling task)
}


void TCPServer::start()
{
    _shutdown = false;

    // The listener task is persistent: it survives WiFi drops by rebinding,
    // so only create it once. Creating a second task here would race the
    // first over the shared listen socket and _htask.
    if (_htask != NULL)
        return;

    // Start tcp listener task. It only accepts connections; the per-client
    // session task (with the larger stack) is created on demand.
    if (xTaskCreatePinnedToCore(&TCPServer::task, "console_tcp", 2560, NULL, 5, &_htask, 0) != pdTRUE)
    {
        Debug_printv("Could not start tcp server task!");
    }
}

void TCPServer::stop()
{
    Debug_printf("Stopping tcp server...");

    // Called on WiFi drop: kick the client and listener off their sockets.
    // The listener task itself stays alive and rebinds — setting _shutdown
    // here raced GOT_IP's start() (old task exiting vs. new task starting)
    // and could leave port 23 permanently dead after a WiFi bounce.
    if (_client_socket > 0)
    {
        close(_client_socket);
        _client_socket = -1;
    }
    if (_server_socket > 0)
    {
        close(_server_socket);
        _server_socket = -1;
    }

    Debug_printf("done!\r\n");
}

void TCPServer::send(std::string data)
{
    if (_client_socket > 0)
    {
        write(_client_socket, data.c_str(), data.length());
    }
}

void TCPServer::disconnect()
{
    if (_client_socket > 0)
    {
        close(_client_socket);
        _client_socket = -1;
    }
}

#endif // ENABLE_CONSOLE && ENABLE_CONSOLE_TCP