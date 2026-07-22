#include "./CoreCommands.h"
#include "linenoise/linenoise.h"
#include "soc/soc_caps.h"
//#include "argparse/argparse.hpp"

#include <string>

#include "string_utils.h"
#include "../../../src/ml_tests.h"
#include "mlConfig.h"
#include "Esp.h"
#include "tcpsvr.h"
#include <cstdio>
#include <getopt.h>
#include "esp_console.h"

// Defined in SystemCommands.cpp; reboot() below needs ESP.restart().
extern EspClass ESP;

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

// Runs a ".sh" script: one console command per line, executed sequentially.
// Blank lines and lines starting with '#' are skipped. Runs on the caller's
// own stack (the console executor when invoked as "run script.sh") via
// esp_console_run() directly, rather than console.runCommand() — submitting
// to the executor from within a command already running on it would
// deadlock (the executor can't service a nested submission to itself).
static int run_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        Serial.printf("Cannot open script: %s\r\n", path);
        return EXIT_FAILURE;
    }

    char line[256];
    int line_num = 0;
    int overall_ret = EXIT_SUCCESS;
    while (fgets(line, sizeof(line), f))
    {
        line_num++;
        std::string cmd = line;
        mstr::trim(cmd);
        if (cmd.empty() || cmd[0] == '#')
            continue;

        Serial.printf("%s\r\n", cmd.c_str());

        int ret = 0;
        esp_err_t err = esp_console_run(cmd.c_str(), &ret);
        if (err == ESP_ERR_NOT_FOUND)
        {
            Serial.printf("Line %d: unrecognized command\r\n", line_num);
            overall_ret = EXIT_FAILURE;
        }
        else if (err == ESP_OK && ret != 0)
        {
            Serial.printf("Line %d: command returned %d\r\n", line_num, ret);
            overall_ret = EXIT_FAILURE;
        }
        else if (err != ESP_OK && err != ESP_ERR_INVALID_ARG)
        {
            Serial.printf("Line %d: internal error: %s\r\n", line_num, esp_err_to_name(err));
            overall_ret = EXIT_FAILURE;
        }

        // Reset getopt state between commands, same as Console's resetAfterCommands().
        optind = 0;
    }

    fclose(f);
    return overall_ret;
}

static int run(int argc, char **argv)
{
    if (argc < 2) {
        Serial.printf("Usage: run test | run <script.sh>\r\n");
        return EXIT_FAILURE;
    }

    if (mstr::endsWith(argv[1], ".sh"))
    {
        return run_script(argv[1]);
    }
    else if (mstr::startsWith(argv[1], "test"))
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

static int reboot(int argc, char **argv)
{
    Serial.println("Saving configuration...");
    mlConfig.save();
    Serial.println("Rebooting...");
    ESP.restart();
    return EXIT_SUCCESS;
}

static int exit_console(int argc, char **argv)
{
    // Commands run on the shared executor task, so use the submission
    // origin (not the current task) to tell serial REPL from TCP.
#ifdef ENABLE_CONSOLE_TCP
    if (console.execOrigin() != ESP32Console::Console::ORIGIN_SERIAL)
    {
        // Submitted from a TCP session: just drop the client connection.
        tcp_server.disconnect();
        return EXIT_SUCCESS;
    }
#endif
    // Serial REPL: stop the REPL task and return to on-demand mode so
    // its stack is freed until the next byte of console input.
    console.requestExit();
    Debug_memory();
    return EXIT_SUCCESS;
}

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
        return ConsoleCommand("run", &run, "Run the test suite, or a \".sh\" script of console commands", "test | <script.sh>");
    }

    const ConsoleCommand getRebootCommand()
    {
        return ConsoleCommand("reboot", &reboot, "Reboot the system");
    }

    const ConsoleCommand getExitCommand()
    {
        return ConsoleCommand("exit", &exit_console, "Exit the console (serial: REPL stops until next input; TCP: disconnect)");
    }
}