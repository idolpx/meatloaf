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

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifndef MIN_CONFIG

#include <esp_http_server.h>
#include <string>

#define http_SEND_BUFF_SIZE 512
#define http_RECV_BUFF_SIZE 512

class HttpServer
{
private:
    struct serverstate {
        httpd_handle_t hServer;
    } state;

    static void custom_global_ctx_free(void *ctx);
    static httpd_handle_t start_server(serverstate &state);

    static char *get_extension(const char *filename);
    static const char *find_mimetype_str(const char *extension);
    static void set_file_content_type(httpd_req_t *req, const char *filepath);
    static void send_file(httpd_req_t *req, const char *filename);
    static void send_file_parsed(httpd_req_t *req, const char *filename);

public:
    static httpd_handle_t s_server;
    static void send_http_error(httpd_req_t *req, int errnum);

    void start();
    void stop();
    bool running() { return state.hServer != NULL; }
};

extern HttpServer httpServer;

#endif // MIN_CONFIG
#endif // WEB_SERVER_H
