#ifdef BUILD_IEC

#include "meatloaf.h"

#include <cstring>

#include <unordered_map>

#include "../../include/debug.h"
#include "../../include/cbm_defines.h"

#include "fsFlash.h"
#include "fnFsSD.h"
#include "fnWiFi.h"
#include "fnConfig.h"
#include "fujiCmd.h"
#include "status_error_codes.h"

#include "device.h"

#include "led.h"
#include "led_strip.h"
#include "utils.h"

#include "meat_media.h"


// iecMeatloaf Meatloaf; // global meatloaf device object
// iecNetwork sioNetDevs[MAX_NETWORK_DEVICES];

// bool _validate_host_slot(uint8_t slot, const char *dmsg = nullptr);
// bool _validate_device_slot(uint8_t slot, const char *dmsg = nullptr);

// bool _validate_host_slot(uint8_t slot, const char *dmsg)
// {
//     if (slot < MAX_HOSTS)
//         return true;

//     if (dmsg == NULL)
//     {
//         Debug_printf("!! Invalid host slot %hu\n", slot);
//     }
//     else
//     {
//         Debug_printf("!! Invalid host slot %hu @ %s\n", slot, dmsg);
//     }

//     return false;
// }

// bool _validate_device_slot(uint8_t slot, const char *dmsg)
// {
//     if (slot < MAX_DISK_DEVICES)
//         return true;

//     if (dmsg == NULL)
//     {
//         Debug_printf("!! Invalid device slot %hu\n", slot);
//     }
//     else
//     {
//         Debug_printf("!! Invalid device slot %hu @ %s\n", slot, dmsg);
// }

//     return false;
// }

// Constructor
iecMeatloaf::iecMeatloaf()
{
    // device_active = false;
    device_active = true; // temporary during bring-up

    _base.reset( MFSOwner::File("/") );
    _last_file = "";

    // // Helpful for debugging
    // for (int i = 0; i < MAX_HOSTS; i++)
    //     _fnHosts[i].slotid = i;
}

// Destructor
iecMeatloaf::~iecMeatloaf()
{

}

// Initializes base settings and adds our devices to the SIO bus
void iecMeatloaf::setup(systemBus *bus)
{
//     // TODO IMPLEMENT
//     Debug_printf("iecFuji::setup()\n");

//     // _populate_slots_from_config();

//     FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fsFlash;
// //    iecPrinter::printer_type ptype = Config.get_printer_type(0);
//     iecPrinter::printer_type ptype = iecPrinter::printer_type::PRINTER_COMMODORE_MPS803; // temporary
//     Debug_printf("Creating a default printer using %s storage and type %d\r\n", ptrfs->typestring(), ptype);
//     iecPrinter *ptr = new iecPrinter(ptrfs, ptype);
//     fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

//     Serial.print("Printer "); bus->addDevice(ptr, 4);                   // 04-07 Printers / Plotters
//     Serial.print("Disk "); bus->addDevice(new iecDisk(), 8);            // 08-16 Drives
//     Serial.print("Network "); bus->addDevice(new iecNetwork(), 16);     // 16-19 Network Devices
//     Serial.print("CPM "); bus->addDevice(new iecCpm(), 20);             // 20-29 Other
//     Serial.print("Clock "); bus->addDevice(new iecClock(), 29);
//     Serial.print("FujiNet "); bus->addDevice(this, 30);                 // 30    FujiNet
}

void logResponse(const void* data, size_t length)
{
    // ensure we don't flood the logs with debug, and make it look pretty using util_hexdump
    uint8_t debug_len = length;
    bool is_truncated = false;
    if (debug_len > 64) {
        debug_len = 64;
        is_truncated = true;
    }

    std::string msg = util_hexdump(data, debug_len);
    Debug_printf("Sending:\n%s\n", msg.c_str());
    if (is_truncated) {
        Debug_printf("[truncated from %d]\n", length);
    }

    // Debug_printf("  ");
    // // ASCII Text representation
    // for (int i=0;i<length;i++)
    // {
    //     char c = petscii2ascii(data[i]);
    //     Debug_printf("%c", c<0x20 || c>0x7f ? '.' : c);
    // }

}

device_state_t iecMeatloaf::process()
{
    virtualDevice::process();

    if (commanddata.channel != CHANNEL_COMMAND)
    {
        Debug_printf("Meatloaf device only accepts on channel 15. Sending NOTFOUND.\r\n");
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return state;
    }

    if (commanddata.primary == IEC_TALK && commanddata.secondary == IEC_REOPEN)
    {
        #ifdef DEBUG
        if (!response.empty() && !is_raw_command) logResponse(response.data(), response.size());
        if (!responseV.empty() && is_raw_command) logResponse(responseV.data(), responseV.size());
        Debug_printf("\n");
        #endif

        // TODO: review everywhere that directly uses IEC.sendBytes and make them all use iec with a common method?

        // only send raw back for a raw command, thus code can set "response", but we won't send it back as that's BASIC response
        if (!responseV.empty() && is_raw_command) {
            IEC.sendBytes(reinterpret_cast<char*>(responseV.data()), responseV.size());
        }

        // only send string response back for basic command        
        if(!response.empty() && !is_raw_command) {
            IEC.sendBytes(const_cast<char*>(response.c_str()), response.size());
        }

        // ensure responses are cleared for next command in case they were set but didn't match the command type (i.e. basic or raw)
        responseV.clear();
        response = "";

    }
    else if (commanddata.primary == IEC_UNLISTEN)
    {
        is_raw_command = payload[0] > 0x7F;
        if (is_raw_command)
            process_raw_commands();
        else
            process_basic_commands();
    }

    return state;
}

// COMMODORE SPECIFIC CONVENIENCE COMMANDS /////////////////////

void iecMeatloaf::local_ip()
{
    char msg[17];

    fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);

    sprintf(msg, "%u.%u.%u.%u", cfg.localIP[0], cfg.localIP[1], cfg.localIP[2], cfg.localIP[3]);

    iecStatus.channel = 15;
    iecStatus.error = 0;
    iecStatus.msg = std::string(msg);
    iecStatus.connected = 0;
}


