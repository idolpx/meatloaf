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
 * @brief Initialize the esp_console component
 *
 * linenoise is not configured: the REPL reads its own lines (Console::readLine)
 * and never calls linenoise().
 */
void initialize_console_library(void);

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
