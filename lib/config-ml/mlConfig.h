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

#pragma once

#include <array>
#include <cstdint>
#include <json.hpp>

class FileSystem;

// MeatloafConfig — JSON-based replacement for fnConfig.
//
// Loads config.json and devices.json from SYSTEM_DIR on the SD card and merges
// them into a single in-memory nlohmann::json tree.  The two files are kept
// separate on disk because each is deliberately kept under 4 KB.
//
// On-disk split:
//   config.json  — everything except iec.devices
//   devices.json — { "iec": { "devices": { ... } } }
//
// Usage:
//   mlConfig.load();                       // call after fnSDFAT.start()
//   mlConfig.data()["general"]["devicename"] = "new name";
//   mlConfig.mark_config_dirty();
//   mlConfig.save();                       // only writes config.json

class MeatloafConfig {
public:
    // Load and merge both JSON files.  Returns true if config.json was read;
    // a missing devices.json is non-fatal (devices section stays empty).
    bool load();

    // Write only the file(s) whose data changed since the last load/save.
    // Dirty flags gate the comparison; only files that actually differ are written.
    void save();

    // Direct access to the merged JSON tree.
    // Mutate through data() then call mark_*_dirty() so save() knows to check.
    nlohmann::json&       data()       { return _data; }
    const nlohmann::json& data() const { return _data; }

    // Convenience: access common sections directly.
    // Returns a null JSON value if the key is absent (safe to read, not to write).
    const nlohmann::json& operator[](const char *key) const {
        static const nlohmann::json null_json;
        return _data.contains(key) ? _data.at(key) : null_json;
    }

    // Explicit dirty-flag setters.
    // Call after any mutation to the corresponding section so save() acts on it.
    void mark_config_dirty()  { _config_dirty  = true; }
    void mark_devices_dirty() { _devices_dirty = true; }

    // Query unsaved-change state.
    bool is_dirty()         const { return _config_dirty || _devices_dirty; }
    bool is_config_dirty()  const { return _config_dirty; }
    bool is_devices_dirty() const { return _devices_dirty; }

private:
    nlohmann::json _data;                        // merged in-memory state
    std::array<uint8_t, 16> _config_hash  = {}; // MD5 of config.json at last load/save
    std::array<uint8_t, 16> _devices_hash = {}; // MD5 of devices.json at last load/save

    bool _config_dirty  = false;
    bool _devices_dirty = false;

    // Return the portion of _data that belongs in config.json (no iec.devices).
    nlohmann::json _extract_config() const;
    // Return { "iec": { "devices": ... } } for devices.json.
    nlohmann::json _extract_devices() const;

    // Compute MD5 of the serialized JSON. Returns a 16-byte digest.
    static std::array<uint8_t, 16> _json_hash(const nlohmann::json &j);

    // Low-level I/O — both helpers operate on whichever FileSystem is passed.
    bool _read_json(const char *path, nlohmann::json &out, FileSystem &fs);
    bool _write_json(const char *path, const nlohmann::json &j, FileSystem &fs);
};

extern MeatloafConfig mlConfig;