void iecMeatloaf::process_basic_commands()
{
    payloadRaw = payload;               // required for appkey in BASIC
    payload = mstr::toUTF8(payload);
    pt = util_tokenize(payload, ',');

    if (payload.find("adapterconfig") != std::string::npos)
        get_adapter_config();
    else if (payload.find("setssid") != std::string::npos)
        net_set_ssid_basic();
    else if (payload.find("getssid") != std::string::npos)
        net_get_ssid_basic();
    else if (payload.find("reset") != std::string::npos)
        reset_device();
    else if (payload.find("scanresult") != std::string::npos)
        net_scan_result_basic();
    else if (payload.find("scan") != std::string::npos)
        net_scan_networks_basic();
    else if (payload.find("wifistatus") != std::string::npos)
        net_get_wifi_status();
    // else if (payload.find("mounthost") != std::string::npos)
    //     mount_host_basic();
    // else if (payload.find("mountdrive") != std::string::npos)
    //     disk_image_mount_basic();
    // else if (payload.find("opendir") != std::string::npos)
    //     open_directory_basic();
    // else if (payload.find("readdir") != std::string::npos)
    //     read_directory_entry();
    // else if (payload.find("closedir") != std::string::npos)
    //     close_directory();
    // else if (payload.find("gethost") != std::string::npos ||
    //          payload.find("flh") != std::string::npos)
    //     read_host_slots();
    // else if (payload.find("puthost") != std::string::npos ||
    //          payload.find("fhost") != std::string::npos)
    //     write_host_slots();
    // else if (payload.find("getdrive") != std::string::npos)
    //     read_device_slots();
    // else if (payload.find("unmounthost") != std::string::npos)
    //     unmount_host();
    // else if (payload.find("getdirpos") != std::string::npos)
    //     get_directory_position();
    // else if (payload.find("setdirpos") != std::string::npos)
    //     set_directory_position();
    // else if (payload.find("setdrivefilename") != std::string::npos)
    //     set_device_filename();
    else if (payload.find("writeappkey") != std::string::npos)
        write_app_key_basic();
    else if (payload.find("readappkey") != std::string::npos)
        read_app_key();
    else if (payload.find("openappkey") != std::string::npos)
        open_app_key();
    else if (payload.find("closeappkey") != std::string::npos)
        close_app_key();
    // else if (payload.find("drivefilename") != std::string::npos)
    //     get_device_filename();
    // else if (payload.find("bootconfig") != std::string::npos)
    //     set_boot_config();
    // else if (payload.find("bootmode") != std::string::npos)
    //     set_boot_mode();
    // else if (payload.find("mountall") != std::string::npos)
    //     mount_all();
    else if (payload.find("localip") != std::string::npos)
        local_ip();
    else if (payload.find("bptiming") != std::string::npos)
    {
        if ( pt.size() < 3 ) 
            return;

        IEC.setBitTiming(pt[1], atoi(pt[2].c_str()), atoi(pt[3].c_str()), atoi(pt[4].c_str()), atoi(pt[5].c_str()));
    }

}

void iecMeatloaf::process_raw_commands()
{
    Debug_printv("payload[%d]", payload[0]);
    switch (payload[0])
    {
    case FUJICMD_RESET:
        reset_device();
        break;
    case FUJICMD_GET_SSID:
        net_get_ssid_raw();
        break;
    case FUJICMD_SCAN_NETWORKS:
        net_scan_networks_raw();
        break;
    case FUJICMD_GET_SCAN_RESULT:
        net_scan_result_raw();
        break;
    case FUJICMD_SET_SSID:
        net_set_ssid_raw();
        break;
    case FUJICMD_GET_WIFISTATUS:
        net_get_wifi_status();
        break;
    // case FUJICMD_MOUNT_HOST:
    //     mount_host_raw();
    //     break;
    // case FUJICMD_MOUNT_IMAGE:
    //     disk_image_mount_raw();
    //     break;
    // case FUJICMD_OPEN_DIRECTORY:
    //     open_directory_raw();
    //     break;
    // case FUJICMD_READ_DIR_ENTRY:
    //     read_directory_entry();
    //     break;
    // case FUJICMD_CLOSE_DIRECTORY:
    //     close_directory();
    //     break;
    // case FUJICMD_READ_HOST_SLOTS:
    //     read_host_slots();
    //     break;
    // case FUJICMD_WRITE_HOST_SLOTS:
    //     write_host_slots();
    //     break;
    // case FUJICMD_READ_DEVICE_SLOTS:
    //     read_device_slots();
    //     break;
    // case FUJICMD_WRITE_DEVICE_SLOTS:
    //     write_device_slots();
    //     break;
    case FUJICMD_ENABLE_UDPSTREAM:
        // Not implemented.
        break;
    // case FUJICMD_UNMOUNT_IMAGE:
    //     disk_image_umount();
    //     break;
    // case FUJICMD_UNMOUNT_HOST:
    //     unmount_host();
    //     break;
    case FUJICMD_GET_ADAPTERCONFIG:
        get_adapter_config();
        break;
    // case FUJICMD_GET_DIRECTORY_POSITION:
    //     get_directory_position();
    //     break;
    // case FUJICMD_SET_DIRECTORY_POSITION:
    //     set_directory_position();
    //     break;
    // case FUJICMD_SET_DEVICE_FULLPATH:
    //     set_device_filename();
    //     break;
    case FUJICMD_WRITE_APPKEY:
        write_app_key_raw();
        break;
    case FUJICMD_READ_APPKEY:
        read_app_key();
        break;
    case FUJICMD_OPEN_APPKEY:
        open_app_key();
        break;
    case FUJICMD_CLOSE_APPKEY:
        close_app_key();
        break;
    // case FUJICMD_GET_DEVICE_FULLPATH:
    //     get_device_filename();
    //     break;
    // case 0xD9:
    //     set_boot_config();
    //     break;
    // case FUJICMD_SET_BOOT_MODE:
    //     set_boot_mode();
    //     break;
    // case FUJICMD_MOUNT_ALL:
    //     mount_all();
    //     break;
    }
}


// Reset Device
void iecMeatloaf::reset_device()
{
    fnSystem.reboot();
}

void iecMeatloaf::net_scan_networks_basic()
{
    net_scan_networks();
    response = std::to_string(_countScannedSSIDs);
}

void iecMeatloaf::net_scan_networks_raw()
{
    net_scan_networks();
    responseV.push_back(_countScannedSSIDs);
}

void iecMeatloaf::net_scan_networks()
{
    _countScannedSSIDs = fnWiFi.scan_networks();
}

void iecMeatloaf::net_scan_result_basic()
{
    if (pt.size() != 2) {
        state = DEVICE_ERROR;
        return;
    }
    util_remove_spaces(pt[1]);
    int i = atoi(pt[1].c_str());
    scan_result_t result = net_scan_result(i);
    response = std::to_string(result.rssi) + ",\"" + std::string(result.ssid) + "\"";
}

void iecMeatloaf::net_scan_result_raw()
{
    int n = payload[1];
    scan_result_t result = net_scan_result(n);
    responseV.assign(reinterpret_cast<const uint8_t*>(&result), reinterpret_cast<const uint8_t*>(&result) + sizeof(result));
}

scan_result_t iecMeatloaf::net_scan_result(int scan_num)
{
    scan_result_t result;
    memset(&result.ssid[0], 0, sizeof(scan_result_t));
    fnWiFi.get_scan_result(scan_num, result.ssid, &result.rssi);
    Debug_printf("SSID: %s RSSI: %u\r\n", result.ssid, result.rssi);
    return result;
}

void iecMeatloaf::net_get_ssid_basic()
{
    net_config_t net_config = net_get_ssid();
    response = std::string(net_config.ssid);
}

void iecMeatloaf::net_get_ssid_raw()
{
    net_config_t net_config = net_get_ssid();
    responseV.assign(reinterpret_cast<const uint8_t*>(&net_config), reinterpret_cast<const uint8_t*>(&net_config) + sizeof(net_config));
}

