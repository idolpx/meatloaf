#include "webdav.h"

#include "../../include/debug.h"

#include "string_utils.h"
#include "meat_io.h"
#include "meat_stream.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <esp_http_server.h>
#include <esp_log.h>

#include <server.h>
#include <request.h>
#include <response.h>

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fnFsSPIFFS.h"

#include "SSDPDevice.h"

#define MIN(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

static const char *TAG = "webdav";


/* Our URI handler function to be called during GET /uri request */
esp_err_t get_handler(httpd_req_t *req)
{
    Debug_printv("url[%s]", req->uri);

    std::string uri = mstr::urlDecode(req->uri);
    if ( uri == "/" )
    {
        uri = "/index.html";
    }

    send_file(req, uri.c_str());

    return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t post_handler(httpd_req_t *req)
{
    Debug_printv("url[%s]", req->uri);

    /* Destination buffer for content of HTTP POST request.
     * httpd_req_recv() accepts char* only, but content could
     * as well be any binary data (needs type casting).
     * In case of string data, null termination will be absent, and
     * content length would give length of string */
    char content[100];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    /* Send a simple response */
    const char resp[] = "URI POST Response";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t webdav_handler(httpd_req_t *httpd_req) {
        WebDav::Server *server = (WebDav::Server *) httpd_req->user_ctx;
        WebDav::Request req(httpd_req->uri);
        WebDav::Response resp;
        int ret;

        Debug_printv("url[%s]", httpd_req->uri);

        if (!req.parseRequest()) {
                resp.setStatus(400, "Invalid request");
                resp.flushHeaders();
                resp.closeBody();
                return ESP_OK;
        }

        httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Headers", "*");
        httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Methods", "*");

        ESP_LOGI(TAG, "%s: >%s<", http_method_str((enum http_method) httpd_req->method), httpd_req->uri);

        switch (httpd_req->method) {
                case HTTP_COPY:
                        ret = server->doCopy(req, resp);
                        break;
                case HTTP_DELETE:
                        ret = server->doDelete(req, resp);
                        break;
                case HTTP_GET:
                        ret = server->doGet(req, resp);
                        break;
                case HTTP_HEAD:
                        ret = server->doHead(req, resp);
                        break;
                case HTTP_LOCK:
                        ret = server->doLock(req, resp);
                        break;
                case HTTP_MKCOL:
                        ret = server->doMkcol(req, resp);
                        break;
                case HTTP_MOVE:
                        ret = server->doMove(req, resp);
                        break;
                case HTTP_OPTIONS:
                        ret = server->doOptions(req, resp);
                        break;
                case HTTP_PROPFIND:
                        ret = server->doPropfind(req, resp);
                        break;
                case HTTP_PROPPATCH:
                        ret = server->doProppatch(req, resp);
                        break;
                case HTTP_PUT:
                        ret = server->doPut(req, resp);
                        break;
                case HTTP_UNLOCK:
                        ret = server->doUnlock(req, resp);
                        break;
                default:
                        ret = ESP_ERR_HTTPD_INVALID_REQ;
                        break;
        }

        resp.setStatus(ret, "");
        resp.flushHeaders();
        resp.closeBody();

        return ret;
}

void webdav_register(httpd_handle_t server, const char *root_path, const char *root_uri) {
        WebDav::Server *webDavServer = new WebDav::Server(root_path, root_uri);

        char *uri;
        asprintf(&uri, "%s/?*", root_uri);

        httpd_uri_t uri_dav = 
        {
            .uri      = uri,
            .method   = http_method(0),
            .handler  = webdav_handler,
            .user_ctx = webDavServer,
        };

        http_method methods[] = {
                HTTP_COPY,
                HTTP_DELETE,
                HTTP_GET,
                HTTP_HEAD,
                HTTP_LOCK,
                HTTP_MKCOL,
                HTTP_MOVE,
                HTTP_OPTIONS,
                HTTP_PROPFIND,
                HTTP_PROPPATCH,
                HTTP_PUT,
                HTTP_UNLOCK,
        };

        for (int i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
                uri_dav.method = methods[i];
                httpd_register_uri_handler(server, &uri_dav);
        }
}

void http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 32;

    esp_log_level_set("httpd_uri", ESP_LOG_DEBUG);

    /* URI handler structure for GET /uri */
    httpd_uri_t uri_get = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = get_handler,
        .user_ctx = NULL
    };

    /* URI handler structure for POST /uri */
    httpd_uri_t uri_post = {
        .uri      = "/*",
        .method   = HTTP_POST,
        .handler  = post_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK)
    {
        /* Register URI handlers */
        webdav_register(server, "/sd", "/dav");

        // Default handlers
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);

        Serial.println( ANSI_GREEN_BOLD "WWW/WebDAV Server Started!" ANSI_RESET );

        // Start SSDP Service
        // SSDPDevice.start();
        // Serial.println( ANSI_GREEN_BOLD "SSDP Service Started!" ANSI_RESET );
    }
    else
    {
        Serial.println( ANSI_RED_BOLD "WWW/WebDAV Server FAILED to start!" ANSI_RESET );
    }
}

