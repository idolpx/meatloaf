/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize console peripheral type
 *
 * Console peripheral is based on sdkconfig settings
 *
 * UART                 CONFIG_ESP_CONSOLE_UART_DEFAULT
 * USB_OTG              CONFIG_ESP_CONSOLE_USB_CDC
 * USB_SERIAL_JTAG      CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
 *
 * @param baud UART baud rate to use. Overrides the sdkconfig
 *             CONFIG_ESP_CONSOLE_UART_BAUDRATE so a regenerated sdkconfig
 *             (e.g. reset to 115200) cannot silently break the console.
 *             Pass 0 to fall back to the sdkconfig value. Ignored for
 *             USB CDC / USB-serial-JTAG consoles (no baud concept).
 */
void initialize_console_peripheral(int baud);

/**
 * @brief Initialize linenoise and esp console
 *
 * This function initialize linenoise library and esp_console component,
 * also checks if the terminal supports escape sequences
 *
 * @param history_path Path to store command history
 */
void initialize_console_library(const char *history_path);

/**
 * @brief Initialize console prompt
 *
 * This function adds color code to the prompt (if the console supports escape sequences)
 *
 * @param prompt_str Prompt in form of string eg esp32s3>
 *
 * @return
 *     - pointer to initialized prompt
 */
char *setup_prompt(const char *prompt_str);

#ifdef __cplusplus
}
#endif
