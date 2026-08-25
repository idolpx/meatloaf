/*
ESP32-BT2PS2
Software for Espressif ESP32 chipsets for PS/2 emulation and Bluetooth BLE/Classic HID keyboard/mouse interfacing.
Thanks to all the pioneers who worked on the code libraries that made this project possible, check them on the README!
Copyright Humberto Mockel - Hamcode - 2024
hamberthm@gmail.com
Dedicated to all who love me and all who I love.
Never stop dreaming.
*/

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "driver\gpio.h"
#include <iostream>
#include <cmath>
#include "bt_keyboard.h" // Interface with a BT/BLE peripheral device (Keyboard & Mouse)
#include "ps2dev.h"  // Emulate a PS/2 device
#include "serial_mouse.h"  // Emulate a serial mouse

/////////////////////////////// USER ADJUSTABLE VARIABLES //////////////////////////////////////////////////

// PS/2 emulation variables
const int KB_CLK_PIN = 22; // VERY IMPORTANT: Not all pins are suitable out-of-the-box. Check README for more info!
const int KB_DATA_PIN = 23;

const int MOUSE_CLK_PIN = 26; // VERY IMPORTANT: Not all pins are suitable out-of-the-box. Check README for more info!
const int MOUSE_DATA_PIN = 25;

static const int SERIAL_MOUSE_RS232_RTS = 15; // If you're not using serial, do not connect anything to this pin, otherwise PS/2 may not work if pulled down
static const int SERIAL_MOUSE_RS232_RX = 4;

const bool pairing_at_startup = true; // set to 'true' if you want BT & BLE pairing at startup (slower startup)
// otherwise, pairing must be requested by pressing BOOT button (or set GPIO 0 to ground) during execution

///////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr char const *TAG = "BT2PS2 Main Module";
static bool pairingRequested = false; // pairing requested flag
static bool pairingAborted = false;   // pairing aborted flag
static bool bleNowScanning = false;   // BLE scanning daemon in course flag
static bool codeHandlerActive = false;
void mouse_task(void *arg);
void ble_connection_daemon(void *arg);
TaskHandle_t pairing_task_handle;

ps2dev::PS2Mouse mouse(MOUSE_CLK_PIN, MOUSE_DATA_PIN);

ps2dev::PS2Keyboard keyboard(KB_CLK_PIN, KB_DATA_PIN);

serialMouse mouse_serial;

// BTKeyboard section
BTKeyboard bt_keyboard;

int NumDigits(int x) // Returns number of digits in keyboard code
{
    x = abs(x);
    return (x < 10 ? 1 : (x < 100 ? 2 : (x < 1000 ? 3 : (x < 10000 ? 4 : (x < 100000 ? 5 : (x < 1000000 ? 6 : (x < 10000000 ? 7 : (x < 100000000 ? 8 : (x < 1000000000 ? 9 : 10)))))))));
}