/* Function for stopping the webserver */
void http_server_stop(httpd_handle_t server)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}



const std::string substitute_tag(const std::string &tag)
{
    enum tagids
    {
        DEVICE_HOSTNAME = 0,
        DEVICE_VERSION,
        DEVICE_IPADDRESS,
        DEVICE_IPMASK,
        DEVICE_IPGATEWAY,
        DEVICE_IPDNS,
        DEVICE_WIFISSID,
        DEVICE_WIFIBSSID,
        DEVICE_WIFIMAC,
        DEVICE_WIFIDETAIL,
        DEVICE_SPIFFS_SIZE,
        DEVICE_SPIFFS_USED,
        DEVICE_SD_SIZE,
        DEVICE_SD_USED,
        DEVICE_UPTIME_STRING,
        DEVICE_UPTIME,
        DEVICE_CURRENTTIME,
        DEVICE_TIMEZONE,
        DEVICE_ROTATION_SOUNDS,
        DEVICE_UDPSTREAM_HOST,
        DEVICE_HEAPSIZE,
        DEVICE_SYSSDK,
        DEVICE_SYSCPUREV,
        DEVICE_SIOVOLTS,
        DEVICE_SIO_HSINDEX,
        DEVICE_SIO_HSBAUD,
        DEVICE_PRINTER1_MODEL,
        DEVICE_PRINTER1_PORT,
        DEVICE_PLAY_RECORD,
        DEVICE_PULLDOWN,
        DEVICE_CASSETTE_ENABLED,
        DEVICE_CONFIG_ENABLED,
        DEVICE_STATUS_WAIT_ENABLED,
        DEVICE_BOOT_MODE,
        DEVICE_PRINTER_ENABLED,
        DEVICE_MODEM_ENABLED,
        DEVICE_MODEM_SNIFFER_ENABLED,
        DEVICE_DRIVE1HOST,
        DEVICE_DRIVE2HOST,
        DEVICE_DRIVE3HOST,
        DEVICE_DRIVE4HOST,
        DEVICE_DRIVE5HOST,
        DEVICE_DRIVE6HOST,
        DEVICE_DRIVE7HOST,
        DEVICE_DRIVE8HOST,
        DEVICE_DRIVE1MOUNT,
        DEVICE_DRIVE2MOUNT,
        DEVICE_DRIVE3MOUNT,
        DEVICE_DRIVE4MOUNT,
        DEVICE_DRIVE5MOUNT,
        DEVICE_DRIVE6MOUNT,
        DEVICE_DRIVE7MOUNT,
        DEVICE_DRIVE8MOUNT,
        DEVICE_HOST1,
        DEVICE_HOST2,
        DEVICE_HOST3,
        DEVICE_HOST4,
        DEVICE_HOST5,
        DEVICE_HOST6,
        DEVICE_HOST7,
        DEVICE_HOST8,
        DEVICE_DRIVE1,
        DEVICE_DRIVE2,
        DEVICE_DRIVE3,
        DEVICE_DRIVE4,
        DEVICE_DRIVE5,
        DEVICE_DRIVE6,
        DEVICE_DRIVE7,
        DEVICE_DRIVE8,
        DEVICE_HOST1PREFIX,
        DEVICE_HOST2PREFIX,
        DEVICE_HOST3PREFIX,
        DEVICE_HOST4PREFIX,
        DEVICE_HOST5PREFIX,
        DEVICE_HOST6PREFIX,
        DEVICE_HOST7PREFIX,
        DEVICE_HOST8PREFIX,
        DEVICE_ERRMSG,
        DEVICE_HARDWARE_VER,
        DEVICE_PRINTER_LIST,
        DEVICE_UUID,
        DEVICE_LASTTAG
    };

    const char *tagids[DEVICE_LASTTAG] =
    {
        "DEVICE_HOSTNAME",
        "DEVICE_VERSION",
        "DEVICE_IPADDRESS",
        "DEVICE_IPMASK",
        "DEVICE_IPGATEWAY",
        "DEVICE_IPDNS",
        "DEVICE_WIFISSID",
        "DEVICE_WIFIBSSID",
        "DEVICE_WIFIMAC",
        "DEVICE_WIFIDETAIL",
        "DEVICE_SPIFFS_SIZE",
        "DEVICE_SPIFFS_USED",
        "DEVICE_SD_SIZE",
        "DEVICE_SD_USED",
        "DEVICE_UPTIME_STRING",
        "DEVICE_UPTIME",
        "DEVICE_CURRENTTIME",
        "DEVICE_TIMEZONE",
        "DEVICE_ROTATION_SOUNDS",
        "DEVICE_UDPSTREAM_HOST",
        "DEVICE_HEAPSIZE",
        "DEVICE_SYSSDK",
        "DEVICE_SYSCPUREV",
        "DEVICE_SIOVOLTS",
        "DEVICE_SIO_HSINDEX",
        "DEVICE_SIO_HSBAUD",
        "DEVICE_PRINTER1_MODEL",
        "DEVICE_PRINTER1_PORT",
        "DEVICE_PLAY_RECORD",
        "DEVICE_PULLDOWN",
        "DEVICE_CASSETTE_ENABLED",
        "DEVICE_CONFIG_ENABLED",
        "DEVICE_STATUS_WAIT_ENABLED",
        "DEVICE_BOOT_MODE",
        "DEVICE_PRINTER_ENABLED",
        "DEVICE_MODEM_ENABLED",
        "DEVICE_MODEM_SNIFFER_ENABLED",
        "DEVICE_DRIVE1HOST",
        "DEVICE_DRIVE2HOST",
        "DEVICE_DRIVE3HOST",
        "DEVICE_DRIVE4HOST",
        "DEVICE_DRIVE5HOST",
        "DEVICE_DRIVE6HOST",
        "DEVICE_DRIVE7HOST",
        "DEVICE_DRIVE8HOST",
        "DEVICE_DRIVE1MOUNT",
        "DEVICE_DRIVE2MOUNT",
        "DEVICE_DRIVE3MOUNT",
        "DEVICE_DRIVE4MOUNT",
        "DEVICE_DRIVE5MOUNT",
        "DEVICE_DRIVE6MOUNT",
        "DEVICE_DRIVE7MOUNT",
        "DEVICE_DRIVE8MOUNT",
        "DEVICE_HOST1",
        "DEVICE_HOST2",
        "DEVICE_HOST3",
        "DEVICE_HOST4",
        "DEVICE_HOST5",
        "DEVICE_HOST6",
        "DEVICE_HOST7",
        "DEVICE_HOST8",
        "DEVICE_DRIVE1",
        "DEVICE_DRIVE2",
        "DEVICE_DRIVE3",
        "DEVICE_DRIVE4",
        "DEVICE_DRIVE5",
        "DEVICE_DRIVE6",
        "DEVICE_DRIVE7",
        "DEVICE_DRIVE8",
        "DEVICE_HOST1PREFIX",
        "DEVICE_HOST2PREFIX",
        "DEVICE_HOST3PREFIX",
        "DEVICE_HOST4PREFIX",
        "DEVICE_HOST5PREFIX",
        "DEVICE_HOST6PREFIX",
        "DEVICE_HOST7PREFIX",
        "DEVICE_HOST8PREFIX",
        "DEVICE_ERRMSG",
        "DEVICE_HARDWARE_VER",
        "DEVICE_PRINTER_LIST",
        "DEVICE_UUID"
    };

    std::stringstream resultstream;

#ifdef DEBUG
    // Debug_printf("Substituting tag '%s'\n", tag.c_str());
#endif

    int tagid;
    for (tagid = 0; tagid < DEVICE_LASTTAG; tagid++)
    {
        if (0 == tag.compare(tagids[tagid]))
        {
            break;
        }
    }

    int drive_slot, host_slot;
    char disk_id;

    // Provide a replacement value
    switch (tagid)
    {
    case DEVICE_HOSTNAME:
        resultstream << fnSystem.Net.get_hostname();
        break;
    case DEVICE_VERSION:
        resultstream << fnSystem.get_fujinet_version();
        break;
    case DEVICE_UUID:
        resultstream << SSDPDevice.getUUID();
        break;
    case DEVICE_IPADDRESS:
        resultstream << fnSystem.Net.get_ip4_address_str();
        break;
    case DEVICE_IPMASK:
        resultstream << fnSystem.Net.get_ip4_mask_str();
        break;
    case DEVICE_IPGATEWAY:
        resultstream << fnSystem.Net.get_ip4_gateway_str();
        break;
    case DEVICE_IPDNS:
        resultstream << fnSystem.Net.get_ip4_dns_str();
        break;
    case DEVICE_WIFISSID:
        resultstream << fnWiFi.get_current_ssid();
        break;
    case DEVICE_WIFIBSSID:
        resultstream << fnWiFi.get_current_bssid_str();
        break;
    case DEVICE_WIFIMAC:
        resultstream << fnWiFi.get_mac_str();
        break;
    case DEVICE_WIFIDETAIL:
        resultstream << fnWiFi.get_current_detail_str();
        break;
    case DEVICE_SPIFFS_SIZE:
        resultstream << fnSPIFFS.total_bytes();
        break;
    case DEVICE_SPIFFS_USED:
        resultstream << fnSPIFFS.used_bytes();
        break;
    case DEVICE_SD_SIZE:
        resultstream << fnSDFAT.total_bytes();
        break;
    case DEVICE_SD_USED:
        resultstream << fnSDFAT.used_bytes();
        break;
    case DEVICE_UPTIME_STRING:
        resultstream << format_uptime();
        break;
    case DEVICE_UPTIME:
        resultstream << uptime_seconds();
        break;
    case DEVICE_CURRENTTIME:
        resultstream << fnSystem.get_current_time_str();
        break;
    case DEVICE_TIMEZONE:
        resultstream << Config.get_general_timezone();
        break;
    case DEVICE_ROTATION_SOUNDS:
        resultstream << Config.get_general_rotation_sounds();
        break;
    case DEVICE_UDPSTREAM_HOST:
        if (Config.get_network_udpstream_port() > 0)
            resultstream << Config.get_network_udpstream_host() << ":" << Config.get_network_udpstream_port();
        else
            resultstream << Config.get_network_udpstream_host();
        break;
    case DEVICE_HEAPSIZE:
        resultstream << fnSystem.get_free_heap_size();
        break;
    case DEVICE_SYSSDK:
        resultstream << fnSystem.get_sdk_version();
        break;
    case DEVICE_SYSCPUREV:
        resultstream << fnSystem.get_cpu_rev();
        break;
    case DEVICE_SIOVOLTS:
        resultstream << ((float)fnSystem.get_sio_voltage()) / 1000.00 << "V";
        break;
#ifdef BUILD_ATARI
    case DEVICE_SIO_HSINDEX:
        resultstream << SIO.getHighSpeedIndex();
        break;
    case DEVICE_SIO_HSBAUD:
        resultstream << SIO.getHighSpeedBaud();
        break;
#endif /* BUILD_ATARI */
    case DEVICE_PRINTER1_MODEL:
        {
#ifdef BUILD_ADAM
            adamPrinter *ap = fnPrinters.get_ptr(0);
            if (ap != nullptr)
            {
                resultstream << fnPrinters.get_ptr(0)->getPrinterPtr()->modelname();
            } else
                resultstream << "No Virtual Printer";
#endif /* BUILD_ADAM */
#ifdef BUILD_ATARI
            resultstream << fnPrinters.get_ptr(0)->getPrinterPtr()->modelname();
#endif /* BUILD_ATARI */
#ifdef BUILD_APPLE
            resultstream << fnPrinters.get_ptr(0)->getPrinterPtr()->modelname();
#endif /* BUILD_APPLE */
        }
        break;
    case DEVICE_PRINTER1_PORT:
        {
#ifdef BUILD_ADAM
            adamPrinter *ap = fnPrinters.get_ptr(0);
            if (ap != nullptr)
            {
                resultstream << (fnPrinters.get_port(0) + 1);
            } else
                resultstream << "";
#endif/* BUILD_ADAM */
#ifdef BUILD_ATARI
            resultstream << (fnPrinters.get_port(0) + 1);
#endif /* BUILD_ATARI */
#ifdef BUILD_APPLE
            resultstream << (fnPrinters.get_port(0) + 1);
#endif /* BUILD_APPLE */
        }
        break;
#ifdef BUILD_ATARI        
    case DEVICE_PLAY_RECORD:
        if (theFuji.cassette()->get_buttons())
            resultstream << "0 PLAY";
        else
            resultstream << "1 RECORD";
        break;
    case DEVICE_PULLDOWN:
        if (theFuji.cassette()->has_pulldown())
            resultstream << "1 Pulldown Resistor";
        else
            resultstream << "0 B Button Press";
        break;
    case DEVICE_CASSETTE_ENABLED:
        resultstream << Config.get_cassette_enabled();
        break;
#endif /* BUILD_ATARI */
    case DEVICE_CONFIG_ENABLED:
        resultstream << Config.get_general_config_enabled();
        break;
    case DEVICE_STATUS_WAIT_ENABLED:
        resultstream << Config.get_general_status_wait_enabled();
        break;
    case DEVICE_BOOT_MODE:
        resultstream << Config.get_general_boot_mode();
        break;
    case DEVICE_PRINTER_ENABLED:
        resultstream << Config.get_printer_enabled();
        break;
    case DEVICE_MODEM_ENABLED:
        resultstream << Config.get_modem_enabled();
        break;
    case DEVICE_MODEM_SNIFFER_ENABLED:
        resultstream << Config.get_modem_sniffer_enabled();
        break;
    case DEVICE_DRIVE1HOST:
    case DEVICE_DRIVE2HOST:
    case DEVICE_DRIVE3HOST:
    case DEVICE_DRIVE4HOST:
    case DEVICE_DRIVE5HOST:
    case DEVICE_DRIVE6HOST:
    case DEVICE_DRIVE7HOST:
    case DEVICE_DRIVE8HOST:
	/* From what host is each disk is mounted on each Drive Slot? */
	drive_slot = tagid - DEVICE_DRIVE1HOST;
	host_slot = Config.get_mount_host_slot(drive_slot);
        if (host_slot != HOST_SLOT_INVALID) {
	    resultstream << Config.get_host_name(host_slot);
        } else {
            resultstream << "";
        }
        break;
    case DEVICE_DRIVE1MOUNT:
    case DEVICE_DRIVE2MOUNT:
    case DEVICE_DRIVE3MOUNT:
    case DEVICE_DRIVE4MOUNT:
    case DEVICE_DRIVE5MOUNT:
    case DEVICE_DRIVE6MOUNT:
    case DEVICE_DRIVE7MOUNT:
    case DEVICE_DRIVE8MOUNT:
	/* What disk is mounted on each Drive Slot (and is it read-only or read-write)? */
	drive_slot = tagid - DEVICE_DRIVE1MOUNT;
	host_slot = Config.get_mount_host_slot(drive_slot);
        if (host_slot != HOST_SLOT_INVALID) {
	    resultstream << Config.get_mount_path(drive_slot);
	    resultstream << " (" << (Config.get_mount_mode(drive_slot) == fnConfig::mount_modes::MOUNTMODE_READ ? "R" : "W") << ")";
        } else {
            resultstream << "(Empty)";
        }
        break;
    case DEVICE_HOST1:
    case DEVICE_HOST2:
    case DEVICE_HOST3:
    case DEVICE_HOST4:
    case DEVICE_HOST5:
    case DEVICE_HOST6:
    case DEVICE_HOST7:
    case DEVICE_HOST8:
	/* What TNFS host is mounted on each Host Slot? */
	host_slot = tagid - DEVICE_HOST1;
        if (Config.get_host_type(host_slot) != fnConfig::host_types::HOSTTYPE_INVALID) {
	    resultstream << Config.get_host_name(host_slot);
        } else {
            resultstream << "(Empty)";
        }
        break;
    case DEVICE_DRIVE1:
    case DEVICE_DRIVE2:
    case DEVICE_DRIVE3:
    case DEVICE_DRIVE4:
    case DEVICE_DRIVE5:
    case DEVICE_DRIVE6:
    case DEVICE_DRIVE7:
    case DEVICE_DRIVE8:
        /* What Dx: drive (if any rotation has occurred) does each Drive Slot currently map to? */
        // drive_slot = tagid - DEVICE_DRIVE1;
        // disk_id = (char) theFuji.get_disk_id(drive_slot);
        // if (disk_id > 0 && disk_id != (char) (0x31 + drive_slot)) {
        //     resultstream << " (D" << disk_id << ":)";
        // }
        break;
    case DEVICE_HOST1PREFIX:
    case DEVICE_HOST2PREFIX:
    case DEVICE_HOST3PREFIX:
    case DEVICE_HOST4PREFIX:
    case DEVICE_HOST5PREFIX:
    case DEVICE_HOST6PREFIX:
    case DEVICE_HOST7PREFIX:
    case DEVICE_HOST8PREFIX:
	/* What directory prefix is set right now
           for the TNFS host mounted on each Host Slot? */
	// host_slot = tagid - DEVICE_HOST1PREFIX;
        // if (Config.get_host_type(host_slot) != fnConfig::host_types::HOSTTYPE_INVALID) {
	//     resultstream << theFuji.get_host_prefix(host_slot);
        // } else {
        //     resultstream << "";
        // }
        break;
    case DEVICE_ERRMSG:
        //resultstream << fnHTTPD.getErrMsg();
        break;
    case DEVICE_HARDWARE_VER:
        resultstream << fnSystem.get_hardware_ver_str();
        break;
    case DEVICE_PRINTER_LIST:
        // {
        //     char *result = (char *) malloc(MAX_PRINTER_LIST_BUFFER);
        //     if (result != NULL)
        //     {
        //         strcpy(result, "");

        //         for(int i=0; i<(int) PRINTER_CLASS::PRINTER_INVALID; i++)
        //         {
        //             strncat(result, "<option value=\"", MAX_PRINTER_LIST_BUFFER-1);
        //             strncat(result, PRINTER_CLASS::printer_model_str[i], MAX_PRINTER_LIST_BUFFER-1);
        //             strncat(result, "\">", MAX_PRINTER_LIST_BUFFER);
        //             strncat(result, PRINTER_CLASS::printer_model_str[i], MAX_PRINTER_LIST_BUFFER-1);
        //             strncat(result, "</option>\n", MAX_PRINTER_LIST_BUFFER-1);
        //         }
        //         resultstream << result;
        //         free(result);
        //     } else
        //         resultstream << "Insufficent memory";
        // }
        break;
    default:
        resultstream << tag;
        break;
    }
#ifdef DEBUG
    // Debug_printf("Substitution result: \"%s\"\n", resultstream.str().c_str());
#endif
    return resultstream.str();
}