net_config_t iecMeatloaf::net_get_ssid()
{
    net_config_t net_config;
    memset(&net_config, 0, sizeof(net_config));

    std::string s = Config.get_wifi_ssid();
    memcpy(net_config.ssid, s.c_str(), s.length() > sizeof(net_config.ssid) ? sizeof(net_config.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(net_config.password, s.c_str(), s.length() > sizeof(net_config.password) ? sizeof(net_config.password) : s.length());

    return net_config;
}

void iecMeatloaf::net_set_ssid_basic(bool store)
{
    if (pt.size() != 2)
    {
        Debug_printv("error: bad args");
        response = "BAD SSID ARGS";
        return;
    }

    // Strip off the SETSSID: part of the command
    pt[0] = pt[0].substr(8, std::string::npos);
    if (mstr::isNumeric(pt[0])) {
        // Find SSID by CRC8 Number
        pt[0] = fnWiFi.get_network_name_by_crc8(std::stoi(pt[0]));
    }

    // Debug_printv("pt1[%s] pt2[%s]", pt[1].c_str(), pt[2].c_str());

    // URL Decode SSID/PASSWORD. This will convert any %xx values to their equivalent char for the xx code. e.g. "%20" is a space
    // It does NOT replace '+' with a space as that's frankly just insane.
    // NOTE: if your password or username has a deliberate % followed by 2 digits, (e.g. the literal string "%69") you will have to type "%2569" to "escape" the percentage.
    // but that's the price you pay for using urlencoding without asking the user if they want it!
    std::string ssid = mstr::urlDecode(pt[0], false);
    std::string passphrase = mstr::urlDecode(pt[1], false);

    if (ssid.length() > MAX_SSID_LEN || passphrase.length() > MAX_PASSPHRASE_LEN || ssid.length() == 0 || passphrase.length() == 0) {
        response = "ERROR: BAD SSID/PASSPHRASE";
        return;
    }

    net_config_t net_config;
    memset(&net_config, 0, sizeof(net_config_t));

    strncpy(net_config.ssid, ssid.c_str(), ssid.length());
    strncpy(net_config.password, passphrase.c_str(), passphrase.length());
    Debug_printv("ssid[%s] pass[%s]", net_config.ssid, net_config.password);

    net_set_ssid(store, net_config);
}

void iecMeatloaf::net_set_ssid_raw(bool store)
{
    net_config_t net_config;
    memset(&net_config, 0, sizeof(net_config_t));

    // the data is coming to us packed not in struct format, so depack it into the NetConfig
    // Had issues with it not sending password after large number of \0 padding ssid.
    uint8_t sent_ssid_len = strlen(&payload[1]);
    uint8_t sent_pass_len = strlen(&payload[2 + sent_ssid_len]);
    strncpy((char *)&net_config.ssid[0], (const char *) &payload[1], sent_ssid_len);
    strncpy((char *)&net_config.password[0], (const char *) &payload[2 + sent_ssid_len], sent_pass_len);
    Debug_printv("ssid[%s] pass[%s]", net_config.ssid, net_config.password);

    net_set_ssid(store, net_config);
}

void iecMeatloaf::net_set_ssid(bool store, net_config_t& net_config)
{
    Debug_println("Fuji cmd: SET SSID");

    int test_result = fnWiFi.test_connect(net_config.ssid, net_config.password);
    if (test_result != 0)
    {
        Debug_println("Could not connect to target SSID. Aborting save.");
        iecStatus.msg = "ssid not set";
    } else {
        // Only save these if we're asked to, otherwise assume it was a test for connectivity
        if (store) {
            net_store_ssid(net_config.ssid, net_config.password);
        }
        iecStatus.msg = "ssid set";
    }

    Debug_println("Restarting WiFiManager");
    fnWiFi.start();

    // give it a few seconds to restart the WiFi before we return to the client, who will immediately start checking status if this is CONFIG
    // and get errors if we're not up yet.
    fnSystem.delay(4000);

    // what happens to iecStatus values?
    iecStatus.channel = 15;
    iecStatus.error = test_result == 0 ? NETWORK_ERROR_SUCCESS : NETWORK_ERROR_NOT_CONNECTED;
    iecStatus.connected = fnWiFi.connected();
}

void iecMeatloaf::net_store_ssid(std::string ssid, std::string password)
{
    // 1. if this is a new SSID and not in the old stored, we should push the current one to the top of the stored configs, and everything else down.
    // 2. If this was already in the stored configs, push the stored one to the top, remove the new one from stored so it becomes current only.
    // 3. if this is same as current, then just save it again. User reconnected to current, nothing to change in stored. This is default if above don't happen

    int ssid_in_stored = -1;

    for (int i = 0; i < MAX_WIFI_STORED; i++)
    {
        std::string stored = Config.get_wifi_stored_ssid(i);
        std::string ithConfig = Config.get_wifi_stored_ssid(i);
        if (!ithConfig.empty() && ithConfig == ssid)
        {
            ssid_in_stored = i;
            break;
        }
    }

    // case 1
    if (ssid_in_stored == -1 && Config.have_wifi_info() && Config.get_wifi_ssid() != ssid) {
        Debug_println("Case 1: Didn't find new ssid in stored, and it's new. Pushing everything down 1 and old current to 0");
        // Move enabled stored down one, last one will drop off
        for (int j = MAX_WIFI_STORED - 1; j > 0; j--)
        {
            bool enabled = Config.get_wifi_stored_enabled(j - 1);
            if (!enabled) continue;

            Config.store_wifi_stored_ssid(j, Config.get_wifi_stored_ssid(j - 1));
            Config.store_wifi_stored_passphrase(j, Config.get_wifi_stored_passphrase(j - 1));
            Config.store_wifi_stored_enabled(j, true); // already confirmed this is enabled
        }
        // push the current to the top of stored
        Config.store_wifi_stored_ssid(0, Config.get_wifi_ssid());
        Config.store_wifi_stored_passphrase(0, Config.get_wifi_passphrase());
        Config.store_wifi_stored_enabled(0, true);
    }

    // case 2
    if (ssid_in_stored != -1 && Config.have_wifi_info() && Config.get_wifi_ssid() != ssid) {
        Debug_printf("Case 2: Found new ssid in stored at %d, and it's not current (should never happen). Pushing everything down 1 and old current to 0\r\n", ssid_in_stored);
        // found the new SSID at ssid_in_stored, so move everything above it down one slot, and store the current at 0
        for (int j = ssid_in_stored; j > 0; j--)
        {
            Config.store_wifi_stored_ssid(j, Config.get_wifi_stored_ssid(j - 1));
            Config.store_wifi_stored_passphrase(j, Config.get_wifi_stored_passphrase(j - 1));
            Config.store_wifi_stored_enabled(j, true);
        }

        // push the current to the top of stored
        Config.store_wifi_stored_ssid(0, Config.get_wifi_ssid());
        Config.store_wifi_stored_passphrase(0, Config.get_wifi_passphrase());
        Config.store_wifi_stored_enabled(0, true);
    }

    // save the new SSID as current
    Config.store_wifi_ssid(ssid.c_str(), ssid.size());
    // Clear text here, it will be encrypted internally if enabled for encryption
    Config.store_wifi_passphrase(password.c_str(), password.size());

    Config.save();
}

void iecMeatloaf::net_get_wifi_status_raw()
{
    responseV.push_back(net_get_wifi_status());    
}

void iecMeatloaf::net_get_wifi_status_basic()
{
    response = net_get_wifi_status() == 3 ? "connected" : "disconnected";
}


uint8_t iecMeatloaf::net_get_wifi_status()
{
    return fnWiFi.connected() ? 3 : 6;
}

void iecMeatloaf::net_get_wifi_enabled()
{
    // Not needed, will remove.
}

// void iecMeatloaf::unmount_host()
// {
//     int hs = -1;
//
//     if (payload[0] == FUJICMD_UNMOUNT_HOST)
//     {
//         hs = payload[1];
//     }
//     else
//     {
//
//         if (pt.size() < 2) // send error.
//         {
//             response = "invalid # of parameters";
//             return;
//         }
//
//         hs = atoi(pt[1].c_str());
//     }
//
//     if (!_validate_device_slot(hs, "unmount_host"))
//     {
//         response = "invalid device slot";
//         return; // send error.
//     }
//
//     if (!_fnHosts[hs].umount())
//     {
//         response = "unable to unmount host slot";
//         return; // send error;
//     }
//
//     response="ok";
// }
//
// // Mount Server
// void iecMeatloaf::mount_host()
// {
//     int hs = -1;
//
//     _populate_slots_from_config();
//     if (payload[0] == FUJICMD_MOUNT_HOST)
//         {
//         hs = payload[1];
//         }
//         else
//         {
//
//
//         if (pt.size() < 2) // send error.
//         {
//             response = "INVALID # OF PARAMETERS.";
//             return;
//         }
//
//         hs = atoi(pt[1].c_str());
//         }
//
//     if (!_validate_device_slot(hs, "mount_host"))
//     {
//         response = "INVALID HOST HOST #";
//         return; // send error.
//     }
//
//     if (!_fnHosts[hs].mount())
//     {
//         response = "UNABLE TO MOUNT HOST SLOT #";
//         return; // send error.
//     }
//
//     // Otherwise, mount was successful.
//     char hn[64];
//     string hns;
//     _fnHosts[hs].get_hostname(hn, 64);
//     hns = string(hn);
//     hns = mstr::toPETSCII2(hns);
//     response = hns + " MOUNTED.";
// }
//
// // Disk Image Mount
// void iecMeatloaf::disk_image_mount()
// {
//     _populate_slots_from_config();
//
//     if (pt.size() < 3)
//     {
//         response = "invalid # of parameters";
//         return;
//     }
//
//     uint8_t ds = atoi(pt[1].c_str());
//     uint8_t mode = atoi(pt[2].c_str());
//
//     char flag[3] = {'r', 0, 0};
//
//     if (mode == DISK_ACCESS_MODE_WRITE)
//         flag[1] = '+';
//
//     if (!_validate_device_slot(ds))
//     {
//         response = "invalid device slot.";
//         return; // error.
//     }
//
//     // A couple of reference variables to make things much easier to read...
//     fujiDisk &disk = _fnDisks[ds];
//     fujiHost &host = _fnHosts[disk.host_slot];
//
//     Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
//                  disk.filename, disk.host_slot, flag, ds + 1);
//
//     // TODO: Refactor along with mount disk image.
//     disk.disk_dev.host = &host;
//
//     disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);
//
//     if (disk.fileh == nullptr)
//     {
//         response = "no file handle";
//         return;
//     }
//
//     // We've gotten this far, so make sure our bootable CONFIG disk is disabled
//     boot_config = false;
//
//     // We need the file size for loading XEX files and for CASSETTE, so get that too
//     disk.disk_size = host.file_size(disk.fileh);
//
//     // And now mount it
//     disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
//     response = "mounted";
// }
//
// // Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
// void iecMeatloaf::set_boot_config()
// {
//     if (payload[0] == FUJICMD_CONFIG_BOOT)
//     {
//         boot_config = payload[1];
//     }
//     else
//     {
//
//
//         if (pt.size() < 2)
//         {
//             Debug_printf("Invalid # of parameters.\r\n");
//             response = "invalid # of parameters";
//             return;
//         }
//
//         boot_config = atoi(pt[1].c_str());
//     }
//     response = "ok";
// }
//
// // Do SIO copy
// void iecMeatloaf::copy_file()
// {
//     // TODO IMPLEMENT
// }
//
// // Mount all
// void iecMeatloaf::mount_all()
// {
//     bool nodisks = true; // Check at the end if no disks are in a slot and disable config
//
//     for (int i = 0; i < MAX_DISK_DEVICES; i++)
//     {
//         fujiDisk &disk = _fnDisks[i];
//         fujiHost &host = _fnHosts[disk.host_slot];
//         char flag[3] = {'r', 0, 0};
//
//         if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
//             flag[1] = '+';
//
//         if (disk.host_slot != INVALID_HOST_SLOT)
//         {
//             nodisks = false; // We have a disk in a slot
//
//             if (host.mount() == false)
//             {
//                 // Send error.
//                 char slotno[3];
//                 itoa(i, slotno, 10);
//                 response = "error: unable to mount slot " + std::string(slotno) + "\r";
//                 return;
//             }
//
//             Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
//                          disk.filename, disk.host_slot, flag, i + 1);
//
//             disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);
//
//             if (disk.fileh == nullptr)
//             {
//                 // Send error.
//                 char slotno[3];
//                 itoa(i, slotno, 10);
//                 response = "error: invalid file handle for slot " + std::string(slotno) + "\r";
//                 return;
//             }
//
//             // We've gotten this far, so make sure our bootable CONFIG disk is disabled
//             boot_config = false;
//
//             // We need the file size for loading XEX files and for CASSETTE, so get that too
//             disk.disk_size = host.file_size(disk.fileh);
//
//             // Set the host slot for high score mode
//             // TODO: Refactor along with mount disk image.
//             disk.disk_dev.host = &host;
//
//             // And now mount it
//             disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
//         }
//     }
//
//     if (nodisks)
//     {
//         // No disks in a slot, disable config
//         boot_config = false;
//     }
//
//     // Send successful.
//     response = "ok";
// }
//
// // Set boot mode
// void iecMeatloaf::set_boot_mode()
// {
//     if (payload[0] == FUJICMD_CONFIG_BOOT)
//     {
//         boot_config = payload[1];
//     }
//     else
//     {
//         if (pt.size() < 2)
//         {
//             Debug_printf("Invalid # of parameters.\r\n");
//             // send error
//             response = "invalid # of parameters";
//             return;
//         }
//
//         boot_config = true;
//         insert_boot_device(atoi(pt[1].c_str()));
//     }
//     response = "ok";
// }

char *_generate_appkey_filename(appkey *info)
{
    static char filenamebuf[30];

    snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key", info->creator, info->app, info->key);
    return filenamebuf;
}

/*
 Opens an "app key".  This just sets the needed app key parameters (creator, app, key, mode)
 for the subsequent expected read/write command. We could've added this information as part
 of the payload in a WRITE_APPKEY command, but there was no way to do this for READ_APPKEY.
 Requiring a separate OPEN command makes both the read and write commands behave similarly
 and leaves the possibity for a more robust/general file read/write function later.
*/
void iecMeatloaf::open_app_key()
{
    Debug_print("Fuji cmd: OPEN APPKEY\r\n");

    // The data expected for this command
    if (payload[0] == FUJICMD_OPEN_APPKEY)
        memcpy(&_current_appkey, &payload.c_str()[1], sizeof(_current_appkey));
    else
    {
        unsigned int val;

        if (pt.size() < 5)
        {
            Debug_printf("Incorrect number of parameters.\r\n");
            response = "invalid # of parameters";
            // send error.
        }

        sscanf(pt[1].c_str(), "%x", &val);
        _current_appkey.creator = (uint16_t)val;
        sscanf(pt[2].c_str(), "%x", &val);
        _current_appkey.app = (uint8_t)val;
        sscanf(pt[3].c_str(), "%x", &val);
        _current_appkey.key = (uint8_t)val;
        sscanf(pt[4].c_str(), "%x", &val);
        _current_appkey.mode = (appkey_mode)val;
        _current_appkey.reserved = 0;
    }

    // We're only supporting writing to SD, so return an error if there's no SD mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        // Send error
        response = "no sd card mounted";
        return;
    }

    // Basic check for valid data
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        // Send error.
        response = "invalid app key data";
        return;
    }

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, filename = \"%s\"\r\n",
                 _current_appkey.creator, _current_appkey.app, _current_appkey.key, _current_appkey.mode,
                 _generate_appkey_filename(&_current_appkey));

    // Send complete
    response = "ok";
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write operation.
*/
void iecMeatloaf::close_app_key()
{
    Debug_print("Fuji cmd: CLOSE APPKEY\r\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    response = "ok";
}

bool iecMeatloaf::check_appkey_creator(bool check_is_write)
{
    if (_current_appkey.creator == 0 || (check_is_write && _current_appkey.mode != APPKEYMODE_WRITE))
    {
        Debug_println("Invalid app key metadata - aborting");
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return false;
    }
    return true;
}

bool iecMeatloaf::check_sd_running()
{
    if (!fnSDFAT.running())
    {
        Debug_println("No SD mounted - can't write app key");
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return false;
    }
    return true;
}

void iecMeatloaf::write_app_key_raw()
{
    if (!check_appkey_creator(true) || !check_sd_running())
    {
        // no error messages are set (yet?) in raw. Maybe that is how to send error code back?
        return;
    }

    // payload[0] = 0xDE for write app key cmd
    // [1+] = data to write to key, size payload - 1
    // we can't write more than the appkey_size, which is set by the mode.
    // May have to change this later as per Eric's comments in discord.
    size_t write_size = payload.size() - 1;
    if (write_size > appkey_size)
    {
        Debug_printf("ERROR: key data sent was larger than keysize. Aborting rather than potentially corrupting existing data.");
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return;
    }

    std::vector<uint8_t> key_data(payload.begin() + 1, payload.end());
    Debug_printf("key_data: \r\n%s\r\n", util_hexdump(key_data.data(), key_data.size()).c_str());
    write_app_key(std::move(key_data));
}

void iecMeatloaf::write_app_key_basic()
{
    // In BASIC, the key length is specified in the parameters.
    if (pt.size() <= 2)
    {
        // send error
        response = "invalid # of parameters";
        return;
    }

    if (!check_appkey_creator(true))
    {
        Debug_println("Invalid app key metadata - aborting");
        response = "malformed appkey data.";
        return;
    }

    if (!check_sd_running())
    {
        Debug_println("No SD mounted - can't write app key");
        response = "no sd card mounted";
        return;
    }


    // Tokenize the raw PETSCII payload to save to the file
    std::vector<std::string> ptRaw;
    ptRaw = tokenize_basic_command(payloadRaw);

    // do we need keylen anymore?
    int keylen = atoi(pt[1].c_str());
    
    // Bounds check
    if (keylen > MAX_APPKEY_LEN)
        keylen = MAX_APPKEY_LEN;

    std::vector<uint8_t> key_data(ptRaw[2].begin(), ptRaw[2].end());
    write_app_key(std::move(key_data));
}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void iecMeatloaf::write_app_key(std::vector<uint8_t>&& value)
{
    char *filename = _generate_appkey_filename(&_current_appkey);

    // Reset the app key data so we require calling APPKEY OPEN before another attempt
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;

    Debug_printf("Writing appkey to \"%s\"\r\n", filename);

    // Make sure we have a "/.app" directory, since that's where we're putting these files
    fnSDFAT.create_path("/.app");

    FILE *fOut = fnSDFAT.file_open(filename, FILE_WRITE);
    if (fOut == nullptr)
    {
        Debug_printf("Failed to open/create output file: errno=%d\n", errno);
        return;
    }
    size_t count = fwrite(value.data(), 1, value.size(), fOut);
    int e = errno;
    fclose(fOut);

    if (count != value.size())
    {
        if (!is_raw_command)
        {
            std::ostringstream oss;
            oss << "error: only wrote " << count << " bytes of expected " << value.size() << ", errno=" << e << "\r\n";
            response = oss.str();
        }
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return;
    }

    if (!is_raw_command)
    {
        response = "ok";
    }

}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void iecMeatloaf::read_app_key()
{
    Debug_println("Fuji cmd: READ APPKEY");

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        response = "no sd mounted";
        // Send error
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return;
    }

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_READ)
    {
        Debug_println("Invalid app key metadata - aborting");
        response = "invalid appkey metadata";
        // Send error
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return;
    }

    char *filename = _generate_appkey_filename(&_current_appkey);
    Debug_printf("Reading appkey from \"%s\"\r\n", filename);

    FILE *fIn = fnSDFAT.file_open(filename, "r");
    if (fIn == nullptr)
    {
        std::ostringstream oss;
        oss << "Failed to open input file: errno=" << errno << "\r\n";
        response = oss.str();
        state = DEVICE_ERROR;
        IEC.senderTimeout();
        return;
    }

    std::vector<uint8_t> response_data(appkey_size);
    size_t count = fread(response_data.data(), 1, response_data.size(), fIn);
    response_data.resize(count);
    Debug_printf("Read %u bytes from input file\n", (unsigned)count);
    fclose(fIn);

