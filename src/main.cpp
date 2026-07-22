// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#include <esp_system.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>

#ifdef CONFIG_SPIRAM
#include <esp_psram.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <esp32/himem.h>
#endif
#endif

#include <driver/gpio.h>

// Disable when not debugging memory leaks
// #include "esp_heap_trace.h"
// #define NUM_RECORDS 100
// static DRAM_ATTR heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
// ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
// DO_SOME_FUNCTION() HERE
// ESP_ERROR_CHECK( heap_trace_stop() );
// heap_trace_dump();
//
// main()
// ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );

#include "../include/global_defines.h"
#include "../include/debug.h"
#include "../include/version.h"
#include "../include/pinmap.h"


#ifdef ENABLE_CONSOLE
#include "../lib/console/ESP32Console.h"
#endif

#ifdef ENABLE_DISPLAY
#include "display.h"
#endif

#ifdef ENABLE_BLUETOOTH
#include <c64b.h>
#endif

#ifdef ENABLE_PS2
#include "ps2_keyboard.h"
ps2dev::PS2Keyboard keyboard(PIN_KB_CLK, PIN_KB_DATA);
#endif

#include "device.h"
#include "keys.h"
#include "led.h"


#include "fnSystem.h"
#include "fnWiFi.h"
#include "fnConfig.h"
#include "mlConfig.h"

#include "fsFlash.h"
#include "fnFsSD.h"



//#include "disk-sounds.h"

/**************************/
// Meatloaf


#include "bus.h"
#include "meat_session.h"
//#include "ml_tests.h"

std::string statusMessage;
bool initFailed = false;


/**************************/

// Labeled internal-heap checkpoint for locating where boot-time RAM goes.
// internal_largest matters as much as internal_free: task stacks (and other
// contiguous-only allocations) need one free block big enough, not just a
// high aggregate total (see the httpd/console_exec stack-creation failures
// this was added to track down).
// static void log_heap_checkpoint(const char *label)
// {
//     Debug_printv("[MEM] %-28s heap=%lu internal_free=%u internal_largest=%u",
//                  label,
//                  (unsigned long)esp_get_free_heap_size(),
//                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
//                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
// }


#if defined(BUILD_IEC) || defined(BUILD_GPIB)
// Retries reloadConfig() for drives whose persisted network-scheme URL
// (fsp://, http://, ...) was deferred at boot because WiFi wasn't connected
// yet. Constructing a network MFile chain needs deep stack (PeoplesUrlParser
// + nlohmann::json), so this must run somewhere with a big contiguous stack.
static void reload_network_drives()
{
    Meatloaf.reloadAllConfig();
}

// One-shot task: polls fnWiFi.connected() and, once up, runs
// reload_network_drives(). The polling loop itself is stack-cheap; the
// deep-stack work is handed off to the console's existing 16 KB executor
// task (ENABLE_CONSOLE builds) instead of paying for a second dedicated
// big-stack task. Builds without a console fall back to running it inline
// on this task's own (in that case, 16 KB) stack.
static void reload_network_drives_task(void *)
{
    const int max_wait_ms = 30000;
    const int poll_ms = 500;
    int waited = 0;
    while (!fnWiFi.connected() && waited < max_wait_ms)
    {
        vTaskDelay(poll_ms / portTICK_PERIOD_MS);
        waited += poll_ms;
    }

    if (fnWiFi.connected())
    {
#ifdef ENABLE_CONSOLE
        console.execAcquire();
        console.runOnExecutor(reload_network_drives);
        console.execRelease();
#else
        reload_network_drives();
#endif
    }

    vTaskDelete(NULL);
}
#endif

void main_shutdown_handler()
{
    Debug_println("Shutdown handler called.");
    // Give devices an opportunity to clean up before rebooting

    SessionBroker::shutdown();
    SYSTEM_BUS.shutdown();
}

