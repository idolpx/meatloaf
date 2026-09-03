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
//
#ifdef ENABLE_ETHERNET
#ifndef MEATLOAF_ETHERNET_W5500_H
#define MEATLOAF_ETHERNET_W5500_H

#include "esp_event.h"
#include "esp_eth.h"
#include "esp_netif.h"

class W5500Ethernet {
public:
    void start();
    void stop();
    esp_netif_t * get_adapter_handle() { return eth_netif; };
    bool connected() { return _connected; }

private:
    // All three are needed for teardown: stop() must delete the SAME glue
    // start() attached, not a freshly created one.
    static esp_eth_handle_t eth_handle;
    static esp_netif_t *eth_netif;
    static esp_eth_netif_glue_handle_t eth_glue;

    static bool _connected;

    static void eth_event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void ip_event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data);
};

extern W5500Ethernet ethernet;

#endif // MEATLOAF_ETHERNET_W5500_H
#endif // ENABLE_ETHERNET