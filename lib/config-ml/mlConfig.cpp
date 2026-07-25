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

#include "mlConfig.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <unordered_map>

#include "mbedtls/md5.h"

#include "esp_log.h"
#include "fnFS.h"
#include "fnFsSD.h"
#include "fsFlash.h"
#include "global_defines.h"

#include "../../include/debug.h"
#include "../hardware/Esp.h"
#include "../www/ws/activity.h"

extern EspClass ESP;

static const char *TAG = "mlConfig";

// The two on-disk files, relative to the SD mount root.
#define CFG_FILE     SYSTEM_DIR "/config.json"
#define DEVICES_FILE SYSTEM_DIR "/devices.json"

// Global singleton
MeatloafConfig mlConfig;

// ─── Timezone ────────────────────────────────────────────────────────────────

const char *iana_to_posix_tz(const std::string &iana)
{
    static const std::unordered_map<std::string, const char *> table = {
        // North America
        {"America/New_York",               "EST5EDT,M3.2.0,M11.1.0/2"},
        {"America/Chicago",                "CST6CDT,M3.2.0,M11.1.0/2"},
        {"America/Denver",                 "MST7MDT,M3.2.0,M11.1.0/2"},
        {"America/Los_Angeles",            "PST8PDT,M3.2.0,M11.1.0/2"},
        {"America/Anchorage",              "AKST9AKDT,M3.2.0,M11.1.0/2"},
        {"America/Phoenix",                "MST7"},
        {"America/Honolulu",               "HST10"},
        {"Pacific/Honolulu",               "HST10"},
        {"America/Toronto",                "EST5EDT,M3.2.0,M11.1.0/2"},
        {"America/Vancouver",              "PST8PDT,M3.2.0,M11.1.0/2"},
        {"America/Edmonton",               "MST7MDT,M3.2.0,M11.1.0/2"},
        {"America/Winnipeg",               "CST6CDT,M3.2.0,M11.1.0/2"},
        {"America/Halifax",                "AST4ADT,M3.2.0,M11.1.0/2"},
        {"America/St_Johns",               "NST3:30NDT,M3.2.0,M11.1.0/2"},
        {"America/Mexico_City",            "CST6"},
        {"America/Sao_Paulo",              "<-03>3"},
        {"America/Argentina/Buenos_Aires", "<-03>3"},
        {"America/Bogota",                 "<-05>5"},
        {"America/Lima",                   "<-05>5"},
        {"America/Santiago",               "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
        // Europe
        {"Europe/London",                  "GMT0BST,M3.5.0/1,M10.5.0"},
        {"Europe/Dublin",                  "IST-1GMT0,M10.5.0,M3.5.0/1"},
        {"Europe/Paris",                   "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Berlin",                  "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Madrid",                  "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Rome",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Amsterdam",               "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Brussels",                "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Vienna",                  "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Zurich",                  "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Warsaw",                  "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Stockholm",               "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Oslo",                    "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Copenhagen",              "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Athens",                  "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Helsinki",                "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Bucharest",               "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Kiev",                    "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Moscow",                  "MSK-3"},
        {"Europe/Istanbul",                "<+03>-3"},
        {"Europe/Lisbon",                  "WET0WEST,M3.5.0/1,M10.5.0"},
        // Asia
        {"Asia/Tokyo",                     "JST-9"},
        {"Asia/Seoul",                     "KST-9"},
        {"Asia/Shanghai",                  "CST-8"},
        {"Asia/Hong_Kong",                 "HKT-8"},
        {"Asia/Singapore",                 "<+08>-8"},
        {"Asia/Taipei",                    "CST-8"},
        {"Asia/Kolkata",                   "IST-5:30"},
        {"Asia/Calcutta",                  "IST-5:30"},
        {"Asia/Dubai",                     "<+04>-4"},
        {"Asia/Bangkok",                   "<+07>-7"},
        {"Asia/Jakarta",                   "WIB-7"},
        {"Asia/Manila",                    "PST-8"},
        {"Asia/Jerusalem",                 "IST-2IDT,M3.4.4/26,M10.5.0"},
        {"Asia/Karachi",                   "PKT-5"},
        {"Asia/Dhaka",                     "<+06>-6"},
        // Australia / Pacific
        {"Australia/Sydney",               "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Australia/Melbourne",            "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Australia/Brisbane",             "AEST-10"},
        {"Australia/Perth",                "AWST-8"},
        {"Australia/Adelaide",             "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
        {"Pacific/Auckland",               "NZST-12NZDT,M9.5.0,M4.1.0/3"},
        {"Pacific/Fiji",                   "<+12>-12<+13>,M11.2.0,M1.3.3/76"},
        // Africa
        {"Africa/Cairo",                   "EET-2"},
        {"Africa/Johannesburg",            "SAST-2"},
        {"Africa/Lagos",                   "WAT-1"},
        {"Africa/Nairobi",                 "EAT-3"},
        // UTC
        {"UTC",                            "UTC0"},
        {"Etc/UTC",                        "UTC0"},
        {"GMT",                            "GMT0"},
    };

    auto it = table.find(iana);
    return (it != table.end()) ? it->second : nullptr;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

std::array<uint8_t, 16> MeatloafConfig::_json_hash(const psram_json &j)
{
    std::string s = j.dump();
    std::array<uint8_t, 16> digest = {};
    mbedtls_md5(reinterpret_cast<const unsigned char *>(s.data()), s.size(), digest.data());
    return digest;
}

bool MeatloafConfig::_read_json(const char *path, psram_json &out, FileSystem &fs)
{
    if (!fs.running()) {
        ESP_LOGW(TAG, "Filesystem not ready, skipping read of %s", path);
        return false;
    }

    FILE *f = fs.file_open(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s for reading", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        ESP_LOGW(TAG, "%s is empty", path);
        return false;
    }

    std::string buf(static_cast<size_t>(size), '\0');
    size_t got = fread(&buf[0], 1, static_cast<size_t>(size), f);
    fclose(f);

    if (got != static_cast<size_t>(size)) {
        ESP_LOGE(TAG, "Short read on %s (%zu of %ld bytes)", path, got, size);
        return false;
    }

    // allow_exceptions=false: returns a discarded value instead of throwing.
    out = psram_json::parse(buf, nullptr, false);
    if (out.is_discarded()) {
        ESP_LOGE(TAG, "JSON parse error in %s", path);
        return false;
    }
    return true;
}

bool MeatloafConfig::_write_json(const char *path, const psram_json &j, FileSystem &fs)
{
    if (!fs.running()) {
        ESP_LOGW(TAG, "Filesystem not ready, skipping write of %s", path);
        return false;
    }

    FILE *f = fs.file_open(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for writing", path);
        return false;
    }

    std::string out = j.dump(2);
    size_t written = fwrite(out.c_str(), 1, out.size(), f);
    fclose(f);

    if (written != out.size()) {
        ESP_LOGE(TAG, "Short write on %s (%zu of %zu bytes)", path, written, out.size());
        return false;
    }

    ESP_LOGI(TAG, "Saved %s (%zu bytes)", path, written);
    return true;
}

// ─── Extract helpers ─────────────────────────────────────────────────────────

// Returns _data with the "devices" key removed — this is what config.json stores.
psram_json MeatloafConfig::_extract_config() const
{
    psram_json j = _data;
    j.erase("devices");
    return j;
}

// Returns { "devices": { "iec": ..., "ps2": ..., ... } } — this is the entirety of devices.json.
psram_json MeatloafConfig::_extract_devices() const
{
    psram_json j = psram_json::object();
    if (_data.contains("devices")) {
        j["devices"] = _data["devices"];
    }
    return j;
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool MeatloafConfig::load()
{
    psram_json cfg;
    bool cfg_ok = _read_json(CFG_FILE, cfg, fnSDFAT);
    if (!cfg_ok) {
        ESP_LOGW(TAG, "config.json not on SD, trying flash fallback");
        cfg_ok = _read_json(CFG_FILE, cfg, fsFlash);
    }

    if (cfg_ok) {
        _data = cfg;
    } else {
        _data = psram_json::object();
        cfg   = psram_json::object();
    }
    _config_hash = _json_hash(cfg);

    // Merge devices.json into _data["devices"].
    psram_json dev;
    bool dev_ok = _read_json(DEVICES_FILE, dev, fnSDFAT);
    if (!dev_ok) {
        ESP_LOGW(TAG, "devices.json not on SD, trying flash fallback");
        dev_ok = _read_json(DEVICES_FILE, dev, fsFlash);
    }
    if (dev_ok) {
        if (dev.contains("devices")) {
            _data["devices"] = dev["devices"];
        }
        _devices_hash = _json_hash(_extract_devices());
    } else {
        _devices_hash = _json_hash(psram_json::object());
    }

    // Apply preferences.timezone (if set) so localtime()/strftime() reflect
    // it immediately. iana_to_posix_tz() maps the IANA zone name (e.g.
    // "America/New_York") to a POSIX TZ string, since newlib's tzset() has
    // no IANA tzdata database to resolve it from directly.
    std::string tz = _data.value("preferences", psram_json::object()).value("timezone", "");
    if (!tz.empty()) {
        const char *posix_tz = iana_to_posix_tz(tz);
        setenv("TZ", posix_tz ? posix_tz : tz.c_str(), 1);
        tzset();
    }

    // Keep firmware/hardware in sync with the running app version
    // (esp_app_desc_t.version, split at the last '.').
    std::string actual_firmware = ESP.getFirmwareVersion();
    std::string actual_hardware = ESP.getHardwareVersion();
    if (_data.value("firmware", "") != actual_firmware || _data.value("hardware", "") != actual_hardware) {
        _data["firmware"] = actual_firmware;
        _data["hardware"] = actual_hardware;
        save();
    }

    ESP_LOGI(TAG, "Config loaded (cfg=%s)", cfg_ok ? "ok" : "missing");
    return cfg_ok;
}

void MeatloafConfig::save()
{
    psram_json cfg      = _extract_config();
    auto       cfg_hash = _json_hash(cfg);
    psram_json dev      = _extract_devices();
    auto       dev_hash = _json_hash(dev);

    bool config_changed  = (cfg_hash != _config_hash);
    bool devices_changed = (dev_hash != _devices_hash);

    if (!config_changed && !devices_changed) {
        ESP_LOGD(TAG, "Config unchanged, skipping save");
        return;
    }

    FileSystem &fs = fnSDFAT.running() ? static_cast<FileSystem &>(fnSDFAT)
                                       : static_cast<FileSystem &>(fsFlash);

    if (fnSDFAT.running())
        fnSDFAT.create_path(SYSTEM_DIR);
    else
        fsFlash.create_path(SYSTEM_DIR);

    if (config_changed && _write_json(CFG_FILE, cfg, fs))
    {
        _config_hash = cfg_hash;
        Serial.println("Saved config.json");
        notify_activity("config", "save", "config.json");
    }
    if (devices_changed && _write_json(DEVICES_FILE, dev, fs))
    {
        _devices_hash = dev_hash;
        Serial.println("Saved devices.json");
        notify_activity("config", "save", "devices.json");
    }
}