// Initial setup
void main_setup()
{
    // ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );

    unsigned long startms = fnSystem.millis();

#ifdef DELAY_START_MILLIS
    vTaskDelay(DELAY_START_MILLIS / portTICK_PERIOD_MS);
#endif

#ifdef ENABLE_CONSOLE
    //You can change the console prompt before calling begin(). By default it is "ESP32>"
    console.setPrompt("meatloaf[%pwd%]# ");

    //You can change the baud rate and pin numbers similar to Serial.begin() here.
    console.begin(DEBUG_SPEED);

    // Register all extra command groups right after the console is up.
    // Doing this here guarantees the user cannot type into a half-initialised
    // REPL (e.g. before WiFi/SD/IEC init finishes) and ensures the full
    // command set is available from the first prompt.
    console.registerSystemCommands();
    console.registerDisplayCommands();
    console.registerIECCommands();
    console.registerNetworkCommands();
    console.registerVFSCommands();
    console.registerGPIOCommands();
    console.registerXFERCommands();
#else
    Serial.begin(DEBUG_SPEED);
#endif

    printf( ANSI_WHITE "\r\n\r\n" ANSI_BLUE_BACKGROUND "==============================" ANSI_RESET_NL );
    printf( ANSI_BLUE_BACKGROUND "   " PRODUCT_ID " " FW_VERSION "   " ANSI_RESET_NL );
    printf( ANSI_BLUE_BACKGROUND "   " PLATFORM_DETAILS "    " ANSI_RESET_NL );
    printf( ANSI_BLUE_BACKGROUND "------------------------------" ANSI_RESET_NL "\r\n" );

    //printf( "Meatloaf %s Started @ %lu\r\n", fnSystem.get_fujinet_version(), startms );

    printf( "RAM  : %lu\r\n", fnSystem.get_free_heap_size() );

#ifdef CONFIG_SPIRAM
    //printf( "PSRAM: %lu\r\n", fnSystem.get_psram_size() );
#ifdef CONFIG_IDF_TARGET_ESP32
    printf( "HIMEM: %u\r\n", esp_himem_get_phys_size() );
    //printf( "HIMEM free %u\r\n", esp_himem_get_free_size() );
    //printf( "HIMEM reserved %u\r\n", esp_himem_reserved_area_size() );
#endif
#endif
    //log_heap_checkpoint("boot start");

    // Install a reboot handler
    esp_register_shutdown_handler(main_shutdown_handler);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        printf("Erasing flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    // Install GPIO ISR service before any GPIO operations
    gpio_install_isr_service(0);

    fnSystem.check_hardware_ver();
    printf("Detected Hardware Version: %s\r\n", fnSystem.get_hardware_ver_str());

    // Setup hardware
    fnKeyManager.setup();
    fnLedManager.setup();
    //log_heap_checkpoint("after nvs/gpio/key/led setup");

#ifndef MIN_CONFIG
    if (PIN_MODEM_ENABLE != GPIO_NUM_NC && PIN_MODEM_UP9600 != GPIO_NUM_NC) {
        // Enable/Disable Modem/Parallel Mode on Userport
        fnSystem.set_pin_mode(PIN_MODEM_ENABLE, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_MODEM_ENABLE, DIGI_LOW); // DISABLE Modem
        //fnSystem.digital_write(PIN_MODEM_ENABLE, DIGI_HIGH); // ENABLE Modem
        fnSystem.set_pin_mode(PIN_MODEM_UP9600, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_MODEM_UP9600, DIGI_LOW); // DISABLE UP9600
        //fnSystem.digital_write(PIN_MODEM_UP9600, DIGI_HIGH); // ENABLE UP9600
    }
#endif

    // Initialize the FileSystem
    printf("Initializing FileSystem\r\n");
    if ( fsFlash.start() )
    {
        // Create directories if they doesn't exist
        fsFlash.create_path( PRINT_DIR );

        // Create SYSTEM DIR if it doesn't exist
        fsFlash.create_path( SYSTEM_DIR );
        fsFlash.create_path( SYSTEM_DIR "/ssh" );
    }
    //log_heap_checkpoint("after flash FS init");

#ifdef SD_CARD
    if ( fnSDFAT.start() )
    {
        // Create directories if they doesn't exist
        fnSDFAT.create_path( BIN_DIR );
        fnSDFAT.create_path( CACHE_DIR );
        fnSDFAT.create_path( PRINT_DIR );
        fnSDFAT.create_path( ROM_DIR );

        fnSDFAT.create_path( SYSTEM_DIR );
        fnSDFAT.create_path( SYSTEM_DIR "/ssh" );
    }
    //log_heap_checkpoint("after SD FS init");
#endif

    // setup crypto key - must be done before loading the config
    crypto.setkey("MLK" + fnWiFi.get_mac_str());

    // Load our stored configuration
    Config.load();
    mlConfig.load();
    //log_heap_checkpoint("after config load");

    // Setup IEC Bus
    SYSTEM_BUS.setup();
#ifdef BUILD_IEC
    printf(ANSI_GREEN_BOLD "IEC Bus Initialized" ANSI_RESET "\r\n");
#endif
#ifdef BUILD_GPIB
    printf(ANSI_GREEN_BOLD "GPIB Bus Initialized" ANSI_RESET "\r\n");
#endif
    //log_heap_checkpoint("after IEC/GPIB bus setup");

    Meatloaf.setup(&SYSTEM_BUS);
    //log_heap_checkpoint("after Meatloaf/drive devices setup");
    // {
    //     // Add devices to bus
    //     FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fsFlash;
    //     iecPrinter::printer_type ptype = iecPrinter::printer_type::PRINTER_COMMODORE_MPS803; // temporary
    //     printf("Creating a default printer using %s storage and type %d\r\n", ptrfs->typestring(), ptype);
    //     iecPrinter *ptr = new iecPrinter(ptrfs, ptype);
    //     fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

    //     printf("Printer "); IEC.addDevice(ptr, 4);                    // 04-07 Printers / Plotters
    //     printf("Disk "); IEC.addDevice(new iecDrive(), 8);            // 08-15 Drives
    //     printf("Network "); IEC.addDevice(new iecNetwork(), 16);      // 16-19 Network Devices
    //     printf("CPM "); IEC.addDevice(new iecCpm(), 20);              // 20-29 Other
    //     printf("Voice "); IEC.addDevice(new iecVoice(), 21);
    //     printf("Clock "); IEC.addDevice(new iecClock(), 28);
    //     printf("OpenAI "); IEC.addDevice(new iecOpenAI(), 29);
    //     printf("Meatloaf "); IEC.addDevice(new iecMeatloaf(), 30);    // 30    Meatloaf

    //     printf("Virtual Device(s) Started: [ " ANSI_YELLOW_BOLD );
    //     for (uint8_t i = 0; i < 31; i++)
    //     {
    //         if (IEC.isDeviceEnabled(i))
    //         {
    //             printf("%.02d ", i);
    //         }
    //     }
    //     printf( ANSI_RESET "]\r\n");
    //     //IEC.enabled = true;
    // }

#ifdef PARALLEL_BUS_X
    // Setup Parallel Bus
    PARALLEL.setup();
    printf( ANSI_GREEN_BOLD "Parallel Bus Initialized" ANSI_RESET "\r\n" );
#endif

#ifdef ENABLE_DISPLAY
    LEDS.start();
    LCD.show_image( (char *)WWW_ROOT "/assets/logo.160x80.jpg" );
    //log_heap_checkpoint("after display/LEDs start");
#endif

#ifdef ENABLE_AUDIO
    AUDIO.start(); // start sound
    //log_heap_checkpoint("after audio start");
#endif

#ifdef DEBUG
    unsigned long endms = fnSystem.millis();
    Debug_printf("Available heap: %lu\r\nSetup complete @ %lu (%lums)\r\n", fnSystem.get_free_heap_size(), endms, endms - startms);
#endif // DEBUG


    // Set up the WiFi adapter
    fnWiFi.start();
    //log_heap_checkpoint("after fnWiFi.start()");

    // Start SessionBroker service task on CPU0
    SessionBroker::setup();
    //log_heap_checkpoint("after SessionBroker::setup()");

    // Restore each drive's persisted mlConfig state (enabled flag, mounted URL).
    // Must happen after fnWiFi.start(): reloadConfig() may mount a network URL
    // (fsp://, http://, ...), and that needs the LWIP tcpip task that
    // fnWiFi.start() creates via esp_netif_init(). Calling this any earlier
    // (e.g. from iecDrive::begin() during SYSTEM_BUS.setup()) crashes with
    // "assert failed: tcpip_send_msg_wait_sem ... Invalid mbox".
#if defined(BUILD_IEC) || defined(BUILD_GPIB)
    bool network_drive_deferred = Meatloaf.reloadAllConfig();

    // At least one drive has a persisted network-scheme URL and WiFi isn't
    // connected yet: retry once it is, instead of leaving it unmounted forever.
    // The task itself only polls fnWiFi.connected() — the deep-stack MFile
    // work runs on console's executor task (see reload_network_drives_task),
    // so this stack only needs to be big enough without ENABLE_CONSOLE, where
    // reload_network_drives() runs inline on it.
#ifdef ENABLE_CONSOLE
    const uint32_t net_drive_retry_stack = 3072;
#else
    const uint32_t net_drive_retry_stack = 16384;
#endif
    if (network_drive_deferred)
        xTaskCreatePinnedToCore(reload_network_drives_task, "net_drive_retry", net_drive_retry_stack, nullptr, 3, nullptr, 0);
    //log_heap_checkpoint("after drive config reload");
#endif

#ifdef DEBUG_TIMING
    Debug_printv( ANSI_GREEN_BOLD "DEBUG_TIMING enabled" ANSI_RESET );
#endif

//#ifdef RUN_TESTS
//    runTestsSuite();
    // lfs_test();
//#endif

#ifdef ENABLE_CONSOLE
    // Create the persistent serial-console task now (small stack, claimed
    // while memory is plentiful). It stays dormant until the first byte of
    // console input, so idle sessions cost only the task's stack — and
    // activation can never fail on allocation.
    console.startOnDemand();
    //log_heap_checkpoint("after console.startOnDemand()");
    printf("Press ENTER to activate console.\r\n");
#endif

    printf("READY.\r\n");
    //log_heap_checkpoint("setup complete");
}

/*
 * This is the start/entry point for an ESP-IDF program (must use "C" linkage)
 */
extern "C"
{
    void app_main()
    {
        // Call our setup routine
        main_setup();

#ifdef ENABLE_BLUETOOTH
        // Setup Bluetooth
        bt_setup();
#endif

        // Delete app_main() task since we no longer need it
        vTaskDelete(NULL);
    }
}
