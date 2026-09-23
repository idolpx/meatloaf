#ifdef ENABLE_ETHERNET
#include "ethernet.h"

#include "esp_err.h"
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
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(loop_err);

    // Create Ethernet netif
    esp_netif_config_t eth_netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&eth_netif_cfg);
    if (eth_netif == NULL)
    {
        ESP_LOGE(TAG, "esp_netif_new failed; ethernet not started");
        return;
    }
    
    // Configure SPI bus for W5500
    spi_bus_config_t spi_bus_cfg = {
        .mosi_io_num = PIN_ETHERNET_MOSI,
        .miso_io_num = PIN_ETHERNET_MISO,
        .sclk_io_num = PIN_ETHERNET_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    
    ESP_ERROR_CHECK(spi_bus_initialize(ETHERNET_SPI_HOST, &spi_bus_cfg,
                                       SPI_DMA_CH_AUTO));
    
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
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    
    // Create PHY
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = -1;
    phy_config.reset_gpio_num = PIN_ETHERNET_RESET;
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    
    // Configure Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    // The W5500 has NO burned-in MAC address -- unlike the ESP32's internal
    // EMAC, nothing supplies one, so without this the interface has no valid
    // source address and DHCP never completes (link comes up, no IP).
    // esp_read_mac(ESP_MAC_ETH) derives a unique address from this chip's
    // factory eFuse, so every board gets its own rather than a shared literal.
    uint8_t eth_mac_addr[6];
    ESP_ERROR_CHECK(esp_read_mac(eth_mac_addr, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac_addr));
    ESP_LOGI(TAG, "MAC %02x:%02x:%02x:%02x:%02x:%02x",
             eth_mac_addr[0], eth_mac_addr[1], eth_mac_addr[2],
             eth_mac_addr[3], eth_mac_addr[4], eth_mac_addr[5]);
    
    // Attach Ethernet driver to netif.  The glue is kept so stop() can delete
    // the SAME one -- esp_eth_new_netif_glue() allocates, so calling it again
    // in stop() would leak this one and delete a stranger.
    eth_glue = esp_eth_new_netif_glue(eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, eth_glue));
    
    // Register Ethernet event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               &ip_event_handler, NULL));
    
    // Without this the driver is installed and attached but never brought up,
    // so no link is ever established.  stop() has the matching esp_eth_stop().
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    ESP_LOGI(TAG, "Ethernet (W5500) initialised");
}


void W5500Ethernet::stop()
{
    esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip_event_handler);

    ESP_ERROR_CHECK(esp_eth_stop(eth_handle));
    ESP_ERROR_CHECK(esp_eth_del_netif_glue(eth_glue));
    ESP_ERROR_CHECK(esp_eth_driver_uninstall(eth_handle));

    esp_netif_destroy(eth_netif);
    ESP_ERROR_CHECK(spi_bus_free(ETHERNET_SPI_HOST));

    eth_handle = nullptr;
    eth_netif  = nullptr;
    eth_glue   = nullptr;

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
                break;
                
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet started");
                break;
                
            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet stopped");
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
                break;
            }
            case IP_EVENT_ETH_LOST_IP:
                ESP_LOGI(TAG, "Lost IP address");
                break;
            default:
                break;
        }
    }
}

#endif // ENABLE_ETHERNET