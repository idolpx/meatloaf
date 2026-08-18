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
#ifdef ENABLE_CONSOLE
    console.requestExit();
#endif
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