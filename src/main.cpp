#include <esp_system.h>
#include <nvs_flash.h>
#include <esp32/spiram.h>
#include <esp32/himem.h>
#include <driver/gpio.h>
#include <esp_console.h>
#include "linenoise/linenoise.h"


#include "../include/global_defines.h"
#include "../include/debug.h"


#include "keys.h"
#include "led.h"

#ifdef LED_STRIP
//#include "feedback.h"
//#include "neopixel.h"
#include "display.h"
#endif

#include "sound.h"
#include "parallel.h"

#include "fnSystem.h"
#include "fnWiFi.h"


#ifdef FLASH_SPIFFS
#include "fnFsSPIFFS.h"
#elif FLASH_LITTLEFS
#include "fnFsLittleFS.h"
#endif
#include "fnFsSD.h"


/**************************/
// Meatloaf


#include "iec.h"
#include "ml_tests.h"


std::string statusMessage;
bool initFailed = false;


/**************************/

// fnSystem is declared and defined in fnSystem.h/cpp
// fnBtManager is declared and defined in fnBluetooth.h/cpp
// fnLedManager is declared and defined in led.h/cpp
// fnKeyManager is declared and defined in keys.h/cpp
// fnHTTPD is declared and defineid in HttpService.h/cpp

// sioFuji theFuji; // moved to fuji.h/.cpp


void main_shutdown_handler()
{
    Debug_println("Shutdown handler called");
    // Give devices an opportunity to clean up before rebooting

//    IEC.shutdown();
}

// Initial setup
void main_setup()
{
#ifdef DEBUG
    fnUartDebug.begin(DEBUG_SPEED);
    unsigned long startms = fnSystem.millis();
    
    Debug_printf( ANSI_WHITE "\n\n" ANSI_BLUE_BACKGROUND "==============================" ANSI_RESET_NL );
    Debug_printf( ANSI_BLUE_BACKGROUND "   " PRODUCT_ID " " FW_VERSION "   " ANSI_RESET_NL );
    Debug_printf( ANSI_BLUE_BACKGROUND "------------------------------" ANSI_RESET_NL "\n" );

    Debug_printf( "FujiNet %s Started @ %lu\n", fnSystem.get_fujinet_version(), startms );

    Debug_printf( "Starting heap: %u\n", fnSystem.get_free_heap_size() );

#ifndef NO_PSRAM
    Debug_printf( "PsramSize %u\n", fnSystem.get_psram_size() );

    Debug_printf( "himem phys %u\n", esp_himem_get_phys_size() );
    Debug_printf( "himem free %u\n", esp_himem_get_free_size() );
    Debug_printf( "himem reserved %u\n", esp_himem_reserved_area_size() );
#endif


#endif // DEBUG

    // Install a reboot handler
    esp_register_shutdown_handler(main_shutdown_handler);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        Debug_println("Erasing flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    // Enable GPIO Interrupt Service Routine
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    fnSystem.check_hardware_ver();
    Debug_printf("Detected Hardware Version: %s\n", fnSystem.get_hardware_ver_str());

    fnKeyManager.setup();
    fnLedManager.setup();

#ifdef FLASH_SPIFFS
    fnSPIFFS.start();
#elif FLASH_LITTLEFS
    fnLITTLEFS.start();
#endif
    fnSDFAT.start();

    // Load our stored configuration
//    Config.load();

    // Set up the WiFi adapter
    fnWiFi.start();
    // Go ahead and try reconnecting to WiFi
    fnWiFi.connect();


    // Setup IEC Bus
    IEC.setup();
    Serial.println( ANSI_GREEN_BOLD "IEC Bus Initialized" ANSI_RESET );

    // Add devices to bus
    IEC.enabledDevices = DEVICE_MASK;
    IEC.enableDevice(30);

    Serial.print("Virtual Device(s) Started: [ " ANSI_YELLOW_BOLD );
    for (uint8_t i = 0; i < 31; i++)
    {
        if (IEC.isDeviceEnabled(i))
        {
            Serial.printf("%.02d ", i);
        }
    }
    Serial.println( ANSI_RESET "]");


#ifdef DEBUG
    unsigned long endms = fnSystem.millis();
    Debug_printf("Available heap: %u\nSetup complete @ %lu (%lums)\n", fnSystem.get_free_heap_size(), endms, endms - startms);
#endif // DEBUG

    //runTestsSuite();
    //lfs_test();
#ifdef DEBUG_TIMING
    Debug_printv( ANSI_GREEN_BOLD "DEBUG_TIMING enabled" ANSI_RESET );
#endif
}



// Main high-priority service loop
void fn_service_loop(void *param)
{
    while (true)
    {
        // We don't have any delays in this loop, so IDLE threads will be starved
        // Shouldn't be a problem, but something to keep in mind...

#ifdef DEBUG_TIMING
        IEC.debugTiming();
#else
        IEC.service();
        // if ( IEC.bus_state < BUS_ACTIVE )
        //     taskYIELD();
#endif
    }
}

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
            printf("You wrote: %s\n", line);
            linenoiseFree(line); /* Or just free(line) if you use libc malloc. */
        }
    }

    esp_console_deinit();
}



/*
 * This is the start/entry point for an ESP-IDF program (must use "C" linkage)
 */
extern "C"
{
    void app_main()
    {
        // cppcheck-suppress "unusedFunction"
        // Call our setup routine
        main_setup();

// Create a new high-priority task to handle the main loop
// This is assigned to CPU1; the WiFi task ends up on CPU0
#define MAIN_STACKSIZE 32768
#define MAIN_PRIORITY 20
#define MAIN_CPUAFFINITY 1
        xTaskCreatePinnedToCore(fn_service_loop, "fnLoop",
                                MAIN_STACKSIZE, nullptr, MAIN_PRIORITY, nullptr, MAIN_CPUAFFINITY);

    
        // xTaskCreatePinnedToCore(fn_console_loop, "fnConsole", 
        //                         4096, nullptr, 1, nullptr, 0);

#ifdef LED_STRIP
        // Start LED Strip
        //led_strip_main();  // led_strip feedback lib

        //neopixel_main();  // neopixel lib

        display_app_main(); // fastled lib
#endif

#ifdef PIEZO_BUZZER
        mlSoundManager.setup(); // start sound
#endif

#ifdef PARALLEL_IO
        parallel_setup();
#endif

        // Sit here twiddling our thumbs
        while (true)
            vTaskDelay(9000 / portTICK_PERIOD_MS);
    }
}
