#include "tcpsvr.h"

#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
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

#ifdef ENABLE_CONSOLE
#include "../lib/console/ESP32Console.h"
#endif

#define MESSAGE "Welcome to Meatloaf!\r\n"
#define LISTENQ 2

TCPServer tcp_server;
int TCPServer::client_socket = -1;

void TCPServer::task(void *pvParameters)
{
    char rx_buffer[128];    // char array to store received data
    char addr_str[128];     // char array to store client IP
    int bytes_received;     // immediate bytes received
    int addr_family;        // Ipv4 address protocol variable

    int ip_protocol;
    int socket_id;
    int bind_err;
    int listen_error;

    std::string line;

    while (1)
    {
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);     //Change hostname to network byte order
        destAddr.sin_family = AF_INET;                    //Define address family as Ipv4
        destAddr.sin_port = htons(TCP_SERVER_PORT);       //Define PORT
        addr_family = AF_INET;                            //Define address family as Ipv4
        ip_protocol = IPPROTO_TCP;                        //Define protocol as TCP
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

        /* Create TCP socket*/
        socket_id = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (socket_id < 0)
        {
            Debug_printv("Unable to create socket: errno %d", errno);
            break;
        }
        //Debug_printv("Socket created");

        /* Bind a socket to a specific IP + port */
        bind_err = bind(socket_id, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (bind_err != 0)
        {
            Debug_printv("Socket unable to bind: errno %d", errno);
            break;
        }
        //Debug_printv("Socket binded");

        /* Begin listening for clients on socket */
        listen_error = listen(socket_id, 3);
        if (listen_error != 0)
        {
            Debug_printv("Error occured during listen: errno %d", errno);
            break;
        }
        //Debug_printv("Socket listening");

        while (1)
        {
            struct sockaddr_in sourceAddr; // Large enough for IPv4
            socklen_t addrLen = sizeof(sourceAddr);
            /* Accept connection to incoming client */
            client_socket = accept(socket_id, (struct sockaddr *)&sourceAddr, &addrLen);
            if (client_socket < 0)
            {
                Debug_printv("Unable to accept connection: errno %d", errno);
                break;
            }
            Debug_printv("Socket accepted");

            // Send Welcome message
            write(client_socket, MESSAGE, strlen(MESSAGE));

            //Optionally set O_NONBLOCK
            //If O_NONBLOCK is set then recv() will return, otherwise it will stall until data is received or the connection is lost.
            //fcntl(client_socket,F_SETFL,O_NONBLOCK);

            // Clear rx_buffer, and fill with zero's
            bzero(rx_buffer, sizeof(rx_buffer));
            vTaskDelay(500 / portTICK_PERIOD_MS);
            while(1)
            {
                //Debug_printv("Waiting for data");
                //send("meatloaf[/]# ");
                bytes_received = recv(client_socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
                //Debug_printv("Received Data");

                // Error occured during receiving
                if (bytes_received < 0)
                {
                    Debug_printv("Waiting for data");
                    vTaskDelay(100 / portTICK_PERIOD_MS);
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
                        console.execute(line.c_str());
                        line.clear();
                    } else if (line[0] == 0xFF) {
                        line.clear();
                    }

                    //rx_buffer[bytes_received] = 0; // Null-terminate whatever we received and treat like a string
                    //Debug_printv("Received %d bytes from %s:", bytes_received, addr_str);
                    //Debug_printf("%s", rx_buffer);

                    // Clear rx_buffer, and fill with zero's
                    bzero(rx_buffer, sizeof(rx_buffer));
                }
            }
        }
    }
    close(socket_id);
    vTaskDelete(NULL);
}


void TCPServer::start()
{    
    //xTaskCreate(&TCPServer::task,"tcp_server",4096,NULL,5,NULL);

    // Start tcp server task
    if (xTaskCreatePinnedToCore(&TCPServer::task, "tcp_server", 4096, NULL, 5, NULL, 0) != pdTRUE)
    {
        Debug_printv("Could not start tcp server task!");
    }
}

void TCPServer::stop()
{
    //vTaskDelete( NULL );
}