#ifdef DEBUG
	Debug_printf("response raw:\r\n%s\n", util_hexdump(response_data.data(), count).c_str());
#endif

    if (is_raw_command)
    {
        responseV = std::move(response_data);
    }
    else {
        response.assign(response_data.begin(), response_data.end());
    }

}

// // Disk Image Unmount
// void iecMeatloaf::disk_image_umount()
// {
//     uint8_t deviceSlot = -1;
//
//     if (payload[0] == FUJICMD_UNMOUNT_IMAGE)
//     {
//         deviceSlot = payload[1];
//     }
//     else
//     {
//
//         deviceSlot = atoi(pt[1].c_str());
//     }
//
//     Debug_printf("Fuji cmd: UNMOUNT IMAGE 0x%02X\n", deviceSlot);
//
//     // Handle disk slots
//     if (deviceSlot < MAX_DISK_DEVICES)
//     {
//         _fnDisks[deviceSlot].disk_dev.unmount();
//         _fnDisks[deviceSlot].disk_dev.device_active = false;
//         _fnDisks[deviceSlot].reset();
//     }
//     else
//     {
//         // Send error
//         response = "invalid device slot";
//         return;
//     }
//     response = "ok";
// }

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void iecMeatloaf::image_rotate()
{
    // TODO IMPLEMENT
}

