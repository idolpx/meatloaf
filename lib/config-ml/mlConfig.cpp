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
#include <string>

#include "mbedtls/md5.h"

#include "esp_log.h"
#include "fnFS.h"
#include "fnFsSD.h"
#include "fsFlash.h"
#include "global_defines.h"

static const char *TAG = "mlConfig";

// The two on-disk files, relative to the SD mount root.
#define CFG_FILE     SYSTEM_DIR "/config.json"
#define DEVICES_FILE SYSTEM_DIR "/devices.json"

// Global singleton
MeatloafConfig mlConfig;

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

    _config_dirty  = false;
    _devices_dirty = false;

    ESP_LOGI(TAG, "Config loaded (cfg=%s)", cfg_ok ? "ok" : "missing");
    return cfg_ok;
}

void MeatloafConfig::save()
{
    FileSystem &fs = fnSDFAT.running() ? static_cast<FileSystem &>(fnSDFAT)
                                       : static_cast<FileSystem &>(fsFlash);

    if (fnSDFAT.running())
        fnSDFAT.create_path(SYSTEM_DIR);
    else
        fsFlash.create_path(SYSTEM_DIR);

    if (_config_dirty) {
        auto current = _extract_config();
        auto h = _json_hash(current);
        if (h != _config_hash) {
            if (_write_json(CFG_FILE, current, fs)) {
                _config_hash = h;
            }
        } else {
            ESP_LOGD(TAG, "config.json unchanged, skipping write");
        }
        _config_dirty = false;
    }

    if (_devices_dirty) {
        auto current = _extract_devices();
        auto h = _json_hash(current);
        if (h != _devices_hash) {
            if (_write_json(DEVICES_FILE, current, fs)) {
                _devices_hash = h;
            }
        } else {
            ESP_LOGD(TAG, "devices.json unchanged, skipping write");
        }
        _devices_dirty = false;
    }
}
