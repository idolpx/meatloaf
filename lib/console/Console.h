#pragma once

#include "esp_console.h"

#include "ConsoleCommandBase.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "linenoise/linenoise.h"

#include "../../include/debug.h"

#define CONSOLE_UART        UART_NUM_0
#define MAX_READ_WAIT_TICKS 200

namespace ESP32Console
{
    class Console
    {
    private:
        const char *prompt_ = "ESP32> ";
        const uint32_t task_priority_;
        const BaseType_t task_stack_size_;
        bool _initialized = false;

        uint16_t max_history_len_ = 40;
        const char* history_save_path_ = nullptr;

        const size_t max_cmdline_len_;
        const size_t max_cmdline_args_;

        volatile bool _exit_requested = false;

        uart_port_t uart_channel_;

        TaskHandle_t task_ = nullptr;

        //QueueHandle_t queue;
        //std::vector<std::string> queue_lines;
        static void repl_task(void *args);
        static void watch_task(void *args);

        void beginCommon();
        size_t _print_number(unsigned long n, uint8_t base);

    public:
        // Where the command currently being executed was submitted from.
        enum Origin { ORIGIN_NONE = 0, ORIGIN_SERIAL, ORIGIN_REMOTE };

    private:
        // Persistent command-executor task. All console commands run on its
        // 16 KB stack (created once at boot while contiguous internal RAM is
        // guaranteed — task stacks cannot live in PSRAM). The REPL and TCP
        // session tasks are thin I/O shells that submit lines here and can
        // therefore use small stacks.
        TaskHandle_t exec_task_ = nullptr;
        SemaphoreHandle_t exec_mutex_ = nullptr;   // serializes submitters
        SemaphoreHandle_t exec_start_ = nullptr;   // command handed to worker
        SemaphoreHandle_t exec_done_  = nullptr;   // worker finished command
        const char *exec_line_ = nullptr;
        int exec_ret_ = 0;
        esp_err_t exec_err_ = ESP_OK;
        volatile Origin exec_origin_ = ORIGIN_NONE;
        static void exec_task_fn(void *args);
        void startExecutor();

    public:
        /**
         * @brief Create a new ESP32Console with the default parameters.
         * The REPL task only does linenoise I/O — commands run on the
         * executor task's 16 KB stack — so 6 KB is enough here.
         */
        Console(const uint32_t task_stack_size = 6144, const BaseType_t task_priority = 4, int max_cmdline_len = 256, int max_cmdline_args = 8) : task_priority_(task_priority), task_stack_size_(task_stack_size), max_cmdline_len_(max_cmdline_len), max_cmdline_args_(max_cmdline_args)
        {
            //queue = xQueueCreate( 10, sizeof( int ) );
        };

        ~Console()
        {
            if (task_ != nullptr)
                vTaskDelete(task_);
            end();
        }

        /**
         * @brief Register the given command, using the raw ESP-IDF structure.
         *
         * @param cmd The command that should be registered
         * @return Return true, if the registration was successfull, false if not.
         */
        bool registerCommand(const esp_console_cmd_t *cmd);

        /**
         * @brief Register the given command
         *
         * @param cmd The command that should be registered
         * @return true If the command was registered successful.
         * @return false If the command was not registered because of an error.
         */
        bool registerCommand(const ConsoleCommandBase &cmd);

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
        bool registerCommand(const char *command, esp_console_cmd_func_t func, const char *help, const char *hint = "");

        void registerCoreCommands();

        void registerSystemCommands();

        void registerDisplayCommands();

        void registerIECCommands();

        void registerNetworkCommands();

        void registerVFSCommands();

        void registerGPIOCommands();

        void registerXFERCommands();

        /**
         * @brief Set the command prompt. Default is "ESP32>".
         *
         * @param prompt
         */
        void setPrompt(const char *prompt) { prompt_ = prompt; };

        /**
         * @brief Set the History Max Length object
         * 
         * @param max_length 
         */
        void setHistoryMaxLength(uint16_t max_length)
        {
            max_history_len_ = max_length;
            linenoiseHistorySetMaxLen(max_length);
        }

        /**
         * @brief Enable saving of command history, which makes history persistent over resets. SPIFF need to be enabled, or you need to pass the filename to use.
         *
         * @param history_save_path The file which will be used to save command history. Set to nullptr to disable persistent saving
         */
        void enablePersistentHistory(const char *history_save_path = "/spiffs/.history.txt") { history_save_path_ = history_save_path; };

        /**
         * @brief Starts the console. Similar to the Serial.begin() function
         * 
         * @param baud The baud rate with which the console should work. Recommended: 115200
         * @param rxPin The pin to use for RX
         * @param txPin The pin to use for TX
         * @param channel The number of the UART to use
         */
        void begin(int baud, int rxPin = -1, int txPin = -1, uart_port_t channel = UART_NUM_0);

        /**
         * @brief Defer the REPL task until the first byte arrives on the console.
         * A minimal watcher task waits for input, then calls startRepl() and exits,
         * keeping the large REPL stack out of internal RAM until actually needed.
         */
        void startOnDemand();

        /**
         * @brief Start the REPL task immediately. Safe to call more than once.
         */
        void startRepl();

        /**
         * @brief Ask the running REPL task to exit after the current command.
         * The REPL frees its stack and re-arms startOnDemand(), so the next
         * byte of console input brings it back.
         */
        void requestExit() { _exit_requested = true; }

        /**
         * @brief True when called from the REPL task itself (e.g. to let the
         * "exit" command distinguish serial REPL from TCP session context).
         */
        bool inReplTask() const { return task_ != nullptr && task_ == xTaskGetCurrentTaskHandle(); }

        /**
         * @brief Run a command line on the executor task and wait for it to
         * finish. Serializes commands from all consoles (serial/TCP/WS).
         */
        esp_err_t runCommand(const char *line, int *ret, Origin origin);

        /**
         * @brief Origin of the command currently executing on the executor
         * task (e.g. lets "exit" distinguish serial REPL from TCP session).
         */
        Origin execOrigin() const { return exec_origin_; }

        void end();

        void execute(const char *command);

        // Called by the TCP server when a client connects / disconnects.
        // Installs (or removes) the stdout tee that mirrors all output to TCP.
        void tcpBegin();
        void tcpEnd();


        size_t write(uint8_t);
        size_t write(const uint8_t *buffer, size_t size);
        size_t write(const char *s);
    
        size_t write(unsigned long n) { return write((uint8_t)n); };
        size_t write(long n) { return write((uint8_t)n); };
        size_t write(unsigned int n) { return write((uint8_t)n); };
        size_t write(int n) { return write((uint8_t)n); };

        size_t lprint(const char *str);
        size_t lprint(const std::string &str);

        size_t printf(const char *format, ...);

        //size_t println(const char *format, ...);
        size_t println(const char *str);
        size_t println() { return print("\r\n"); };
        size_t println(std::string str);
        size_t println(int num, int base = 10);

        //size_t print(const char *format, ...);
        size_t print(const char *str);
        size_t print(const std::string &str);
        size_t print(int n, int base = 10);
        size_t print(unsigned int n, int base = 10);
        size_t print(long n, int base = 10);
        size_t print(unsigned long n, int base = 10);
    };
};

extern ESP32Console::Console console;