// This gets called when we're about to shutdown/reboot
void iecMeatloaf::shutdown()
{
    // TODO IMPLEMENT
}

std::pair<std::string, std::string> split_at_delim(const std::string& input, char delim) {
    // Find the position of the first occurrence of delim in the string
    size_t pos = input.find(delim);

    std::string firstPart, secondPart;
    if (pos != std::string::npos) {
        firstPart = input.substr(0, pos);
        // Check if there's content beyond the delim for the second part
        if (pos + 1 < input.size()) {
            secondPart = input.substr(pos + 1);
        }
    } else {
        // If delim is not found, the entire input is the first part
        firstPart = input;
    }

    // Remove trailing slash from firstPart, if present
    if (!firstPart.empty() && firstPart.back() == '/') {
        firstPart.pop_back();
    }

    return {firstPart, secondPart};
}

// void iecMeatloaf::open_directory()
// {
//     Debug_println("Fuji cmd: OPEN DIRECTORY");

    

//     if (pt.size() < 3)
//     {
//         response = "invalid # of parameters.";
//         return; // send error
//     }

//     char dirpath[256];
//     uint8_t hostSlot = atoi(pt[1].c_str());
//     strncpy(dirpath, pt[2].c_str(), sizeof(dirpath));

//     if (!_validate_host_slot(hostSlot))
//     {
//         // send error
//         response = "invalid host slot #";
//         return;
//     }