bool is_parsable(const char *extension)
{
    Debug_printv("extension[%s]", extension);
    if (extension != NULL)
    {
        if (strncmp(extension, "html", 4) == 0)
            return true;
        if (strncmp(extension, "xml", 3) == 0)
            return true;
        if (strncmp(extension, "json", 4) == 0)
            return true;
    }
    return false;
}


// Look for anything between {{ and }} tags
// And send that to a routine that looks for suitable substitutions
// Returns string with subtitutions in place
std::string parse_contents(const std::string &contents)
{
    Debug_printv("parsing");
    std::stringstream ss;
    uint pos = 0, x, y;
    do
    {
        x = contents.find("{{", pos);
        if (x == std::string::npos)
        {
            ss << contents.substr(pos);
            break;
        }
        // Found opening tag, now find ending
        y = contents.find("}}", x + 2);
        if (y == std::string::npos)
        {
            ss << contents.substr(pos);
            break;
        }
        // Now we have starting and ending tags
        if (x > 0)
            ss << contents.substr(pos, x - pos);
        ss << substitute_tag(contents.substr(x + 2, y - x - 2));
        pos = y + 2;
    } while (true);

    return ss.str();
}

long uptime_seconds()
{
    return fnSystem.get_uptime() / 1000000;
}

std::string format_uptime()
{
    int64_t ms = fnSystem.get_uptime();
    long s = ms / 1000000;

    int m = s / 60;
    int h = m / 60;
    int d = h / 24;

    std::stringstream resultstream;
    if (d)
        resultstream << d << " days, ";
    if (h % 24)
        resultstream << (h % 24) << " hours, ";
    if (m % 60)
        resultstream << (m % 60) << " minutes, ";
    if (s % 60)
        resultstream << (s % 60) << " seconds";

    return resultstream.str();
}


