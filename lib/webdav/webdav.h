
#include "request.h"

#define WWW_FILE_ROOT "/.www/"
#define WWW_SEND_BUFF_SIZE 512 // Used when sending files in chunks
#define WWW_RECV_BUFF_SIZE 512 // Used when receiving POST data from client

#define MSG_ERR_OPENING_FILE     "Error opening file"
#define MSG_ERR_OUT_OF_MEMORY    "Ran out of memory"
#define MSG_ERR_UNEXPECTED_HTTPD "Unexpected web server error"
#define MSG_ERR_RECEIVE_FAILURE  "Failed to receive posted data"

enum www_err
{
    www_err_noerrr = 0,
    www_err_fileopen,
    www_err_memory,
    www_err_post_fail
};

esp_err_t webdav_handler(httpd_req_t *httpd_req);
void webdav_register(httpd_handle_t server, const char *root_path, const char *root_uri);
void http_server_start(void);

std::string format_uptime();
long uptime_seconds();
const std::string substitute_tag(const std::string &tag);

std::string parse_contents(const std::string &contents);
bool is_parsable(const char *extension);

const char * find_mimetype_str(const char *extension);
char * get_extension(const char *filename);
void set_file_content_type(httpd_req_t *req, const char *filepath);
void send_file(httpd_req_t *req, const char *filename);
void send_file_parsed(httpd_req_t *req, const char *filename);
void return_http_error(httpd_req_t *req, www_err errnum);