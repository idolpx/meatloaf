
#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <string>

#define TCP_SERVER_PORT 23 // 3000

class TCPServer 
{
private:
    static int client_socket;
    static void task(void *pvParameters);

public:
    void start();
    void stop();

    static void send(std::string data);
};

extern TCPServer tcp_server;

#endif