const char *find_mimetype_str(const char *extension)
{
    static std::map<std::string, std::string> mime_map {
        {"css", "text/css"},
        {"txt", "text/plain"},
        {"js",  "text/javascript"},
        {"xml", "text/xml; charset=\"utf-8\""},

        {"gif", "image/gif"},
        {"ico", "image/x-icon"},
        {"jpg", "image/jpeg"},
        {"png", "image/png"},
        {"svg", "image/svg+xml"},

        {"atascii", "application/octet-stream"},
        {"bin",     "application/octet-stream"},
        {"json",    "application/json"},
        {"pdf",     "application/pdf"}
    };

    if (extension != NULL)
    {
        std::map<std::string, std::string>::iterator mmatch;

        mmatch = mime_map.find(extension);
        if (mmatch != mime_map.end())
            return mmatch->second.c_str();
    }
    return NULL;
}

char *get_extension(const char *filename)
{
    char *result = strrchr(filename, '.');
    if (result != NULL)
        return ++result;
    return NULL;
}

// Set the response content type based on the file being sent.
// Just using the file extension
// If nothing is set here, the default is 'text/html'
void set_file_content_type(httpd_req_t *req, const char *filepath)
{
    // Find the current file extension
    char *dot = get_extension(filepath);
    if (dot != NULL)
    {
        const char *mimetype = find_mimetype_str(dot);
        if (mimetype)
            httpd_resp_set_type(req, mimetype);
    }
}

