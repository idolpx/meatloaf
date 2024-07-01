
#include <esp_console.h>
#include "linenoise/linenoise.h"

// Main high-priority service loop
void fn_console_loop(void *param)
{
    esp_console_config_t  config = {
        .max_cmdline_length = 80,
        .max_cmdline_args = 10,
        .hint_color = 39
    };

    esp_err_t e = esp_console_init(&config);

    char* line;

    if(e == ESP_OK) {
        while((line = linenoise("hello> ")) != NULL) {
            printf("You wrote: %s\r\n", line);
            linenoiseFree(line); /* Or just free(line) if you use libc malloc. */
        }
    }

    esp_console_deinit();
}