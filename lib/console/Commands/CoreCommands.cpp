#include "./CoreCommands.h"
#include "linenoise/linenoise.h"
#include "soc/soc_caps.h"
//#include "argparse/argparse.hpp"

#include <string>

#include "display.h"
#include "string_utils.h"
#include "../../../src/ml_tests.h"

static int clear(int argc, char **argv)
{
    // If we are on a dumb terminal clearing does not work
    if (linenoiseProbe())
    {
        Serial.printf("\r\nYour terminal does not support escape sequences. Clearing screen does not work!\r\n");
        return EXIT_FAILURE;
    }

    linenoiseClearScreen();
    return EXIT_SUCCESS;
}

static int echo(int argc, char **argv)
{
    for (int n = 1; n<argc; n++)
    {
        Serial.printf("%s ", argv[n]);
    }
    Serial.printf("\r\n");

    return EXIT_SUCCESS;
}

static int set_multiline_mode(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("You have to give 'on' or 'off' as an argument!\r\n");
        return EXIT_FAILURE;
    }

    // Get argument
    auto mode = std::string(argv[1]);
    // Normalize
    mstr::toLower(mode);

    if (mode == "on")
    {
        linenoiseSetMultiLine(1);
    }
    else if (mode == "off")
    {
        linenoiseSetMultiLine(0);
    }
    else
    {
        Serial.printf("Unknown option. Pass 'on' or 'off' (without quotes)!\r\n");
        return EXIT_FAILURE;
    }

    Serial.printf("Multiline mode set.\r\n");

    return EXIT_SUCCESS;
}

static int history_channel = 0;

static int history(int argc, char **argv)
{
    // If arguments were passed check for clearing
    /*if (argc > 1)
    {
        if (strcasecmp(argv[1], "-c"))
        { // When -c option was detected clear history.
            linenoiseHistorySetMaxLen(0);
            Serial.printf("History cleared!\r\n");
            linenoiseHistorySetMaxLen(10);
            return EXIT_SUCCESS;
        }
        else
        {
            Serial.printf("Invalid argument. Use -c to clear history.\r\n");

            return EXIT_FAILURE;
        }
    }
    else*/
    { // Without arguments we just output the history
      // We use the ESP-IDF VFS to directly output the file to an UART. UART channel 0 has the path /dev/uart/0 and so on.
        char path[12] = {0};
        snprintf(path, 12, "/dev/uart/%d", history_channel);

        // If we found the correct one, let linoise save (output) them.
        linenoiseHistorySave(path);
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

extern char **environ;

static int env(int argc, char **argv)
{
    char **s = environ;

    for (; *s; s++)
    {
        Serial.printf("%s\r\n", *s);
    }
    return EXIT_SUCCESS;
}

static int declare(int argc, char **argv)
{
    if (argc != 3) {
        Serial.printf("Syntax: declare VAR short OR declare VARIABLE \"Long Value\"\r\n");
        return EXIT_FAILURE; 
    }

    setenv(argv[1], argv[2], 1);

    return EXIT_SUCCESS;
}

static int run(int argc, char **argv)
{
    if (argc < 2) {
        Serial.printf("run {test}\r\n");
        return EXIT_FAILURE; 
    }

    if (mstr::startsWith(argv[1], "test"))
    {
        runTestsSuite();
    }
    // else if (mstr::startsWith(argv[1], "send"))
    // {
    //     LEDS.send();
    // }
    else
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#ifdef ENABLE_DISPLAY
static int led(int argc, char **argv)
{
    if (argc < 2) {
        Serial.printf("led {idle|send|receive|activity|progress {0-100}|status {1-255}|speed {0-255}}\r\n");
        return EXIT_FAILURE; 
    }

    if (mstr::startsWith(argv[1], "idle"))
    {
        LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "send"))
    {
        LEDS.send();
    }
    else if (mstr::startsWith(argv[1], "receive"))
    {
        LEDS.receive();
    }
    else if (mstr::startsWith(argv[1], "activity"))
    {
        LEDS.activity = !LEDS.activity;
    }
    else if (mstr::startsWith(argv[1], "progress"))
    {
        if (argc == 3)
            LEDS.progress = atoi(argv[2]);
        else
            LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "status"))
    {
        if (argc == 3)
            LEDS.status(atoi(argv[2]));
        else
            LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "speed"))
    {
        if (argc == 3)
            LEDS.speed = atoi(argv[2]);
        else
            LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "pixel"))
    {
        int i = -1;
        if (mstr::isNumeric(argv[2]))
            i = atoi(argv[2]); // pixel index, '*' for all
        


        // if (argc == 6)
        //     uint16_t index = atoi(argv[2]);
        //     uint8_t r = atoi(argv[3]);
        //     uint8_t g = atoi(argv[4]);
        //     uint8_t b = atoi(argv[5]);
        //     if (i == -1) // all
        //         LEDS.fill_all((CRGB){.r=r, .g=g, .b=b});
        //     else
        //         LEDS.set_pixel(index, r, g, b);
        // else
        //     LEDS.idle();
    }
    else
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
#endif



namespace ESP32Console::Commands
{
    const ConsoleCommand getClearCommand()
    {
        return ConsoleCommand("clear", &clear, "Clears the screen using ANSI codes");
    }

    const ConsoleCommand getEchoCommand()
    {
        return ConsoleCommand("echo", &echo, "Echos the text supplied as argument");
    }

    const ConsoleCommand getSetMultilineCommand()
    {
        return ConsoleCommand("multiline_mode", &set_multiline_mode, "Sets the multiline mode of the console");
    }

    const ConsoleCommand getHistoryCommand(int uart_channel)
    {
        history_channel = uart_channel;
        return ConsoleCommand("history", &history, "Shows and clear command history (using -c parameter)");
    }

    const ConsoleCommand getEnvCommand()
    {
        return ConsoleCommand("env", &env, "List all environment variables.");
    }

    const ConsoleCommand getDeclareCommand()
    {
        return ConsoleCommand("declare", &declare, "Change enviroment variables");
    }

    const ConsoleCommand getRunCommand()
    {
        return ConsoleCommand("run", &run, "Run a command");
    }

#ifdef ENABLE_DISPLAY
    const ConsoleCommand getLEDCommand()
    {
        return ConsoleCommand("led", &led, "Change LED display settings");
    }
#endif
}