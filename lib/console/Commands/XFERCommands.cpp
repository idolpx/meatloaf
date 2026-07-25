#include "XFERCommands.h"

#include <cstring>
#include <cerrno>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syslimits.h>
#include <esp_rom_crc.h>
#include <iostream>
#include <sstream>
#include <sys/fcntl.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline void *psram_malloc(size_t sz) {
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

#include "string_utils.h"

#include "../Console.h"
#include "../Helpers/PWDHelpers.h"
#include "../../include/debug.h"

#define ChunkSize              64   //bytes to send in chunk before ack
#define ChunkAckChar          '+'   //char sent to ack chunk

// Reads a single byte from the console's stdin, regardless of the underlying
// transport (UART, USB-Serial-JTAG, USB-CDC) - unlike driver-specific calls
// (e.g. uart_read_bytes on a hardcoded UART port), this works on every board.
// Same non-blocking poll idiom as the REPL's stdin handling in Console.cpp.
static bool console_read_byte(uint8_t *out, int timeout_ms)
{
    int fd = fileno(stdin);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int c = EOF;
    for (int waited = 0; waited < timeout_ms; waited += 10)
    {
        c = fgetc(stdin);
        if (c != EOF)
            break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (flags >= 0)
        fcntl(fd, F_SETFL, flags);

    if (c == EOF)
        return false;

    *out = (uint8_t)c;
    return true;
}

std::string read_until(char delimiter)
{
    uint8_t byte = 0;
    std::string response;
    while (byte != delimiter)
    {
        if (!console_read_byte(&byte, pdTICKS_TO_MS(MAX_READ_WAIT_TICKS)))
        {
            Serial.printf("3 Error: Response Timeout\r\n");
            return "";
        }

        if (byte != delimiter)
            response.push_back(byte);
    }
    return response;
}

int rx(int argc, char **argv)
{
    // rx {filename}
    if (argc != 2)
    {
        Serial.printf("rx {filename}\r\n");
        return EXIT_SUCCESS;
    }

    char filename[PATH_MAX];
    ESP32Console::console_realpath(argv[1], filename);

    // get file size and checksum
    std::string s = read_until(' ');
    int size = atoi(s.c_str());
    std::string src_checksum = read_until('\n');
    mstr::trim(src_checksum);

    FILE *file = fopen(filename, "w");
    if (file == nullptr)
    {
        Serial.printf("2 Error: Can't open file!\r\n");
        return 2;
    }

    // Receive File
    int count = 0;
    uint8_t byte = 0;
    int dest_checksum = 0;
    while (count < size)
    {
        if (!console_read_byte(&byte, pdTICKS_TO_MS(MAX_READ_WAIT_TICKS)))
        {
            Serial.printf("3 Error: Receive Timeout at %lu bytes\r\n", count);
            fclose(file);
            return 3;
        }

        fprintf(file, "%c", byte);
        Serial.printf("%02X", byte);

        // Calculate checksum
        dest_checksum = esp_rom_crc32_le(dest_checksum, &byte, 1);
        count++;
        if (count % ChunkSize == 0 || count == size) Serial.printf("%c", ChunkAckChar); // send ack character after every chunk
    }
    fclose(file);
    Serial.printf("[%d]\r\n", dest_checksum);

    // Check checksum
    std::ostringstream ss;
    ss << std::hex << dest_checksum;
    std::string dest_checksum_str = ss.str();
    if ( !mstr::equals(src_checksum, dest_checksum_str ) )
    {
        Serial.printf("2 Error: Checksum mismatch! src[%s][%d] != dest[%s][%d]\r\n", src_checksum.c_str(), src_checksum.length(), dest_checksum_str.c_str(), dest_checksum_str.length());
        return 2;
    }

    Serial.printf("0 OK\r\n");
    return EXIT_SUCCESS;
}

int tx(int argc, char **argv)
{
    // tx {filename}
    if (argc != 2)
    {
        Serial.printf("tx {filename}\r\n");
        return EXIT_SUCCESS;
    }

    // Get file size
    char filename[PATH_MAX];
    ESP32Console::console_realpath(argv[1], filename);
    struct stat file_stat;
    stat(filename, &file_stat);
    int size = file_stat.st_size;

    // Receive File
    uint8_t *buffer = (uint8_t *)psram_malloc(256);
    int bytesRead = 0;

    // Calculate checksum
    int src_checksum = 0;
    FILE *file = fopen(filename, "r");
    if (file == nullptr)
    {
        free(buffer);
        Serial.printf("2 Error: Can't open file!\r\n");
        return 2;
    }
    else
    {
        // Read file 256 bytes at a time and calculate checksum
        while ((bytesRead = fread(buffer, 1, 256, file)) > 0)
        {
            src_checksum = esp_rom_crc32_le(src_checksum, buffer, bytesRead);
        }
        fseek(file, 0, SEEK_SET);
    }

    // Send size and checksum
    Serial.printf("%d %8x\r\n", size, src_checksum);

    // Send file 256 bytes at a time, waiting for the receiver's chunk ack
    // (the '+' rx() sends back every ChunkSize bytes) before continuing -
    // mirrors the flow control rx() implements from the receiving side.
    int count = 0;
    while ((bytesRead = fread(buffer, 1, 256, file)) > 0)
    {
        // print buffer bytes
        for (int i = 0; i < bytesRead; i++) {
            Serial.printf("%c", buffer[i]);
            count++;
            if (count % ChunkSize == 0 || count == size)
            {
                uint8_t ack = 0;
                if (!console_read_byte(&ack, pdTICKS_TO_MS(MAX_READ_WAIT_TICKS)) || ack != ChunkAckChar)
                {
                    Serial.printf("\n3 Error: Ack Timeout at %d bytes\r\n", count);
                    fclose(file);
                    free(buffer);
                    return 3;
                }
            }
        }
    }
    fclose(file);
    free(buffer);

    // End file data with CRLF
    Serial.printf("\r\n");

    // Read response
    std::string response = read_until('\n');

    if (!mstr::startsWith(response, "0 OK"))
    {
        Serial.printf("2 Error: %s\r\n", response.c_str());
        return 2;
    }

    return EXIT_SUCCESS;
}


namespace ESP32Console::Commands
{
    const ConsoleCommand getRXCommand()
    {
        return ConsoleCommand("rx", &rx, "Receive file");
    }

    const ConsoleCommand getTXCommand()
    {
        return ConsoleCommand("tx", &tx, "Transmit file");
    }
}