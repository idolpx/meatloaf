#ifndef WEBDAV_H
#define WEBDAV_H

#include "request.h"

#include "fnFS.h"

#define http_FILE_ROOT "/.www/"
#define http_SEND_BUFF_SIZE 512 // Used when sending files in chunks
#define http_RECV_BUFF_SIZE 512 // Used when receiving POST data from client

#define MSG_ERR_OPENING_FILE     "Error opening file"
#define MSG_ERR_OUT_OF_MEMORY    "Ran out of memory"
#define MSG_ERR_UNEXPECTED_HTTPD "Unexpected web server error"
#define MSG_ERR_RECEIVE_FAILURE  "Failed to receive posted data"


enum http_err
{
    http_err_noerrr = 0,
    http_err_fileopen,
    http_err_memory,
    http_err_post_fail
};
class cHttpdServer 
{
private:
    struct serverstate {
        httpd_handle_t hServer;
        FileSystem *_FS = nullptr;
    } state;
        
    static void custom_global_ctx_free(void * ctx);

    static httpd_handle_t start_server(serverstate &state);

    static char * get_extension(const char *filename);
    static const char *find_mimetype_str(const char *extension);
    static void set_file_content_type(httpd_req_t *req, const char *filepath);
    static void send_file(httpd_req_t *req, const char *filename);
    static void send_file_parsed(httpd_req_t *req, const char *filename);
    static void return_http_error(httpd_req_t *req, http_err errnum);

public:

    static esp_err_t get_handler(httpd_req_t *httpd_req);
    static esp_err_t post_handler(httpd_req_t *httpd_req);
    static esp_err_t webdav_handler(httpd_req_t *httpd_req);
    static void webdav_register(httpd_handle_t server, const char *root_uri, const char *root_path);

    void start();
    void stop();
    bool running(void) {
        return state.hServer != NULL;
    }
};

extern cHttpdServer oHttpdServer;
#endif // WEBDAV_H