//     // If we already have a directory open, close it first
//     if (_current_open_directory_slot != -1)
//     {
//         Debug_print("Directory was already open - closign it first\n");
//         _fnHosts[_current_open_directory_slot].dir_close();
//         _current_open_directory_slot = -1;
//     }

//     // Preprocess ~ (pi!) into filter
//     for (int i = 0; i < sizeof(dirpath); i++)
//     {
//         if (dirpath[i] == '~')
//             dirpath[i] = 0; // turn into null
//     }

//     // See if there's a search pattern after the directory path
//     const char *pattern = nullptr;
//     int pathlen = strnlen(dirpath, sizeof(dirpath));
//     if (pathlen < sizeof(dirpath) - 3) // Allow for two NULLs and a 1-char pattern
//     {
//         pattern = dirpath + pathlen + 1;
//         int patternlen = strnlen(pattern, sizeof(dirpath) - pathlen - 1);
//         if (patternlen < 1)
//             pattern = nullptr;
//     }

//     // Remove trailing slash
//     if (pathlen > 1 && dirpath[pathlen - 1] == '/')
//         dirpath[pathlen - 1] = '\0';

//     Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath, pattern ? pattern : "");

//     if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
//     {
//         _current_open_directory_slot = hostSlot;
//     }
//     else
//     {
//         // send error
//         response = "unable to open directory";
//     }

//     response = "ok";
// }

// void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
// {
//     // File modified date-time
//     struct tm *modtime = localtime(&f->modified_time);
//     modtime->tm_mon++;
//     modtime->tm_year -= 70;

//     dest[0] = modtime->tm_year;
//     dest[1] = modtime->tm_mon;
//     dest[2] = modtime->tm_mday;
//     dest[3] = modtime->tm_hour;
//     dest[4] = modtime->tm_min;
//     dest[5] = modtime->tm_sec;

//     // File size
//     uint16_t fsize = f->size;
//     dest[6] = LOBYTE_FROM_UINT16(fsize);
//     dest[7] = HIBYTE_FROM_UINT16(fsize);

//     // File flags
// #define FF_DIR 0x01
// #define FF_TRUNC 0x02

//     dest[8] = f->isDir ? FF_DIR : 0;

//     maxlen -= 10; // Adjust the max return value with the number of additional bytes we're copying
//     if (f->isDir) // Also subtract a byte for a terminating slash on directories
//         maxlen--;
//     if (strlen(f->filename) >= maxlen)
//         dest[8] |= FF_TRUNC;

//     // File type
//     dest[9] = MediaType::discover_mediatype(f->filename);
// }

// void iecMeatloaf::read_directory_entry()
// {
    

//     if (pt.size() < 2)
//     {
//         // send error
//         response = "invalid # of parameters";
//         return;
//     }

//     uint8_t maxlen = atoi(pt[1].c_str());
//     uint8_t addtlopts = atoi(pt[2].c_str());

//     Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

//     // Make sure we have a current open directory
//     if (_current_open_directory_slot == -1)
//     {
//         // Return error.
//         response = "no currently open directory";
//         Debug_print("No currently open directory\n");
//         return;
//     }

//     char current_entry[256];

//     fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();

//     if (f == nullptr)
//     {
//         Debug_println("Reached end of of directory");
//         current_entry[0] = 0x7F;
//         current_entry[1] = 0x7F;
//     }
//     else
//     {
//         Debug_printf("::read_direntry \"%s\"\n", f->filename);

//         int bufsize = sizeof(current_entry);
//         char *filenamedest = current_entry;

// #define ADDITIONAL_DETAILS_BYTES 10
//         // If 0x80 is set on AUX2, send back additional information
//         if (addtlopts & 0x80)
//         {
//             _set_additional_direntry_details(f, (uint8_t *)current_entry, maxlen);
//             // Adjust remaining size of buffer and file path destination
//             bufsize = sizeof(current_entry) - ADDITIONAL_DETAILS_BYTES;
//             filenamedest = current_entry + ADDITIONAL_DETAILS_BYTES;
//         }
//         else
//         {
//             bufsize = maxlen;
//         }

//         // int filelen = strlcpy(filenamedest, f->filename, bufsize);
//         int filelen = util_ellipsize(f->filename, filenamedest, bufsize);

//         // Add a slash at the end of directory entries
//         if (f->isDir && filelen < (bufsize - 2))
//         {
//             current_entry[filelen] = '/';
//             current_entry[filelen + 1] = '\0';
//         }
//     }

//     // Output RAW vs non-raw
//     if (payload[0] == FUJICMD_READ_DIR_ENTRY)
//         response = std::string((const char *)current_entry, maxlen);
//     else
//     {
//         char reply[258];
//         memset(reply, 0, sizeof(reply));
//         sprintf(reply, "%s", current_entry);
//         std::string s(reply);
//         s = mstr::toPETSCII2(s);
//         response = s;
//     }
// }

// void iecMeatloaf::get_directory_position()
// {
//     Debug_println("Fuji cmd: GET DIRECTORY POSITION");

//     // Make sure we have a current open directory
//     if (_current_open_directory_slot == -1)
//     {
//         Debug_print("No currently open directory\n");
//         response = "no currently open directory";
//         // Send error
//         return;
//     }

//     uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();
//     if (pos == FNFS_INVALID_DIRPOS)
//     {
//         Debug_println("Invalid directory position");
//         // Send error.
//         response = "invalid directory position";
//         return;
//     }

//     // Return the value we read

//     if (payload[0] == FUJICMD_GET_DIRECTORY_POSITION)
//         response = std::string((const char *)&pos, sizeof(pos));
//     else
//     {
//         char reply[8];
//         itoa(pos, reply, 10);
//         response = std::string(reply);
//     }
// }

// void iecMeatloaf::set_directory_position()
// {
//     Debug_println("Fuji cmd: SET DIRECTORY POSITION");

//     uint16_t pos = 0;

//     if (payload[0] == FUJICMD_SET_DIRECTORY_POSITION)
//     {
//         pos = payload[1] & 0xFF;
//         pos |= payload[2] << 8;
//     }
//     else
//     {
        
//         if (pt.size() < 2)
//         {
//             Debug_println("Invalid directory position");
//             response = "error: invalid directory position\r";
//             return;
//         }

//         pos = atoi(pt[1].c_str());
//     }

