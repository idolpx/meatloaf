#include "Console.h"

#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "Commands/CoreCommands.h"
#include "Commands/DisplayCommands.h"
#include "Commands/SystemCommands.h"
#include "Commands/IECCommands.h"
#include "Commands/NetworkCommands.h"
#include "Commands/VFSCommands.h"
#include "Commands/GPIOCommands.h"
#include "Commands/XFERCommands.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#ifdef CONFIG_IDF_TARGET_ESP32S3
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif
#include "linenoise/linenoise.h"
#include "Helpers/PWDHelpers.h"
#include "Helpers/InputParser.h"

#include "../../include/debug.h"
#include "string_utils.h"
#include "console_settings.h"

#include "tcpsvr.h"

ESP32Console::Console console;

using namespace ESP32Console::Commands;

namespace ESP32Console
{
    /**
     * @brief Register the given command, using the raw ESP-IDF structure.
     *
     * @param cmd The command that should be registered
     * @return Return true, if the registration was successfull, false if not.
     */
    bool Console::registerCommand(const esp_console_cmd_t *cmd)
    {
        //Debug_printv("Registering new command %s", cmd->command);

        auto code = esp_console_cmd_register(cmd);
        if (code != ESP_OK)
        {
            Debug_printv("Error registering command (Reason %s)", esp_err_to_name(code));
            return false;
        }

        return true;
    }

    /**
     * @brief Register the given command
     *
     * @param cmd The command that should be registered
     * @return true If the command was registered successful.
     * @return false If the command was not registered because of an error.
     */
    bool Console::registerCommand(const ConsoleCommandBase &cmd)
    {
        auto c = cmd.toCommandStruct();
        return registerCommand(&c);
    }

    /**
     * @brief Registers the given command
     *
     * @param command The name under which the command can be called (e.g. "ls"). Must not contain spaces.
     * @param func A pointer to the function which should be run, when this command is called
     * @param help A text shown in output of "help" command describing this command. When empty it is not shown in help.
     * @param hint A text describing the usage of the command in help output
     * @return true If the command was registered successful.
     * @return false If the command was not registered because of an error.
     */
    bool Console::registerCommand(const char *command, esp_console_cmd_func_t func, const char *help, const char *hint)
    {
        const esp_console_cmd_t cmd = {
            .command = command,
            .help = help,
            .hint = hint,
            .func = func,
            .argtable = nullptr
        };

        return registerCommand(&cmd);
    };

    void Console::registerCoreCommands()
    {
        registerCommand(getClearCommand());
        registerCommand(getHistoryCommand());
        registerCommand(getEchoCommand());
        registerCommand(getSetMultilineCommand());
        registerCommand(getEnvCommand());
        registerCommand(getDeclareCommand());
        registerCommand(getRunCommand());
    }

    void Console::registerSystemCommands()
    {
        registerCommand(getSysInfoCommand());
        registerCommand(getRestartCommand());
        registerCommand(getMemInfoCommand());
        registerCommand(getTaskInfoCommand());
        registerCommand(getDateCommand());
    }

    void Console::registerDisplayCommands()
    {
#ifdef ENABLE_DISPLAY
        registerCommand(getLEDCommand());
#endif
    }

    void Console::registerIECCommands()
    {
        registerCommand(getIECDetectCommand());
    }

    void ESP32Console::Console::registerNetworkCommands()
    {
        registerCommand(getPingCommand());
        registerCommand(getIpconfigCommand());
        registerCommand(getScanCommand());
        registerCommand(getConnectCommand());
        registerCommand(getIMPROVCommand());
    }

    void Console::registerVFSCommands()
    {
        registerCommand(getCatCommand());
        registerCommand(getHexCommand());
        registerCommand(getCDCommand());
        registerCommand(getPWDCommand());
        registerCommand(getLsCommand());
        registerCommand(getMvCommand());
        registerCommand(getCPCommand());
        registerCommand(getRMCommand());
        registerCommand(getRMDirCommand());
        registerCommand(getMKDirCommand());
        registerCommand(getEditCommand());
        registerCommand(getMountCommand());
        registerCommand(getWgetCommand());
        registerCommand(getUpdateCommand());
        registerCommand(getEnableCommand());
        registerCommand(getDisableCommand());
    }

    void Console::registerGPIOCommands()
    {
        registerCommand(getPinModeCommand());
        registerCommand(getDigitalReadCommand());
        registerCommand(getDigitalWriteCommand());
        registerCommand(getAnalogReadCommand());
    }

    void Console::registerXFERCommands()
    {
        registerCommand(getRXCommand());
        registerCommand(getTXCommand());
    }


    void Console::beginCommon()
    {
        // Match ESP-IDF console example defaults for line editing behavior.
        linenoiseSetMultiLine(1);
        linenoiseAllowEmpty(false);

        /* Tell linenoise where to get command completions and hints */
        linenoiseSetCompletionCallback(&esp_console_get_completion);
        linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint);

        /* Set command history size */
        linenoiseHistorySetMaxLen(max_history_len_);

        /* Set command maximum length */
        linenoiseSetMaxLineLen(max_cmdline_len_);

