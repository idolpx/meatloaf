#ifdef ENABLE_ETHERNET
#include "ethernet.h"
#include "web_server.h"

#include <cstdlib>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_eth_driver.h"
#include "esp_mac.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "../../include/pinmap.h"

static const char *TAG = "eth_events";

namespace {
esp_eth_mac_t *w5500_mac = nullptr;
esp_eth_phy_t *w5500_phy = nullptr;
esp_event_handler_instance_t eth_handler = nullptr;
esp_event_handler_instance_t ip_handler = nullptr;
esp_event_handler_instance_t lost_ip_handler = nullptr;
bool owns_spi_bus = false;
bool start_attempted = false;
bool mac_lowlevel_active = false;
bool teardown_incomplete = false;
}

W5500Ethernet ethernet;

// Out-of-class definitions for the static members declared in ethernet.h.
esp_eth_handle_t            W5500Ethernet::eth_handle = nullptr;
esp_netif_t                *W5500Ethernet::eth_netif  = nullptr;
esp_eth_netif_glue_handle_t W5500Ethernet::eth_glue   = nullptr;
bool                        W5500Ethernet::_connected = false;

void W5500Ethernet::start()
{
    // =========================================================================
    // Ethernet Initialisation (W5500)
    // =========================================================================
    
    // Both of these are normally done by fnWiFi.start(), which runs first --
    // but depending on call order for correctness is how this crashed once
    // already, so ask for them explicitly.  Both are safe to repeat:
    // esp_netif_init() returns ESP_OK if already initialised, and
    // esp_event_loop_create_default() returns ESP_ERR_INVALID_STATE.
    if (eth_netif || teardown_incomplete)
        return;

    ESP_RETURN_VOID_ON_ERROR(esp_netif_init(), TAG, "network interface init failed");
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "default event loop creation failed: %s", esp_err_to_name(loop_err));
        return;
    }

    // Create Ethernet netif
    esp_netif_config_t eth_netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&eth_netif_cfg);
    if (eth_netif == NULL)
    {
        ESP_LOGE(TAG, "esp_netif_new failed; ethernet not started");
        return;
    }
    
    esp_err_t ret;
    {
        // Configure SPI bus for W5500
        spi_bus_config_t spi_bus_cfg = {
            .mosi_io_num = PIN_ETHERNET_MOSI,
            .miso_io_num = PIN_ETHERNET_MISO,
            .sclk_io_num = PIN_ETHERNET_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
        };

        ESP_GOTO_ON_ERROR(spi_bus_initialize(ETHERNET_SPI_HOST, &spi_bus_cfg,
                                           SPI_DMA_CH_AUTO), fail, TAG, "SPI bus init failed");
        owns_spi_bus = true;

        // Configure W5500 SPI device
        // Configure W5500 SPI device (CRITICAL: W5500 requires specific bit configuration)
        spi_device_interface_config_t spi_devcfg = {
            .command_bits = 16,        // W5500 requires 16-bit command
            .address_bits = 8,         // W5500 requires 8-bit address
            .mode = 0,
            .clock_speed_hz = ETHERNET_SPI_CLOCK_MHZ * 1000 * 1000,
            .spics_io_num = PIN_ETHERNET_CS,
            .queue_size = 20,
        };

        // W5500 MAC configuration
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(
            ETHERNET_SPI_HOST, &spi_devcfg);
        w5500_config.int_gpio_num = PIN_ETHERNET_INT;
        w5500_config.poll_period_ms = 0;  // Use interrupt mode

        // MAC config (REQUIRED - cannot be NULL!)
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();

        // Create MAC
        w5500_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
        ESP_GOTO_ON_FALSE(w5500_mac, ESP_FAIL, fail, TAG, "W5500 MAC creation failed");

        // Create PHY
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
        phy_config.phy_addr = -1;
        phy_config.reset_gpio_num = PIN_ETHERNET_RESET;
        w5500_phy = esp_eth_phy_new_w5500(&phy_config);
        ESP_GOTO_ON_FALSE(w5500_phy, ESP_FAIL, fail, TAG, "W5500 PHY creation failed");

        // Configure Ethernet driver
        esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(w5500_mac, w5500_phy);
        eth_config.on_lowlevel_init_done = [](esp_eth_handle_t) {
            mac_lowlevel_active = true;
            return ESP_OK;
        };
        eth_config.on_lowlevel_deinit_done = [](esp_eth_handle_t) {
            mac_lowlevel_active = false;
            return ESP_OK;
        };
        ret = esp_eth_driver_install(&eth_config, &eth_handle);
        if (ret != ESP_OK)
        {
            // IDF 5.5.3 frees the mediator on an install error. The W5500 MAC
            // removes its GPIO ISR on its own init error, but a subsequent PHY
            // error can leave the ISR and RX task live with a dangling mediator.
            // Neither deinit nor del is safe in that state. Keep IDF's fatal
            // behavior until driver_install can roll back that partial init.
            if (mac_lowlevel_active)
            {
                ESP_LOGE(TAG, "W5500 install failed after MAC init (%s); IDF cannot safely roll back",
                         esp_err_to_name(ret));
                std::abort();
            }
            ESP_GOTO_ON_ERROR(ret, fail, TAG, "W5500 driver install failed");
        }

        // The W5500 has NO burned-in MAC address -- unlike the ESP32's internal
        // EMAC, nothing supplies one, so without this the interface has no valid
        // source address and DHCP never completes (link comes up, no IP).
        // esp_read_mac(ESP_MAC_ETH) derives a unique address from this chip's
        // factory eFuse, so every board gets its own rather than a shared literal.
        uint8_t eth_mac_addr[6];
        ESP_GOTO_ON_ERROR(esp_read_mac(eth_mac_addr, ESP_MAC_ETH), fail, TAG, "MAC address read failed");
        ESP_GOTO_ON_ERROR(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac_addr),
                          fail, TAG, "MAC address setup failed");
        ESP_LOGI(TAG, "MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 eth_mac_addr[0], eth_mac_addr[1], eth_mac_addr[2],
                 eth_mac_addr[3], eth_mac_addr[4], eth_mac_addr[5]);

        // Attach Ethernet driver to netif.  The glue is kept so stop() can delete
        // the SAME one -- esp_eth_new_netif_glue() allocates, so calling it again
        // in stop() would leak this one and delete a stranger.
        eth_glue = esp_eth_new_netif_glue(eth_handle);
        ESP_GOTO_ON_FALSE(eth_glue, ESP_FAIL, fail, TAG, "Ethernet glue creation failed");
        ESP_GOTO_ON_ERROR(esp_netif_attach(eth_netif, eth_glue), fail, TAG, "Ethernet netif attach failed");

        // Register Ethernet event handlers
        ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                          &eth_event_handler, nullptr, &eth_handler), fail, TAG, "Ethernet handler registration failed");
        ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                          &ip_event_handler, nullptr, &ip_handler), fail, TAG, "IP handler registration failed");
        ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP,
                          &ip_event_handler, nullptr, &lost_ip_handler), fail, TAG, "Lost-IP handler registration failed");

        // Without this the driver is installed and attached but never brought up,
        // so no link is ever established.  stop() has the matching esp_eth_stop().
        // IDF 5.5.3 changes the driver state before all start steps have passed.
        start_attempted = true;
        ESP_GOTO_ON_ERROR(esp_eth_start(eth_handle), fail, TAG, "W5500 start failed");
    }

    ESP_LOGI(TAG, "Ethernet (W5500) initialised");
    return;

