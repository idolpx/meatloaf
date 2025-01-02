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

#include "string_utils.h"

#include "../Console.h"
#include "../Helpers/PWDHelpers.h"

char *canonicalize_file_name(const char *path);

int rx(int argc, char **argv)
{
    // rx {filename} {size} {checksum}
    if (argc != 4)
    {
        fprintf(stderr, "rx {filename} {size} {checksum}\r\n");
        return EXIT_SUCCESS;
    }

    char filename[PATH_MAX];
    ESP32Console::console_realpath(argv[1], filename);
    int size = atoi(argv[2]);
    std::string src_checksum = argv[3];

    FILE *file = fopen(filename, "w");
    if (file == nullptr)
    {
        fprintf(stdout, "2 Error: Can't open file!\r\n");
        return 2;
    }

    // Receive File
    int count = 0;
    uint8_t byte;
    int dest_checksum = 0;
    while (count < size)
    {
        size_t size;
        uart_get_buffered_data_len(CONSOLE_UART, &size);
        if (size > 0)
        {
            int result = uart_read_bytes(CONSOLE_UART, &byte, 1, MAX_READ_WAIT_TICKS);
            if (result < 1)
                break;

            fprintf(file, "%c", byte);

            // Calculate checksum
            dest_checksum = esp_rom_crc32_le(dest_checksum, &byte, 1);
            count++;
        }
    }
    fclose(file);

    // Check checksum
    std::ostringstream ss;
    ss << std::hex << dest_checksum;
    std::string dest_checksum_str = ss.str();
    if ( !mstr::compare(dest_checksum_str, src_checksum) )
    {
        fprintf(stdout, "2 Error: Checksum mismatch!\r\n");
        return 2;
    }

    fprintf(stdout, "0 OK\r\n");
    return EXIT_SUCCESS;
}

int tx(int argc, char **argv)
{
    // tx {filename}
    if (argc != 2)
    {
        fprintf(stderr, "tx {filename}\r\n");
        return EXIT_SUCCESS;
    }

    // Get file size
    char filename[PATH_MAX];
    ESP32Console::console_realpath(argv[1], filename);
    struct stat file_stat;
    stat(filename, &file_stat);
    int size = file_stat.st_size;

    // Calculate checksum
    int src_checksum = 0;
    FILE *file = fopen(filename, "r");
    if (file == nullptr)
    {
        fprintf(stdout, "2 Error: Can't open file!\r\n");
        return 2;
    }
    else
    {
        // Read file 256 bytes at a time and calculate checksum
        uint8_t buffer[256];
        int bytesRead = 0;
        while ((bytesRead = fread(buffer, 1, 256, file)) > 0)
        {
            src_checksum = esp_rom_crc32_le(src_checksum, buffer, bytesRead);
        }
        fseek(file, 0, SEEK_SET);
    }

    // send size and checksum
    fprintf(stdout, "%d %8x\r\n", size, src_checksum);

    // Send file 256 bytes at a time
    uint8_t buffer[256];
    int bytesRead = 0;
    while ((bytesRead = fread(buffer, 1, 256, file)) > 0)
    {
        // print buffer bytes
        for (int i = 0; i < bytesRead; i++) {
            fprintf(stdout, "%c", buffer[i]);
        }
    }
    fclose(file);

    fprintf(stdout, "\r\n");

    // Read response
    uint8_t byte;
    std::string response;
    while (byte != '\n')
    {
        size_t size;
        uart_get_buffered_data_len(CONSOLE_UART, &size);
        if (size > 0)
        {
            int result = uart_read_bytes(CONSOLE_UART, &byte, 1, MAX_READ_WAIT_TICKS);
            if (result < 1)
            {
                fprintf(stdout, "2 Error: Response Timeout\r\n");
                return 3;
            }

            response.push_back(byte);
        }
    }

    if (!mstr::startsWith(response, "0 OK"))
    {
        fprintf(stdout, "2 Error: %s\r\n", response.c_str());
        return 2;
    }

    return EXIT_SUCCESS;
}


namespace ESP32Console::Commands
{
    const ConsoleCommand getRXCommand()
    {
        return ConsoleCommand("rx", &rx, "Receive one or more files.");
    }

    const ConsoleCommand getTXCommand()
    {
        return ConsoleCommand("tx", &tx, "Send one or more files.");
    }
}