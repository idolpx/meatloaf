
#include <request-espidf.h>


esp_err_t webdav_handler(httpd_req_t *httpd_req);
void webdav_register(httpd_handle_t server, const char *root_path, const char *root_uri);
void http_server_start(void);
