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

#include "device_db.h"

#include "../../include/global_defines.h"
#include "meat_io.h"
#include "meat_buffer.h"

DeviceDB device_config;

// DeviceDB::DeviceDB(uint8_t device)
// {
//     select(device);
//     m_dirty = false;

//     // Create .sys folder if it doesn't exist
//     std::unique_ptr<MFile> file(MFSOwner::File(SYSTEM_DIR));
//     if ( !file->exists() )
//     {
//         Debug_printv("Create '" SYSTEM_DIR "' folder!");
//         file->mkDir();
//     }
// } // constructor

// DeviceDB::~DeviceDB()
// {

// } // destructor


bool DeviceDB::select(uint8_t new_device_id)
{
    uint8_t device_id = m_device["id"];


#ifndef FLASH_SPIFFS
    // Create .sys folder if it doesn't exist
    std::unique_ptr<MFile> file(MFSOwner::File(SYSTEM_DIR));
    if ( !file->exists() )
    {
        Debug_printv("Create '" SYSTEM_DIR "' folder!");
        file->mkDir();
    }
#else
    std::unique_ptr<MFile> file(MFSOwner::File("/"));
#endif

    //Debug_printv("cur[%d] new[%d]", device_id, new_device_id);
    if ( device_id == new_device_id )
    {
        return false;
    }

    // Save current config
    save();

    config_file = SYSTEM_DIR "device." + std::to_string(new_device_id) + ".conf";
    //std::unique_ptr<MFile> file(MFSOwner::File(config_file));
    file.reset(MFSOwner::File(config_file));

    //Debug_printv("config_file[%s]", config_file.c_str());
    if ( file->exists() )
    {
        // Load Device Settings
        Meat::iostream istream(config_file);
        deserializeJson(m_device, istream);
        Debug_printv("loaded id[%d]", (uint8_t)m_device["id"]);
    }
    else
    {
        // Create New Settings
        deserializeJson(m_device, "{\"id\":0,\"url\":\"\",\"basepath\":\"\",\"path\":\"/\",\"archive\":\"\",\"image\":\"\"}");
        m_device["id"] = new_device_id;
        //Debug_printv("created id[%d]", (uint8_t)m_device["id"]);
    }

    return true;
}

bool DeviceDB::save()
{
    // Only save if dirty
    if ( m_dirty )
    {
        // Debug_printv("saved [%s]", config_file.c_str());
        // std::unique_ptr<MFile> file(MFSOwner::File(config_file));
        // if ( file->exists() )
        //     file->remove();

        // Meat::iostream ostream(file.get()->url);
        // ostream.open();
        // if(ostream.is_open())
        // {
        //     serializeJson(m_device, ostream);
        //     return true;
        // }
        // else
        // {
        //     return false;
        // }
        return true;
    }

    return false;
}

uint8_t DeviceDB::id()
{
    return m_device["id"];
}
void DeviceDB::id(uint8_t id)
{
    select(id);
}

std::string DeviceDB::url()
{
    if (m_device["url"] == NULL)
        m_device["url"] = "/";

    return m_device["url"];
}
void DeviceDB::url(std::string url)
{
    if (url != m_device["url"])
    {
        m_device["url"] = url;
        m_dirty = true;
    }
}
std::string DeviceDB::basepath()
{
    if (m_device["basepath"] == NULL)
        m_device["basepath"] = "";

    return m_device["basepath"];
}
void DeviceDB::basepath(std::string basepath)
{
    if (basepath != m_device["basepath"])
    {
        m_device["basepath"] = basepath;
        m_dirty = true;
    }
}
std::string DeviceDB::path()
{
    if (m_device["path"] == NULL)
        m_device["path"] = "/";

    return m_device["path"];
}
void DeviceDB::path(std::string path)
{
    if (path != m_device["path"])
    {
        m_device["path"] = path;
        m_dirty = true;
    }
}
std::string DeviceDB::archive()
{
    return m_device["archive"];
}
void DeviceDB::archive(std::string archive)
{
    if (archive != m_device["archive"])
    {
        m_device["archive"] = archive;
        m_dirty = true;
    }
}
std::string DeviceDB::image()
{
    return m_device["image"];
}
void DeviceDB::image(std::string image)
{
    if (image != m_device["image"])
    {
        m_device["image"] = image;
        m_dirty = true;
    }
}

