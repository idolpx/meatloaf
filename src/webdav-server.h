
// #include "freertos/FreeRTOS.h"
// #include "esp_log.h"
#include <esp_http_server.h>
// #include <esp_log.h>

// #include <server.h>
// #include <request-espidf.h>
// #include <response-espidf.h>

void webdav_register(httpd_handle_t server, const char *root_path, const char *root_uri);
static esp_err_t webdav_handler(httpd_req_t *httpd_req);
void http_server_start(void);