// Send content of given file out to client
void send_file(httpd_req_t *req, const char *filename)
{
    // Build the full file path
    std::string fpath = WWW_FILE_ROOT;
    // Trim any '/' prefix before adding it to the base directory
    while (*filename == '/')
        filename++;
    fpath += filename;

    // Handle file differently if it's one of the types we parse
    if (is_parsable(get_extension(filename)))
        return send_file_parsed(req, fpath.c_str());

    Debug_printv("filename[%s]", filename);
    auto file = MFSOwner::File( fpath );
    if (!file->exists())
    {
        Debug_printf("Failed to open file for sending: '%s'\n", fpath.c_str());
        return_http_error(req, www_err_fileopen);
    }
    else
    {
        auto istream = file->meatStream();

        // Set the response content type
        set_file_content_type(req, fpath.c_str());
        // Set the expected length of the content
        char hdrval[10];
        snprintf(hdrval, 10, "%d", istream->size());
        httpd_resp_set_hdr(req, "Content-Length", hdrval);

        // Send the file content out in chunks
        char *buf = (char *)malloc(WWW_SEND_BUFF_SIZE);
        size_t count = 0;
        do
        {
            count = istream->read( (uint8_t *)buf, WWW_SEND_BUFF_SIZE );
            httpd_resp_send_chunk(req, buf, count);
        } while (count > 0);
        free(buf);
    }
}