void pairing_handler(uint32_t pid) // This handler deals with BT Classic's manual code entry. DEFAULT CODE FOR LEGACY PAIRING IS 1234 (When no signaling occurs)
{
    codeHandlerActive = true; // LED control taken from main loop

    int x = (int)pid;
    std::cout << "Please enter the following pairing code, "
              << std::endl
              << "followed by ENTER on your keyboard: "
              << pid
              << std::endl;

    for (int i = 0; i < 10; i++) // Flash quickly many times to alert user of incoming code display
    {
        gpio_set_level(GPIO_NUM_2, 1);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_2, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    int dig = NumDigits(x); // How many digits does our code have?

    for (int i = 1; i <= dig; i++)
    {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP_LOGW(TAG, "PAIRING CODE (TYPE ON KEYBOARD AND PRESS ENTER): %d ", pid);
        int flash = ((int)((pid / pow(10, (dig - i)))) % 10); // This extracts one ditit at a time from our code
        ESP_LOGI(TAG, "Flashing %d times", flash);
        for (int n = 0; n < flash; n++) // Flash the LED as many times as the digit
        {
            gpio_set_level(GPIO_NUM_2, 1);
            vTaskDelay(200 / portTICK_PERIOD_MS);
            gpio_set_level(GPIO_NUM_2, 0);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }

        if (flash < 1) // If digit is 0, keep a steady light for 1.5sec
        {
            gpio_set_level(GPIO_NUM_2, 1);
            vTaskDelay(1500 / portTICK_PERIOD_MS);
            gpio_set_level(GPIO_NUM_2, 0);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    for (int i = 0; i < 10; i++) // Quick flashes indicate end of code display
    {

        gpio_set_level(GPIO_NUM_2, 1);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_2, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    codeHandlerActive = false; // LED control returned to main loop
}

static void IRAM_ATTR pairing_scan(void *arg = NULL)
{
    BTKeyboard::isConnected = false;
    pairingRequested = true;

    gpio_set_level(GPIO_NUM_2, 0); // Turn off LED for pairing

    while (bleNowScanning)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS); // shield for BLE daemon
    }

    ESP_LOGI(TAG, "/////////////////// PAIRING ACTIVATED ////////////////////////");
    ESP_LOGI(TAG, "Scanning...");

    while (!BTKeyboard::isConnected && !pairingAborted)
    {
        bt_keyboard.devices_scan();
        if (BTKeyboard::btFound)
        {
            BTKeyboard::btFound = false;
            for (int i = 0; i < 60; i++)
            {
                if (BTKeyboard::isConnected)
                    break;
                ESP_LOGI(TAG, "Waiting for BT Classic manual code entry...");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
        else
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "Pairing re-scan...");
        }
    } // Required to discover new keyboards and for pairing
      // Default duration is 5 seconds

    ESP_LOGI(TAG, "/////////////////// PAIRING ENDED ////////////////////////");

    pairingRequested = false;
    pairingAborted = false;
    gpio_set_level(GPIO_NUM_2, 1); // success, device found (or not and it was aborted, but we are positive in life)

    vTaskDelete(NULL);

    ESP_LOGE(TAG, "Pairing task could not be ended! You shouldn't be reading this, panic!");
}

static void IRAM_ATTR start_pairing_scan(void *arg = NULL)
{
    xTaskCreatePinnedToCore(pairing_scan, "pairing_task", 4096, NULL, 0, &pairing_task_handle, 0);
}

extern "C"
{

    void app_main(void)
    {
        gpio_config_t io_conf; // PIN CONFIGURARION SECTION: CRITICAL

        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT_OD;
        io_conf.pin_bit_mask = (1ULL << KB_DATA_PIN);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        io_conf.pin_bit_mask = (1ULL << KB_CLK_PIN);
        gpio_config(&io_conf);

        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT_OD;
        io_conf.pin_bit_mask = (1ULL << MOUSE_DATA_PIN);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        io_conf.pin_bit_mask = (1ULL << MOUSE_CLK_PIN);
        gpio_config(&io_conf);

        io_conf.intr_type = GPIO_INTR_DISABLE; // PAIRING BUTTON CONFIGURATION (GPIO 0 OR "BOOT BUTTON")
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << GPIO_NUM_0);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&io_conf);

        io_conf.intr_type = GPIO_INTR_NEGEDGE; // interrupt for serial mouse
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << SERIAL_MOUSE_RS232_RTS);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&io_conf);

        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << SERIAL_MOUSE_RS232_RX);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        // init PS/2 emulation first

        gpio_reset_pin(GPIO_NUM_2);                       // using built-in LED for notifications
        gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT); // Set the GPIO as a push/pull output
        gpio_set_level(GPIO_NUM_2, 1);

        mouse.begin(true); // true parameter indicates we want to recover previous mouse state from NVS
        keyboard.begin();
        mouse_serial.setup(SERIAL_MOUSE_RS232_RTS, SERIAL_MOUSE_RS232_RX);

        gpio_set_level(GPIO_NUM_2, 0);

        // init BTKeyboard
        esp_err_t ret;

        // To test the Pairing code entry, uncomment the following line as pairing info is
        // kept in the nvs. Pairing will then be required on every boot.
        // ESP_ERROR_CHECK(nvs_flash_erase());

        ret = nvs_flash_init();
        if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND))
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        if (bt_keyboard.setup(pairing_handler)) // Must be called once
        {
            if (pairing_at_startup)
            {
                start_pairing_scan();
            }
            else
            {
                gpio_set_level(GPIO_NUM_2, 1); // let the fun begin!
            }
        }
        else
        {
            ESP_LOGE(TAG, "bt_keyboard.setup() returned false. System initialization ABORTED! Post above log error messages in forum to get help. Restarting...");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            esp_restart(); // fun aborted
        }

        // time variables, don't adjust unless you know what you're doing
        uint8_t typematicRate = 20;    // characters per second in Typematic mode
        uint16_t typematicDelay = 500; // ms to become Typematic

        // fixed stuff
        uint8_t cycle = 1000 / typematicRate;            // keywait timeout in ms. Important so we can check connection and do Typematic
        TickType_t repeat_period = pdMS_TO_TICKS(cycle); // keywait timeout in ticks. Important so we can check connection and do Typematic
        BTKeyboard::KeyInfo info;                        // freshly received
        BTKeyboard::KeyInfo infoBuf;                     // currently pressed
        BTKeyboard::KeyInfo_CCONTROL infoCCONTROL;       // freshly received multimedia (CCONTROL) keys
        BTKeyboard::KeyInfo_CCONTROL infoBufCCONTROL;    // currently pressed multimedia (CCONTROL) keys
        TaskHandle_t mouse_task_handle;                  // mouse uses it's own task. Mouse is important
        TaskHandle_t ble_connection_daemon_handle;       // the BLE daemon constantly scans for BLE previously bonded devices

        bool found = false;                 // just an innocent flasg I mean flag
        int typematicLeft = typematicDelay; // timekeeping

        info.modifier = infoBuf.modifier = (BTKeyboard::KeyModifier)0;

        for (int j = 0; j < BTKeyboard::MAX_KEY_COUNT; j++)
        {
            infoBuf.keys[j] = 0;
            info.keys[j] = 0;
            infoBufCCONTROL.keys[j] = 0;
            infoCCONTROL.keys[j] = 0;
        }

        xTaskCreatePinnedToCore(mouse_task, "mouse_task", 4096, NULL, 9, &mouse_task_handle, 0); // this lives in core 0 so not to run alongside serial mouse daemon (and getting paused by it)
        xTaskCreatePinnedToCore(ble_connection_daemon, "ble_task", 4096, NULL, 0, &ble_connection_daemon_handle, 0);

        while (true)
        {
            if (bt_keyboard.wait_for_low_event(info, repeat_period))
            {
                // Handle modifier keys
                if (info.modifier != infoBuf.modifier)
                {

                    // MODIFIER SECTION

                    // Are you a communist?
                    if (((uint8_t)info.modifier & 0x0f) != ((uint8_t)infoBuf.modifier & 0x0f))
                    {

                        // LSHIFT
                        if (((uint8_t)info.modifier & 0x02) != ((uint8_t)infoBuf.modifier & 0x02))
                        {
                            if ((uint8_t)info.modifier & 0x02)
                            {
                                ESP_LOGD(TAG, "Down key: LSHIFT");
                                keyboard.keydown(ps2dev::scancodes::Key::K_LSHIFT);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: LSHIFT");
                                keyboard.keyup(ps2dev::scancodes::Key::K_LSHIFT);
                            }
                        }

                        // LCTRL
                        if (((uint8_t)info.modifier & 0x01) != ((uint8_t)infoBuf.modifier & 0x01))
                        {
                            if ((uint8_t)info.modifier & 0x01)
                            {
                                ESP_LOGD(TAG, "Down key: LCTRL");
                                keyboard.keydown(ps2dev::scancodes::Key::K_LCTRL);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: LCTRL");
                                keyboard.keyup(ps2dev::scancodes::Key::K_LCTRL);
                            }
                        }

                        // LMETA
                        if (((uint8_t)info.modifier & 0x08) != ((uint8_t)infoBuf.modifier & 0x08))
                        {
                            if ((uint8_t)info.modifier & 0x08)
                            {
                                ESP_LOGD(TAG, "Down key: LMETA");
                                keyboard.keydown(ps2dev::scancodes::Key::K_LSUPER);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: LMETA");
                                keyboard.keyup(ps2dev::scancodes::Key::K_LSUPER);
                            }
                        }

                        // LALT
                        if (((uint8_t)info.modifier & 0x04) != ((uint8_t)infoBuf.modifier & 0x04))
                        {
                            if ((uint8_t)info.modifier & 0x04)
                            {
                                ESP_LOGD(TAG, "Down key: LALT");
                                keyboard.keydown(ps2dev::scancodes::Key::K_LALT);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: LALT");
                                keyboard.keyup(ps2dev::scancodes::Key::K_LALT);
                            }
                        }
                    }

                    // Are you a capitalist?
                    if (((uint8_t)info.modifier & 0xf0) != ((uint8_t)infoBuf.modifier & 0xf0))
                    {

                        // RSHIFT
                        if (((uint8_t)info.modifier & 0x20) != ((uint8_t)infoBuf.modifier & 0x20))
                        {
                            if ((uint8_t)info.modifier & 0x20)
                            {
                                ESP_LOGD(TAG, "Down key: RSHIFT");
                                keyboard.keydown(ps2dev::scancodes::Key::K_RSHIFT);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: RSHIFT");
                                keyboard.keyup(ps2dev::scancodes::Key::K_RSHIFT);
                            }
                        }

                        // RCTRL
                        if (((uint8_t)info.modifier & 0x10) != ((uint8_t)infoBuf.modifier & 0x10))
                        {
                            if ((uint8_t)info.modifier & 0x10)
                            {
                                ESP_LOGD(TAG, "Down key: RCTRL");
                                keyboard.keydown(ps2dev::scancodes::Key::K_RCTRL);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: RCTRL");
                                keyboard.keyup(ps2dev::scancodes::Key::K_RCTRL);
                            }
                        }

                        // RMETA
                        if (((uint8_t)info.modifier & 0x80) != ((uint8_t)infoBuf.modifier & 0x80))
                        {
                            if ((uint8_t)info.modifier & 0x80)
                            {
                                ESP_LOGD(TAG, "Down key: RMETA");
                                keyboard.keydown(ps2dev::scancodes::Key::K_RSUPER);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: RMETA");
                                keyboard.keyup(ps2dev::scancodes::Key::K_RSUPER);
                            }
                        }

                        // RALT
                        if (((uint8_t)info.modifier & 0x40) != ((uint8_t)infoBuf.modifier & 0x40))
                        {
                            if ((uint8_t)info.modifier & 0x40)
                            {
                                ESP_LOGD(TAG, "Down key: RALT");
                                keyboard.keydown(ps2dev::scancodes::Key::K_RALT);
                            }
                            else
                            {
                                ESP_LOGD(TAG, "Up key: RALT");
                                keyboard.keyup(ps2dev::scancodes::Key::K_RALT);
                            }
                        }
                    }
                }

                // KEY SECTION (always tested)
                // release the keys that have been just released
                for (int i = 0; i < BTKeyboard::MAX_KEY_COUNT; i++)
                {
                    if (!infoBuf.keys[i])
                    { // Detect END FLAG
                        break;
                    }
                    for (int j = 0; (info.keys[j]) && (j < BTKeyboard::MAX_KEY_COUNT); j++)
                    {
                        if (infoBuf.keys[i] == info.keys[j])
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        ESP_LOGD(TAG, "Up key: %x", infoBuf.keys[i]);
                        keyboard.keyHid_send(infoBuf.keys[i], false);
                        gpio_set_level(GPIO_NUM_2, 1);
                    }
                    else
                    {
                        found = false;
                    }
                }

                // press the keys that have been just pressed
                for (int i = 0; i < BTKeyboard::MAX_KEY_COUNT; i++)
                {
                    if (!info.keys[i])
                    { // Detect END FLAG
                        break;
                    }
                    for (int j = 0; (infoBuf.keys[j]) && (j < BTKeyboard::MAX_KEY_COUNT); j++)
                    {
                        if (info.keys[i] == infoBuf.keys[j])
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        ESP_LOGD(TAG, "Down key: %x", info.keys[i]);
                        keyboard.keyHid_send(info.keys[i], true);
                        gpio_set_level(GPIO_NUM_2, 0);
                    }
                    else
                    {
                        found = false;
                    }
                }

                infoBuf = info;                 // Now all the keys are handled, we save the state
                typematicLeft = typematicDelay; // Typematic timer reset
            }

            else
            {
                if (infoBuf.keys[0])
                { // If any key held down, do the Typematic dance
                    typematicLeft = typematicLeft - cycle;
                    if (typematicLeft <= 0)
                    {
                        for (int i = 1; i < BTKeyboard::MAX_KEY_COUNT; i++)
                        {
                            if (infoBuf.keys[i] == 0)
                            {
                                if (infoBuf.keys[i - 1] != 0x39)
                                { // Please don't repeat caps, it's ugly
                                    ESP_LOGD(TAG, "Down key: %x", infoBuf.keys[i - 1]);
                                    keyboard.keyHid_send(infoBuf.keys[i - 1], true); // Resend the last key
                                }
                                break;
                            }
                        }
                    }
                }

                if (!pairingRequested)
                {
                    gpio_set_level(GPIO_NUM_2, 1); // LED up for every key cycle

                    if (!gpio_get_level(GPIO_NUM_0)) // Pairing request via BOOT button (GPIO_0) check
                    {
                        pairingRequested = true;
                        start_pairing_scan();
                    }
                }
                else
                {
                    if (!pairingAborted && !codeHandlerActive) // code handler needs LED control, no touchy while is active!
                    {
                        gpio_set_level(GPIO_NUM_2, 0); // LED down while pairing is active
                    }
                    else if (!codeHandlerActive)
                    {
                        gpio_set_level(GPIO_NUM_2, 1); // LED up while pairin is active but abort is requested
                    }
                    if (!gpio_get_level(GPIO_NUM_0)) // Pairing request via BOOT button (GPIO_0) check
                    {
                        vTaskDelay(2000 / portTICK_PERIOD_MS);
                        if (!gpio_get_level(GPIO_NUM_0))
                            pairingAborted = true; // User pressed and hold the pairing button for 2sec while it was active. This is an abort request!
                    }
                }
            }

            ////////////////////// MULTIMEDIA KEYS (CCONTROL HID USAGE CODES) SECTION

            if (bt_keyboard.wait_for_low_event_CCONTROL(infoCCONTROL, 0)) // return immediately if queue empty
            {
                // KEY SECTION (always tested)
                // release the keys that have been just released
                for (int i = 0; i < BTKeyboard::MAX_KEY_COUNT; i++)
                {
                    if (!infoBufCCONTROL.keys[i]) // Detect END FLAG
                        break;
                    for (int j = 0; (infoCCONTROL.keys[j]) && (j < BTKeyboard::MAX_KEY_COUNT); j++)
                    {
                        if (infoBufCCONTROL.keys[i] == infoCCONTROL.keys[j])
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        ESP_LOGD(TAG, "Up CCONTROL key: %x", infoBufCCONTROL.keys[i]);
                        keyboard.keyHid_send_CCONTROL(infoBufCCONTROL.keys[i], false);
                        gpio_set_level(GPIO_NUM_2, 1);
                    }
                    else
                        found = false;
                }

                // press the keys that have been just pressed
                for (int i = 0; i < BTKeyboard::MAX_KEY_COUNT; i++)
                {
                    if (!infoCCONTROL.keys[i]) // Detect END FLAG
                        break;
                    for (int j = 0; (infoBufCCONTROL.keys[j]) && (j < BTKeyboard::MAX_KEY_COUNT); j++)
                    {
                        if (infoCCONTROL.keys[i] == infoBufCCONTROL.keys[j])
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        ESP_LOGD(TAG, "Down CCONTROL key: %x", infoCCONTROL.keys[i]);
                        keyboard.keyHid_send_CCONTROL(infoCCONTROL.keys[i], true);
                        gpio_set_level(GPIO_NUM_2, 0);
                    }
                    else
                        found = false;
                }

                infoBufCCONTROL = infoCCONTROL; // Now all the keys are handled, we save the state
            }
        }
    }
}

