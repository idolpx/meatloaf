
#include "template.h"

#include <sstream>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"

#include "fsFlash.h"
#include "fnFsSD.h"

#include "fnWiFi.h"
#include "ssdp.h"


bool is_parsable(const char *extension)
{
    //Debug_printv("extension[%s]", extension);
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
    //Debug_printv("parsing");
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
    // Debug_printf("Substituting tag '%s'\r\n", tag.c_str());
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
        resultstream << fsFlash.total_bytes();
        break;
    case DEVICE_SPIFFS_USED:
        resultstream << fsFlash.used_bytes();
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
#if defined( BUILD_ATARI ) || defined( BUILD_APPLE ) || defined( BUILD_IEC )
            resultstream << fnPrinters.get_ptr(0)->getPrinterPtr()->modelname();
#endif
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
#if defined( BUILD_ATARI ) || defined( BUILD_APPLE ) || defined( BUILD_IEC )
            resultstream << (fnPrinters.get_port(0) + 1);
#endif
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
        //             strncat(result, "</option>\r\n", MAX_PRINTER_LIST_BUFFER-1);
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
    // Debug_printf("Substitution result: \"%s\"\r\n", resultstream.str().c_str());
#endif
    return resultstream.str();
}