        // Load history if defined
        if (history_save_path_)
        {
            linenoiseHistoryLoad(history_save_path_);
        }

        // Register core commands like echo
        esp_console_register_help_command();
        registerCoreCommands();
    }

    void Console::begin(int baud, int rxPin, int txPin, uart_port_t channel)
    {
        //Debug_printv("Initialize console");

        (void)baud;
        (void)rxPin;
        (void)txPin;
        (void)channel;

        // Use shared ESP-IDF style console setup for peripheral + stdio behavior.
        initialize_console_peripheral();

        // Initialize linenoise + esp_console using the shared settings module.
        initialize_console_library(history_save_path_);

        beginCommon();

        // Start REPL task
        _initialized = true;
        if (xTaskCreatePinnedToCore(&Console::repl_task, "console_repl", task_stack_size_, this, task_priority_, &task_, 0) != pdTRUE)
        {
            Debug_printv("Could not start REPL task!");
            _initialized = false;
        }
    }

    static void resetAfterCommands()
    {
        //Reset all global states a command could change

        //Reset getopt parameters
        optind = 0;
    }

    void Console::repl_task(void *args)
    {
        Console const &console = *(static_cast<Console *>(args));

        /* Change standard input and output of the task if the requested UART is
         * NOT the default one. This block will replace stdin, stdout and stderr.
         * We have to do this in the repl task (not in the begin, as these settings are only valid for the current task)
         */
        // if (console.uart_channel_ != CONFIG_ESP_CONSOLE_UART_NUM)
        // {
        //     char path[13] = {0};
        //     snprintf(path, 13, "/dev/uart/%1d", console.uart_channel_);

        //     stdin = fopen(path, "r");
        //     stdout = fopen(path, "w");
        //     stderr = stdout;
        // }

        //setvbuf(stdin, NULL, _IONBF, 0);

        /* This message shall be printed here and not earlier as the stdout
         * has just been set above. */
        // printf("\r\n"
        //        "Type 'help' to get the list of commands.\r\n"
        //        "Use UP/DOWN arrows to navigate through command history.\r\n"
        //        "Press TAB when typing command name to auto-complete.\r\n");

        // Do not force dumb mode here. On some USB monitor setups this causes
        // rapid empty reads and prompt flooding.

        // if (linenoiseIsDumbMode())
        // {
        //     printf("\r\n"
        //            "Your terminal application does not support escape sequences.\n\n"
        //            "Line editing and history features are disabled.\n\n"
        //            "On Windows, try using Putty instead.\r\n");
        // }

        // Keep stdin in blocking mode inside the REPL task to avoid a prompt spin
        // when USB CDC reconnects briefly return no data.
        int stdin_flags = fcntl(fileno(stdin), F_GETFL, 0);
        if (stdin_flags >= 0)
        {
            fcntl(fileno(stdin), F_SETFL, stdin_flags & ~O_NONBLOCK);
        }

        linenoiseSetMaxLineLen(console.max_cmdline_len_);
        while (true)
        {
            std::string prompt = console.prompt_;

            // Insert current PWD into prompt if needed
            mstr::replaceAll(prompt, "%pwd%", getCurrentPath()->url);
#ifdef ENABLE_CONSOLE_TCP
            tcp_server.send(prompt);
#endif
            char *line = linenoise(prompt.c_str());
            if (line == NULL)
            {
                // Avoid tight-looping when input is temporarily unavailable.
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            // Ignore empty/whitespace-only input lines.
            std::string raw_line = line;
            mstr::trim(raw_line);
            if (raw_line.empty())
            {
                linenoiseFree(line);
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            //Debug_printv("Line received from linenoise: [%s]\n", line);

            // /* Add the command to the history */
            // linenoiseHistoryAdd(line);
            
            // /* Save command history to filesystem */
            // if (console.history_save_path_)
            // {
            //     linenoiseHistorySave(console.history_save_path_);
            // }

            //Interpolate the input line
            std::string interpolated_line = interpolateLine(raw_line.c_str());

            /* Try to run the command */
            int ret;
#ifdef ENABLE_CONSOLE_TCP
            {
                std::string _tcp_line;
                _tcp_line.reserve(interpolated_line.size() + 1);
                _tcp_line = interpolated_line;
                _tcp_line += '\n';
                tcp_server.send(_tcp_line);
            }
#endif
            esp_err_t err = esp_console_run(interpolated_line.c_str(), &ret);

            //Reset global state
            resetAfterCommands();

            if (err == ESP_ERR_NOT_FOUND)
            {
#ifdef ENABLE_CONSOLE_TCP
                tcp_server.send("Unrecognized command\n");
#endif
                fprintf(stdout, "Unrecognized command\n");
            }
            else if (err == ESP_ERR_INVALID_ARG)
            {
                // command was empty
            }
            else if (err == ESP_OK && ret != ESP_OK)
            {
                fprintf(stdout, "Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
            }
            else if (err != ESP_OK)
            {
                fprintf(stdout, "Internal error: %s\n", esp_err_to_name(err));
            }
            /* linenoise allocates line buffer on the heap, so need to free it */
            linenoiseFree(line);
        }
        //Debug_printv("REPL task ended");
        vTaskDelete(NULL);
        esp_console_deinit();
    }

    void Console::end()
    {
        // what do we need to do when exiting?
    }

    void Console::execute(const char *command)
    {
        if (command == nullptr)
        {
            return;
        }

        std::string command_str = command;
        mstr::trim(command_str);
        if (command_str.empty())
        {
            return;
        }

        //Debug_printv("Executing command: [%s]\n", command);
        lprint(command_str);
        lprint("\n");

        //Interpolate the input line
        std::string interpolated_line = interpolateLine(command_str.c_str());
        //Debug_printv("Interpolated line: [%s]\n", interpolated_line.c_str());

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(interpolated_line.c_str(), &ret);

        //Reset global state
        resetAfterCommands();

        if (err == ESP_ERR_NOT_FOUND)
        {
            printf("Unrecognized command\n");
        }
        else if (err == ESP_ERR_INVALID_ARG)
        {
            // command was empty
        }
        else if (err == ESP_OK && ret != ESP_OK)
        {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        }
        else if (err != ESP_OK)
        {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }

        // Insert current PWD into prompt if needed
        std::string prompt = console.prompt_;
        mstr::replaceAll(prompt, "%pwd%", getCurrentPath()->url);
        print(prompt.c_str());
    }

    size_t Console::write(uint8_t c)
    {
        int z = fwrite(&c, 1, 1, stdout);
#ifdef ENABLE_CONSOLE_TCP
        tcp_server.send(std::string((const char *)&c, 1));
#endif
        return z;
    }
    
    size_t Console::write(const uint8_t *buffer, size_t size)
    {
        int z = fwrite(buffer, 1, size, stdout);
#ifdef ENABLE_CONSOLE_TCP
        tcp_server.send((char *)buffer);
#endif
        return z;
    }
    
    size_t Console::write(const char *str)
    {
        int z = fwrite(str, 1, strlen(str), stdout);
#ifdef ENABLE_CONSOLE_TCP
        tcp_server.send(str);
#endif
        return z;
    }
    
    size_t Console::lprint(const char *str)
    {
        int z = strlen(str);
    
        if (!_initialized)
            return -1;

        return fwrite(str, 1, z, stdout);
    }
    
    size_t Console::lprint(const std::string &str)
    {
        if (!_initialized)
            return -1;
    
        return lprint(str.c_str());
    }

    size_t Console::printf(const char *fmt...)
    {
        if (!_initialized)
            return -1;
    
        va_list vargs;
        va_start(vargs, fmt);
        int z = vfprintf(stdout, fmt, vargs);
        va_end(vargs);
    
        return z >= 0 ? z : 0;
    }

    size_t Console::_print_number(unsigned long n, uint8_t base)
    {
        char buf[8 * sizeof(long) + 1]; // Assumes 8-bit chars plus zero byte.
        char *str = &buf[sizeof(buf) - 1];
    
        if (!_initialized)
            return -1;
    
        *str = '\0';
    
        // prevent crash if called with base == 1
        if (base < 2)
            base = 10;
    
        do
        {
            unsigned long m = n;
            n /= base;
            char c = m - base * n;
            *--str = c < 10 ? c + '0' : c + 'A' - 10;
        } while (n);
    
        return write(str);
    }
    
    size_t Console::print(const char *str)
    {
        int z = strlen(str);
    
        if (!_initialized)
            return -1;

#ifdef ENABLE_CONSOLE_TCP
        tcp_server.send(str);
#endif
        return fwrite(str, 1, z, stdout);
    }
    
    size_t Console::print(const std::string &str)
    {
        if (!_initialized)
            return -1;
    
        return print(str.c_str());
    }
    
    size_t Console::print(int n, int base)
    {
        if (!_initialized)
            return -1;
    
        return print((long)n, base);
    }
    
    size_t Console::print(unsigned int n, int base)
    {
        if (!_initialized)
            return -1;
    
        return print((unsigned long)n, base);
    }
    
    size_t Console::print(long n, int base)
    {
        if (!_initialized)
            return -1;
    
        if (base == 0)
        {
            return write(n);
        }
        else if (base == 10)
        {
            if (n < 0)
            {
                int t = print('-');
                n = -n;
                return _print_number(n, 10) + t;
            }
            return _print_number(n, 10);
        }
        else
        {
            return _print_number(n, base);
        }
    }
    
    size_t Console::print(unsigned long n, int base)
    {
        if (!_initialized)
            return -1;
    
        if (base == 0)
        {
            return write(n);
        }
        else
        {
            return _print_number(n, base);
        }
    }

    size_t Console::println(const char *str)
    {
        if (!_initialized)
            return -1;
    
        size_t n = print(str);
        n += println();
        return n;
    }
    
    size_t Console::println(std::string str)
    {
        if (!_initialized)
            return -1;
    
        size_t n = print(str);
        n += println();
        return n;
    }
    
    size_t Console::println(int num, int base)
    {
        if (!_initialized)
            return -1;
    
        size_t n = print(num, base);
        n += println();
        return n;
    }
};