// Send file content after parsing for replaceable strings
void send_file_parsed(httpd_req_t *req, const char *filename)
{
    // Note that we don't add FNWS_FILE_ROOT as it should've been done in send_file()

    www_err err = www_err_noerrr;

    Debug_printv("filename[%s]", filename);
    auto file = MFSOwner::File( filename );
    if (!file->exists())
    {
        Debug_println("Failed to open file for parsing");
        err = www_err_fileopen;
    }
    else
    {
        auto istream = file->meatStream();

        // Set the response content type
        set_file_content_type(req, filename);
        // We're going to load the whole thing into memory, so watch out for big files!
        size_t sz = istream->size() + 1;
        char *buf = (char *)calloc(sz, 1);
        if (buf == NULL)
        {
            Debug_printf("Couldn't allocate %u bytes to load file contents!\n", sz);
            err = www_err_memory;
        }
        else
        {
            istream->read( (uint8_t *)buf, sz );
            std::string contents(buf);
            free(buf);
            contents = parse_contents(contents);

            httpd_resp_send(req, contents.c_str(), contents.length());
        }
    }

    if (err != www_err_noerrr)
        return_http_error(req, err);
}

// Send some meaningful(?) error message to client
void return_http_error(httpd_req_t *req, www_err errnum)
{
    const char *message;

    switch (errnum)
    {
    case www_err_fileopen:
        message = MSG_ERR_OPENING_FILE;
        break;
    case www_err_memory:
        message = MSG_ERR_OUT_OF_MEMORY;
        break;
    default:
        message = MSG_ERR_UNEXPECTED_HTTPD;
        break;
    }
    httpd_resp_send(req, message, strlen(message));
}