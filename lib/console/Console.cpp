#include "Console.h"

#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "Commands/CoreCommands.h"
#include "Commands/SystemCommands.h"
#include "Commands/NetworkCommands.h"
#include "Commands/VFSCommands.h"
#include "Commands/GPIOCommands.h"
#include "Commands/XFERCommands.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include "Helpers/PWDHelpers.h"
#include "Helpers/InputParser.h"

#include "../../include/debug.h"
#include "string_utils.h"

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
#ifdef ENABLE_DISPLAY
        registerCommand(getLEDCommand());
#endif
    }

    void Console::registerSystemCommands()
    {
        registerCommand(getSysInfoCommand());
        registerCommand(getRestartCommand());
        registerCommand(getMemInfoCommand());
        registerCommand(getTaskInfoCommand());
        registerCommand(getDateCommand());
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

    void Console::begin(int baud, int rxPin, int txPin, uint8_t channel)
    {
        //Debug_printv("Initialize console");

        if (channel >= SOC_UART_NUM)
        {
            Debug_printv("Serial number is invalid, please use numers from 0 to %u", SOC_UART_NUM - 1);
            return;
        }

        this->uart_channel_ = channel;

        //Reinit the UART driver if the channel was already in use
        if (uart_is_driver_installed(channel)) {
            uart_driver_delete(channel);
        }

        /* Drain stdout before reconfiguring it */
        fflush(stdout);
        fsync(fileno(stdout));

        /* Disable buffering on stdin */
        setvbuf(stdin, NULL, _IONBF, 0);

        /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
        esp_vfs_dev_uart_port_set_rx_line_endings(channel, ESP_LINE_ENDINGS_CR);
        /* Move the caret to the beginning of the next line on '\n' */
        esp_vfs_dev_uart_port_set_tx_line_endings(channel, ESP_LINE_ENDINGS_CRLF);

        /* Enable non-blocking mode on stdin and stdout */
        fcntl(fileno(stdout), F_SETFL, 0);
        fcntl(fileno(stdin), F_SETFL, 0);


        /* Configure UART. Note that REF_TICK is used so that the baud rate remains
         * correct while APB frequency is changing in light sleep mode.
         */
        const uart_config_t uart_config = {
            .baud_rate = baud,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .source_clk = UART_SCLK_DEFAULT,
        };
    

        ESP_ERROR_CHECK(uart_param_config(channel, &uart_config));

        // Set the correct pins for the UART of needed
        if (rxPin > 0 || txPin > 0) {
            if (rxPin < 0 || txPin < 0) {
                Debug_printv("Both rxPin and txPin has to be passed!");
            }
            uart_set_pin(channel, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        }

        /* Install UART driver for interrupt-driven reads and writes */
        ESP_ERROR_CHECK(uart_driver_install(channel, 256, 0, 0, NULL, 0));

        /* Tell VFS to use UART driver */
        esp_vfs_dev_uart_use_driver(channel);

        esp_console_config_t console_config = {
            .max_cmdline_length = max_cmdline_len_,
            .max_cmdline_args = max_cmdline_args_,
            .hint_color = 333333
        };

        ESP_ERROR_CHECK(esp_console_init(&console_config));

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

        // Probe terminal status
        int probe_status = linenoiseProbe();
        if (probe_status)
        {
            linenoiseSetDumbMode(1);
        }

        // if (linenoiseIsDumbMode())
        // {
        //     printf("\r\n"
        //            "Your terminal application does not support escape sequences.\n\n"
        //            "Line editing and history features are disabled.\n\n"
        //            "On Windows, try using Putty instead.\r\n");
        // }

        linenoiseSetMaxLineLen(console.max_cmdline_len_);
        while (true)
        {
            std::string prompt = console.prompt_;

            // Insert current PWD into prompt if needed
            mstr::replaceAll(prompt, "%pwd%", getCurrentPath()->url);

            tcp_server.send(prompt);
            char *line = linenoise(prompt.c_str());
            if (line == NULL)
            {
                //Debug_printv("empty line");
                /* Ignore empty lines */
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
            std::string interpolated_line = interpolateLine(line);
            //Debug_printv("Interpolated line: [%s]\n", interpolated_line.c_str());

            // Flush trailing CR
            uart_flush(CONSOLE_UART);

            /* Try to run the command */
            int ret;
            tcp_server.send(interpolated_line + "\n");
            esp_err_t err = esp_console_run(interpolated_line.c_str(), &ret);

            //Reset global state
            resetAfterCommands();

            if (err == ESP_ERR_NOT_FOUND)
            {
                std::string t = "Unrecognized command\n";
                tcp_server.send(t);
                fprintf(stdout, t.c_str());
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
        //Debug_printv("Executing command: [%s]\n", command);

        //Interpolate the input line
        std::string interpolated_line = interpolateLine(command);
        //Debug_printv("Interpolated line: [%s]\n", interpolated_line.c_str());

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(interpolated_line.c_str(), &ret);

        //Reset global state
        resetAfterCommands();

        if (err == ESP_ERR_NOT_FOUND)
        {
            std::string t = "Unrecognized command\n";
            tcp_server.send(t);
            printf(t.c_str());
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
        printf(prompt.c_str());
    }

    size_t Console::write(uint8_t c)
    {
        int z = uart_write_bytes(uart_channel_, (const char *)&c, 1);
        // uart_wait_tx_done(_uart_num, MAX_WRITE_BYTE_TICKS);
        tcp_server.send((char *)c);
        return z;
    }
    
    size_t Console::write(const uint8_t *buffer, size_t size)
    {
        int z = uart_write_bytes(uart_channel_, (const char *)buffer, size);
        // uart_wait_tx_done(_uart_num, MAX_WRITE_BUFFER_TICKS);
        tcp_server.send((char *)buffer);
        return z;
    }
    
    size_t Console::write(const char *str)
    {
        int z = uart_write_bytes(uart_channel_, str, strlen(str));
        tcp_server.send(str);
        return z;
    }
    
    

    size_t Console::printf(const char *fmt...)
    {
        char *result = nullptr;
        va_list vargs;
    
        if (!_initialized)
            return -1;
    
        va_start(vargs, fmt);
    
        int z = vasprintf(&result, fmt, vargs);
    
        if (z > 0) {
            uart_write_bytes(uart_channel_, result, z);
            tcp_server.send(result);
        }
    
        va_end(vargs);
    
        if (result != nullptr)
            free(result);
    
        // uart_wait_tx_done(uart_channel_, MAX_WRITE_BUFFER_TICKS);
    
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
        
        tcp_server.send(str);
        return uart_write_bytes(uart_channel_, str, z);
        ;
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