//     // Make sure we have a current open directory
//     if (_current_open_directory_slot == -1)
//     {
//         Debug_print("No currently open directory\n");
//         // Send error
//         response = "error: no currently open directory";
//         return;
//     }

//     bool result = _fnHosts[_current_open_directory_slot].dir_seek(pos);
//     if (result == false)
//     {
//         // Send error
//         response = "error: unable to perform directory seek\r";
//         return;
//     }
//     response = "ok\r";
// }

// void iecMeatloaf::close_directory()
// {
//     Debug_println("Fuji cmd: CLOSE DIRECTORY");

//     if (_current_open_directory_slot != -1)
//         _fnHosts[_current_open_directory_slot].dir_close();

//     _current_open_directory_slot = -1;
//     response = "ok";
// }

// Get network adapter configuration
void iecMeatloaf::get_adapter_config()
{
    Debug_printf("get_adapter_config()\r\n");

    memset(&cfg, 0, sizeof(cfg));

    strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

    if (!fnWiFi.connected())
    {
        strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
        strlcpy(cfg.hostname, "NOT CONNECTED", sizeof(cfg.hostname));
    }
    else
    {
        strlcpy(cfg.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(cfg.hostname));
        strlcpy(cfg.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(cfg.ssid));
        fnWiFi.get_current_bssid(cfg.bssid);
        fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);
        fnSystem.Net.get_ip4_dns_info(cfg.dnsIP);
    }

    fnWiFi.get_mac(cfg.macAddress);

    if (payload[0] == FUJICMD_GET_ADAPTERCONFIG)
    {
        responseV.assign(reinterpret_cast<const uint8_t*>(&cfg), reinterpret_cast<const uint8_t*>(&cfg) + sizeof(AdapterConfig));
    }
    else if (payload == "ADAPTERCONFIG")
    {
        response = "use localip netmask gateway dnsip bssid hostname version";
        return;
    }
}

//  Make new disk and shove into device slot
void iecMeatloaf::new_disk()
{
    // TODO: Implement when we actually have a good idea of
    // media types.
}

// // Send host slot data to computer
// void iecMeatloaf::read_host_slots()
// {
//     Debug_println("Fuji cmd: READ HOST SLOTS");

//     char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
//     memset(hostSlots, 0, sizeof(hostSlots));

//     for (int i = 0; i < MAX_HOSTS; i++)
//         strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

//     if (payload[0] == FUJICMD_READ_HOST_SLOTS)
//         response = std::string((const char *)hostSlots, 256);
//     else
//     {
        

//         if (pt.size() < 2)
//         {
//             response = "host slot # required";
//             return;
//         }

//         util_remove_spaces(pt[1]);

//         int selected_hs = atoi(pt[1].c_str());
//         std::string hn = std::string(hostSlots[selected_hs]);

//         if (hn.empty())
//         {
//             response = "<empty>";
//         }
//         else
//             response = std::string(hostSlots[selected_hs]);

//         response = mstr::toPETSCII2(response);
//     }
// }

// // Read and save host slot data from computer
// void iecMeatloaf::write_host_slots()
// {
//     int hostSlot = -1;
//     std::string hostname;

//     Debug_println("FUJI CMD: WRITE HOST SLOTS");

//     // RAW command
//     if (payload[0] == FUJICMD_WRITE_HOST_SLOTS)
//     {
//         union _hostSlots
//         {
//             char hostSlots[8][32];
//             char rawdata[256];
//         } hostSlots;

//         strncpy(hostSlots.rawdata, &payload.c_str()[1], sizeof(hostSlots.rawdata));

//         for (int i = 0; i < MAX_HOSTS; i++)
//         {
//             _fnHosts[i].set_hostname(hostSlots.hostSlots[i]);

//             _populate_config_from_slots();
//             Config.save();
//         }
//     }
//     else
//     {
//         // PUTHOST,<slot>,<hostname>
        

//         if (pt.size() < 2)
//         {
//             response = "no host slot #";
//             return;
//         }
//         else
//             hostSlot = atoi(pt[1].c_str());

//         if (!_validate_host_slot(hostSlot))
//         {
//             // Send error.
//             response = "invalid host slot #";
//             return;
//         }

//         if (pt.size() == 3)
//         {
//             hostname = pt[2];
//         }
//         else
//         {
//             hostname = std::string();
//         }

//         Debug_printf("Setting host slot %u to %s\n", hostSlot, hostname.c_str());
//         _fnHosts[hostSlot].set_hostname(hostname.c_str());
//     }

//     _populate_config_from_slots();
//     Config.save();

//     response = "ok";
// }

// // Store host path prefix
// void iecMeatloaf::set_host_prefix()
// {
//     // TODO IMPLEMENT
// }

// // Retrieve host path prefix
// void iecMeatloaf::get_host_prefix()
// {
//     // TODO IMPLEMENT
// }

// // Send device slot data to computer
// void iecMeatloaf::read_device_slots()
// {
//     Debug_println("Fuji cmd: READ DEVICE SLOTS");

//     struct disk_slot
//     {
//         uint8_t hostSlot;
//         uint8_t mode;
//         char filename[MAX_DISPLAY_FILENAME_LEN];
//     };

//     disk_slot diskSlots[MAX_DISK_DEVICES];

//     int returnsize;
//     char *filename;

//     // Load the data from our current device array
//     for (int i = 0; i < MAX_DISK_DEVICES; i++)
//     {
//         diskSlots[i].mode = _fnDisks[i].access_mode;
//         diskSlots[i].hostSlot = _fnDisks[i].host_slot;
//         if (_fnDisks[i].filename[0] == '\0')
//         {
//             strlcpy(diskSlots[i].filename, "", MAX_DISPLAY_FILENAME_LEN);
//         }
//         else
//         {
//             // Just use the basename of the image, no path. The full path+filename is
//             // usually too long for the Atari to show anyway, so the image name is more important.
//             // Note: Basename can modify the input, so use a copy of the filename
//             filename = strdup(_fnDisks[i].filename);
//             strlcpy(diskSlots[i].filename, basename(filename), MAX_DISPLAY_FILENAME_LEN);
//             free(filename);
//         }
//     }

//     returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;

//     if (payload[0] == FUJICMD_READ_DEVICE_SLOTS)
//         status_override = std::string((const char *)&diskSlots, returnsize);
//     else
//     {
        

//         if (pt.size() < 2)
//         {
//             response = "host slot required";
//             return;
//         }

//         util_remove_spaces(pt[1]);

//         int selected_ds = atoi(pt[1].c_str());

//         response = diskSlots[selected_ds].filename;
//     }
// }

// // Read and save disk slot data from computer
// void iecMeatloaf::write_device_slots()
// {
//     Debug_println("Fuji cmd: WRITE DEVICE SLOTS");

//     if (payload[0] == FUJICMD_WRITE_DEVICE_SLOTS)
//     {
//         union _diskSlots
//         {
//             struct
//             {
//                 uint8_t hostSlot;
//                 uint8_t mode;
//                 char filename[MAX_DISPLAY_FILENAME_LEN];
//             } diskSlots[MAX_DISK_DEVICES];
//             char rawData[152];
//         } diskSlots;

//         strncpy(diskSlots.rawData, &payload.c_str()[1], 152);

