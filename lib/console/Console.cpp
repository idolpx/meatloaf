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

#ifdef ENABLE_CONSOLE_TCP
// Tee FILE* installed on the TCP task's stdout for the duration of a client
// session. Non-null means a client is connected and all stdout writes go to
// both UART (via _tee_orig) and TCP (via tcp_server.send).
static FILE *_tee      = nullptr;
static FILE *_tee_orig = nullptr;

static ssize_t _stdout_tee_write(void *cookie, const char *buf, size_t n)
{
    fwrite(buf, 1, n, (FILE *)cookie);
    tcp_server.send(std::string(buf, n));
    return (ssize_t)n;
}
static cookie_io_functions_t _stdout_tee_fns = {
    .read = nullptr, .write = _stdout_tee_write, .seek = nullptr, .close = nullptr
};
#endif

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
        registerCommand(getRebootCommand());
        registerCommand(getMemInfoCommand());
        registerCommand(getTaskInfoCommand());
        registerCommand(getDateCommand());
        registerCommand(getConfigCommand());
    }

    void Console::registerDisplayCommands()
    {
#ifdef ENABLE_DISPLAY
        registerCommand(getLEDCommand());
        registerCommand(getShowCommand());
#endif
    }

    void Console::registerIECCommands()
    {
        registerCommand(getIECDetectCommand());
    }

    void ESP32Console::Console::registerNetworkCommands()
    {
        registerCommand(getPingCommand());
        registerCommand(getIfconfigCommand());
        registerCommand(getNetstatCommand());
        registerCommand(getScanCommand());
        registerCommand(getConnectCommand());
        registerCommand(getExitCommand());
#ifndef MIN_CONFIG
        registerCommand(getWsCommand());
#endif
    }

    void Console::registerVFSCommands()
    {
        registerCommand(getDFCommand());
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
        registerCommand(getAuthCommand());
        registerCommand(getWgetCommand());
        registerCommand(getUpdateCommand());
        registerCommand(getEnableCommand());
        registerCommand(getDisableCommand());
        registerCommand(getGzipCommand());
#ifndef MIN_CONFIG
        registerCommand(getUnzipCommand());
#endif
#ifdef SD_CARD
        registerCommand(getFormatSDCommand());
        registerCommand(getUpdatedbCommand());
        registerCommand(getLocateCommand());
#endif
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
        // Allow empty lines so linenoise returns "" instead of re-printing the
        // prompt internally. The REPL loop's own empty-line guard handles them,
        // preventing a double-prompt when the terminal sends \r\n (which produces
        // two newlines after ESP_LINE_ENDINGS_CR RX mapping).
        linenoiseAllowEmpty(true);

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

        // The console (stdio, esp_console, commands) is usable now; the REPL
        // task itself is started separately via startRepl()/startOnDemand()
        // so its stack is not allocated until the console is actually used.
        _initialized = true;
    }

    void Console::startRepl()
    {
        if (!_initialized || task_ != nullptr)
            return;

        if (xTaskCreatePinnedToCore(&Console::repl_task, "console_repl", task_stack_size_, this, task_priority_, &task_, 0) != pdTRUE)
        {
            Debug_printv("Could not start REPL task!");
            task_ = nullptr;
            startOnDemand();
        }
    }

    void Console::startOnDemand()
    {
        if (!_initialized || task_ != nullptr)
            return;

        if (xTaskCreatePinnedToCore(&Console::watch_task, "console_watch", 2560, this, 2, nullptr, 0) != pdTRUE)
        {
            Debug_printv("Could not start console watch task!");
            startRepl();
        }
    }

    void Console::watch_task(void *args)
    {
        Console *console = static_cast<Console *>(args);

        // Block until the first byte arrives on the console, then hand over
        // to the full REPL task. The byte (usually ENTER) is consumed; the
        // REPL prints its prompt as soon as it starts.
        int stdin_flags = fcntl(fileno(stdin), F_GETFL, 0);
        if (stdin_flags >= 0)
        {
            fcntl(fileno(stdin), F_SETFL, stdin_flags & ~O_NONBLOCK);
        }

        while (fgetc(stdin) == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        console->startRepl();
        vTaskDelete(NULL);
    }

    static void resetAfterCommands()
    {
        //Reset all global states a command could change

        //Reset getopt parameters
        optind = 0;
    }

    void Console::repl_task(void *args)
    {
        Console &console = *(static_cast<Console *>(args));

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
            char *line = linenoise(prompt.c_str());
            if (line == NULL)
            {
                // Avoid tight-looping when input is temporarily unavailable.
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            // ESP_LINE_ENDINGS_CR maps both \r and \n to \n, so a \r\n terminal
            // leaves a second \n in the buffer after linenoise consumes the first.
            // Drain it non-blocking to prevent a double prompt on the next call.
            {
                int fl = fcntl(fileno(stdin), F_GETFL, 0);
                fcntl(fileno(stdin), F_SETFL, fl | O_NONBLOCK);
                int ch = fgetc(stdin);
                fcntl(fileno(stdin), F_SETFL, fl);
                if (ch != '\n' && ch != EOF) ungetc(ch, stdin);
            }

            // Truncate to buffer size in case paste overflow corrupted length.
            line[console.max_cmdline_len_ - 1] = '\0';

            // Ignore empty/whitespace-only input lines.
            std::string raw_line = line;
            mstr::trim(raw_line);
            if (raw_line.empty())
            {
                linenoiseFree(line);
                continue;
            }

            //Debug_printv("Line received from linenoise: [%s]", line);

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
            esp_err_t err = esp_console_run(interpolated_line.c_str(), &ret);

            //Reset global state
            resetAfterCommands();

            if (err == ESP_ERR_NOT_FOUND)
            {
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

            if (console._exit_requested)
                break;
        }

        // "exit" was requested: free this task's stack and return to
        // on-demand mode. The watcher re-creates the REPL on the next byte
        // of console input.
        console._exit_requested = false;
        console.task_ = nullptr;
        ::printf("Console deactivated. Press ENTER to reactivate.\r\n");
        console.startOnDemand();
        vTaskDelete(NULL);
    }

    void Console::end()
    {
        // what do we need to do when exiting?
    }

    void Console::tcpBegin()
    {
#ifdef ENABLE_CONSOLE_TCP
        if (_tee) return; // already active
        _tee_orig = stdout;
        FILE *tee = fopencookie(_tee_orig, "w", _stdout_tee_fns);
        if (tee) {
            setvbuf(tee, nullptr, _IONBF, 0);
            stdout = tee;
            _tee = tee;
        }
#endif
    }

    void Console::tcpEnd()
    {
#ifdef ENABLE_CONSOLE_TCP
        if (!_tee) return;
        fflush(_tee);
        stdout = _tee_orig;
        fclose(_tee);
        _tee      = nullptr;
        _tee_orig = nullptr;
#endif
    }

    void Console::execute(const char *command)
    {
        if (command == nullptr)
        {
            return;
        }

        std::string command_str = command;
        mstr::trim(command_str);

        if (!command_str.empty())
        {
            lprint(command_str);
            lprint("\n");

            std::string interpolated_line = interpolateLine(command_str.c_str());

            int ret;
            esp_err_t err = esp_console_run(interpolated_line.c_str(), &ret);

            resetAfterCommands();

            if (err == ESP_ERR_NOT_FOUND)
                printf("Unrecognized command\n");
            else if (err == ESP_OK && ret != ESP_OK)
                printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
            else if (err != ESP_OK)
                printf("Internal error: %s\n", esp_err_to_name(err));
        }

#ifdef ENABLE_CONSOLE_TCP
        // Prompt goes to TCP only — the REPL loop owns the UART prompt via linenoise.
        {
            std::string p = prompt_;
            mstr::replaceAll(p, "%pwd%", getCurrentPath()->url);
            tcp_server.send(p);
        }
#endif
    }

    size_t Console::write(uint8_t c)
    {
        return fwrite(&c, 1, 1, stdout);
    }

    size_t Console::write(const uint8_t *buffer, size_t size)
    {
        return fwrite(buffer, 1, size, stdout);
    }

    size_t Console::write(const char *str)
    {
        return fwrite(str, 1, strlen(str), stdout);
    }

    size_t Console::lprint(const char *str)
    {
        if (!_initialized)
            return -1;

        size_t z = strlen(str);
        fwrite(str, 1, z, stdout);
#ifdef ENABLE_CONSOLE_TCP
        if (_tee == nullptr)
            tcp_server.send(std::string(str, z));
#endif
        return z;
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
#ifdef ENABLE_CONSOLE_TCP
        if (_tee == nullptr) {
            // No TCP tee active (REPL task context): forward to TCP explicitly.
            char *buf = nullptr;
            int z = vasprintf(&buf, fmt, vargs);
            va_end(vargs);
            if (z < 0 || !buf)
                return 0;
            fwrite(buf, 1, z, stdout);
            tcp_server.send(std::string(buf, z));
            free(buf);
            return z;
        }
#endif
        int z = vfprintf(stdout, fmt, vargs);
        va_end(vargs);
        return z < 0 ? 0 : z;
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
        if (!_initialized)
            return -1;

        return fwrite(str, 1, strlen(str), stdout);
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