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

#pragma once
#ifndef MIN_CONFIG

#include <esp_http_server.h>
#include <esp_http_client.h>

// Core fetch: issue the request to target_url and stream the response back.
// Called by proxy_handler and by get_handler when a proxy base is active.
esp_err_t proxy_fetch(httpd_req_t *req, const char *target_url);

// The origin (scheme+host) stored from the last /proxy request, or empty.
// While set, get_handler forwards all bare-path requests to this host.
const std::string &proxy_base_url();
void proxy_clear_base();

esp_err_t proxy_handler(httpd_req_t *req);
void proxy_register(httpd_handle_t server);

#endif // MIN_CONFIG