//         // Load the data into our current device array
//         for (int i = 0; i < MAX_DISK_DEVICES; i++)
//             _fnDisks[i].reset(diskSlots.diskSlots[i].filename, diskSlots.diskSlots[i].hostSlot, diskSlots.diskSlots[i].mode);
//     }
//     else
//     {
//         // from BASIC
        

//         if (pt.size() < 4)
//         {
//             response = "need file mode";
//             return;
//         }
//         else if (pt.size() < 3)
//         {
//             response = "need filename";
//             return;
//         }
//         else if (pt.size() < 2)
//         {
//             response = "need host slot";
//             return;
//         }
//         else if (pt.size() < 1)
//         {
//             response = "need device slot";
//             return;
//         }

//         unsigned char ds = atoi(pt[1].c_str());
//         unsigned char hs = atoi(pt[2].c_str());
//         string filename = pt[3];
//         unsigned char m = atoi(pt[4].c_str());

//         _fnDisks[ds].reset(filename.c_str(),hs,m);
//         strncpy(_fnDisks[ds].filename,filename.c_str(),256);
//     }

//     // Save the data to disk
//     _populate_config_from_slots();
//     Config.save();
// }

// // Temporary(?) function while we move from old config storage to new
// void iecMeatloaf::_populate_slots_from_config()
// {
//     Debug_printf("_populate_slots_from_config()\n");
//     for (int i = 0; i < MAX_HOSTS; i++)
//     {
//         if (Config.get_host_type(i) == fnConfig::host_types::HOSTTYPE_INVALID)
//             _fnHosts[i].set_hostname("");
//         else
//             _fnHosts[i].set_hostname(Config.get_host_name(i).c_str());
//     }

//     for (int i = 0; i < MAX_DISK_DEVICES; i++)
//     {
//         _fnDisks[i].reset();

//         if (Config.get_mount_host_slot(i) != HOST_SLOT_INVALID)
//         {
//             if (Config.get_mount_host_slot(i) >= 0 && Config.get_mount_host_slot(i) <= MAX_HOSTS)
//             {
//                 strlcpy(_fnDisks[i].filename,
//                         Config.get_mount_path(i).c_str(), sizeof(fujiDisk::filename));
//                 _fnDisks[i].host_slot = Config.get_mount_host_slot(i);
//                 if (Config.get_mount_mode(i) == fnConfig::mount_modes::MOUNTMODE_WRITE)
//                     _fnDisks[i].access_mode = DISK_ACCESS_MODE_WRITE;
//                 else
//                     _fnDisks[i].access_mode = DISK_ACCESS_MODE_READ;
//             }
//         }
//     }
// }

// // Temporary(?) function while we move from old config storage to new
// void iecMeatloaf::_populate_config_from_slots()
// {
//     for (int i = 0; i < MAX_HOSTS; i++)
//     {
//         fujiHostType htype = _fnHosts[i].get_type();
//         const char *hname = _fnHosts[i].get_hostname();

//         if (hname[0] == '\0')
//         {
//             Config.clear_host(i);
//         }
//         else
//         {
//             Config.store_host(i, hname,
//                               htype == HOSTTYPE_TNFS ? fnConfig::host_types::HOSTTYPE_TNFS : fnConfig::host_types::HOSTTYPE_SD);
//         }
//     }

//     for (int i = 0; i < MAX_DISK_DEVICES; i++)
//     {
//         if (_fnDisks[i].host_slot >= MAX_HOSTS || _fnDisks[i].filename[0] == '\0')
//             Config.clear_mount(i);
//         else
//             Config.store_mount(i, _fnDisks[i].host_slot, _fnDisks[i].filename,
//                                _fnDisks[i].access_mode == DISK_ACCESS_MODE_WRITE ? fnConfig::mount_modes::MOUNTMODE_WRITE : fnConfig::mount_modes::MOUNTMODE_READ);
//     }
// }

// // Write a 256 byte filename to the device slot
// void iecMeatloaf::set_device_filename()
// {
//     char tmp[MAX_FILENAME_LEN];

//     uint8_t slot = 0;
//     uint8_t host = 0;
//     uint8_t mode = 0;

//     if (payload[0] == FUJICMD_SET_DEVICE_FULLPATH)
//     {
//         slot = payload[1];
//         host = payload[2];
//         mode = payload[3];
//         strncpy(tmp, &payload[4], 256);
//     }
//     else
//     {
        
//         if (pt.size() < 4)
//         {
//             Debug_printf("not enough parameters.\n");
//             response = "error: invalid # of parameters";
//             return; // send error
//         }
//     }

//     Debug_printf("Fuji cmd: SET DEVICE SLOT 0x%02X/%02X/%02X FILENAME: %s\n", slot, host, mode, tmp);

//     // Handle DISK slots
//     if (slot < MAX_DISK_DEVICES)
//     {
//         // TODO: Set HOST and MODE
//         memcpy(_fnDisks[slot].filename, tmp, MAX_FILENAME_LEN);
//         _populate_config_from_slots();
//     }
//     else
//     {
//         Debug_println("BAD DEVICE SLOT");
//         // Send error
//         response = "error: invalid device slot\r";
//         return;
//     }

//     Config.save();
//     response = "ok";
// }

// // Get a 256 byte filename from device slot
// void iecMeatloaf::get_device_filename()
// {
//     Debug_println("Fuji CMD: get device filename");

//     uint8_t ds = 0xFF;

//     if (payload[0] == FUJICMD_GET_DEVICE_FULLPATH)
//         ds = payload[1];
//     else
//     {
        

//         if (pt.size() < 2)
//         {
//             Debug_printf("Incorrect # of parameters.\n");
//             response = "invalid # of parameters";
//             // Send error
//             return;
//          }

//         ds = atoi(pt[1].c_str());
//      }

//     if (!_validate_device_slot(ds, "get_device_filename"))
//     {
//         Debug_printf("Invalid device slot: %u\n", ds);
//         response = "invalid device slot";
//         // send error
//         return;
//     }

//     std::string reply = std::string(_fnDisks[ds].filename);
//     response = reply;
// }

// Mounts the desired boot disk number
void iecMeatloaf::insert_boot_device(uint8_t d)
{
    // TODO IMPLEMENT
}


iecDrive *iecMeatloaf::bootdisk()
{
    return &_bootDisk;
}

// int iecMeatloaf::get_disk_id(int drive_slot)
// {
//     return _fnDisks[drive_slot].disk_dev.id();
// }

// std::string iecMeatloaf::get_host_prefix(int host_slot)
// {
//     return _fnHosts[host_slot].get_prefix();
// }

/* @brief Tokenizes the payload command and parameters.
 Example: "COMMAND:Param1,Param2" will return a vector of [0]="COMMAND", [1]="Param1",[2]="Param2"
 Also supports "COMMAND,Param1,Param2"
*/
vector<string> iecMeatloaf::tokenize_basic_command(string command)
{
    Debug_printf("Tokenizing basic command: %s\n", command.c_str());

    // Replace the first ":" with "," for easy tokenization. 
    // Assume it is fine to change the payload at this point.
    // Technically, "COMMAND,Param1,Param2" will work the smae, if ":" is not in a param value
    size_t endOfCommand = command.find(':');
    if (endOfCommand != std::string::npos)
        command.replace(endOfCommand,1,",");
    
    vector<string> result =  util_tokenize(command, ',');
    return result;

}

#endif /* BUILD_IEC */