void mouse_task(void *arg)
{
    BTKeyboard::Mouse_Control infoMouse;    // freshly received mouse report
    BTKeyboard::Mouse_Control infoMouseBuf; // currently pressed mouse report

    while (true)
    {
        if (bt_keyboard.wait_for_low_event_MOUSE(infoMouse)) // return immediately if queue empty
        {
            if (!gpio_get_level((gpio_num_t)SERIAL_MOUSE_RS232_RTS)) // serial connection auto detection, RTS should be high during serial presence 
            {
                mouse_serial.serialMove(infoMouse.mouse_buttons, infoMouse.mouse_x, -infoMouse.mouse_y);
            }
            else
            {
                mouse.move(infoMouse.mouse_x, infoMouse.mouse_y, infoMouse.mouse_w);

                // KEY SECTION (always tested)
                if ((infoMouse.mouse_buttons) != (infoMouseBuf.mouse_buttons))
                {
                    if ((infoMouse.mouse_buttons & 0b1) != (infoMouseBuf.mouse_buttons & 0b1)) // change on first button?
                    {
                        if (infoMouse.mouse_buttons & 0b1)
                        {
                            ESP_LOGD(TAG, "Down Mouse button 1");
                            mouse.press(ps2dev::PS2Mouse::Button::LEFT);
                            gpio_set_level(GPIO_NUM_2, 0);
                        }
                        else
                        {
                            ESP_LOGD(TAG, "Up Mouse button 1");
                            mouse.release(ps2dev::PS2Mouse::Button::LEFT);
                            gpio_set_level(GPIO_NUM_2, 1);
                        }
                    }

                    if ((infoMouse.mouse_buttons & 0b10) != (infoMouseBuf.mouse_buttons & 0b10)) // change on second button?
                    {
                        if (infoMouse.mouse_buttons & 0b10)
                        {
                            ESP_LOGD(TAG, "Down Mouse button 2");
                            mouse.press(ps2dev::PS2Mouse::Button::RIGHT);
                            gpio_set_level(GPIO_NUM_2, 0);
                        }
                        else
                        {
                            ESP_LOGD(TAG, "Up Mouse button 2");
                            mouse.release(ps2dev::PS2Mouse::Button::RIGHT);
                            gpio_set_level(GPIO_NUM_2, 1);
                        }
                    }

                    if ((infoMouse.mouse_buttons & 0b100) != (infoMouseBuf.mouse_buttons & 0b100)) // change on third button?
                    {
                        if (infoMouse.mouse_buttons & 0b100)
                        {
                            ESP_LOGD(TAG, "Down Mouse button 3");
                            mouse.press(ps2dev::PS2Mouse::Button::MIDDLE);
                            gpio_set_level(GPIO_NUM_2, 0);
                        }
                        else
                        {
                            ESP_LOGD(TAG, "Up Mouse button 3");
                            mouse.release(ps2dev::PS2Mouse::Button::MIDDLE);
                            gpio_set_level(GPIO_NUM_2, 1);
                        }
                    }
                }
            }

            infoMouseBuf = infoMouse; // Now all the keys are handled, we save the state
        }
    }
}

void ble_connection_daemon(void *arg)
{
    while (true)
    {
        if (!pairingRequested)
        {
            bleNowScanning = true;
            bt_keyboard.devices_scan_ble_daemon();
            ESP_LOGD(TAG, "RAM left %d", esp_get_free_heap_size());
        }
        else
        {
            ESP_LOGI(TAG, "Pairing requested! Stopping BLE connection daemon...");
            bleNowScanning = false;
            while (pairingRequested)
                vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
    }
}