fail:
    stop();
    ESP_LOGW(TAG, "W5500 start failed (%s); continuing boot", esp_err_to_name(ret));
}


void W5500Ethernet::stop()
{
    if (!eth_netif || teardown_incomplete)
        return;

    // A failed SDK teardown may already have changed the driver state.
    // Block retries unless all resources were successfully released.
    teardown_incomplete = true;
    if (lost_ip_handler)
    {
        ESP_RETURN_VOID_ON_ERROR(esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_ETH_LOST_IP, lost_ip_handler), TAG, "Lost-IP handler removal failed");
        lost_ip_handler = nullptr;
    }
    if (ip_handler)
    {
        ESP_RETURN_VOID_ON_ERROR(esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_handler), TAG, "IP handler removal failed");
        ip_handler = nullptr;
    }
    if (eth_handler)
    {
        ESP_RETURN_VOID_ON_ERROR(esp_event_handler_instance_unregister(
            ETH_EVENT, ESP_EVENT_ANY_ID, eth_handler), TAG, "Ethernet handler removal failed");
        eth_handler = nullptr;
    }
    _connected = false;
#ifndef MIN_CONFIG
    httpServer.updateNetworkState();
#endif

    // Retain the dependent objects if any teardown step fails. In particular,
    // a partially started IDF driver may reject stop if its timer never ran.
    if (start_attempted)
    {
        ESP_RETURN_VOID_ON_ERROR(esp_eth_stop(eth_handle), TAG, "Ethernet stop failed; retaining driver resources");
        start_attempted = false;
    }
    if (eth_glue)
    {
        ESP_RETURN_VOID_ON_ERROR(esp_eth_del_netif_glue(eth_glue), TAG, "Ethernet glue removal failed");
        eth_glue = nullptr;
    }
    if (eth_handle)
    {
        // Uninstall deinitializes the MAC (including its ISR) and PHY.
        // It does not delete either object.
        ESP_RETURN_VOID_ON_ERROR(esp_eth_driver_uninstall(eth_handle), TAG, "Ethernet uninstall failed; retaining driver resources");
        eth_handle = nullptr;
    }
    if (w5500_phy)
    {
        ESP_RETURN_VOID_ON_ERROR(w5500_phy->del(w5500_phy), TAG, "W5500 PHY deletion failed");
        w5500_phy = nullptr;
    }
    if (w5500_mac)
    {
        ESP_RETURN_VOID_ON_ERROR(w5500_mac->del(w5500_mac), TAG, "W5500 MAC deletion failed");
        w5500_mac = nullptr;
    }
    if (owns_spi_bus)
    {
        ESP_RETURN_VOID_ON_ERROR(spi_bus_free(ETHERNET_SPI_HOST), TAG, "W5500 SPI bus release failed");
        owns_spi_bus = false;
    }
    if (eth_netif)
    {
        esp_netif_destroy(eth_netif);
        eth_netif = nullptr;
    }
    teardown_incomplete = false;

    ESP_LOGI(TAG, "W5500 Ethernet stopped");
}

void W5500Ethernet::eth_event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet link up");
                _connected = true;
                break;
                
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet link down");
                _connected = false;
#ifndef MIN_CONFIG
                httpServer.updateNetworkState();
#endif
                break;
                
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet started");
                break;
                
            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet stopped");
                _connected = false;
#ifndef MIN_CONFIG
                httpServer.updateNetworkState();
#endif
                break;
                
            default:
                break;
        }
    }
}

void W5500Ethernet::ip_event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_ETH_GOT_IP:
            {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
                const esp_netif_ip_info_t *ip_info = &event->ip_info;
                ESP_LOGI(TAG, "Got IP address");
                ESP_LOGI(TAG, "IP:      " IPSTR, IP2STR(&ip_info->ip));
                ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info->netmask));
                ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info->gw));
#ifndef MIN_CONFIG
                httpServer.updateNetworkState();
#endif
                break;
            }
            case IP_EVENT_ETH_LOST_IP:
                ESP_LOGI(TAG, "Lost IP address");
#ifndef MIN_CONFIG
                httpServer.updateNetworkState();
#endif
                break;
            default:
                break;
        }
    }
}

#endif // ENABLE_ETHERNET
