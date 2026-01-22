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

// MQTT:// - MQTT Protocol for pub/sub operations
//

#ifndef MEATLOAF_SCHEME_MQTT
#define MEATLOAF_SCHEME_MQTT

#include "meatloaf.h"
#include "meat_session.h"

#include "mqtt_client.h"

#include "fnSystem.h"

#include "utils.h"
#include "string_utils.h"

#include <queue>
#include <mutex>
#include <condition_variable>

/********************************************************
 * MQTT Session
 ********************************************************/

class MQTTMSession : public MSession {
public:
    MQTTMSession(std::string host = "mqtt.eclipse.org", uint16_t port = 1883, std::string client_id = "");
    ~MQTTMSession() override;

    // MSession interface implementation
    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    // MQTT-specific methods
    bool publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    bool subscribe(const std::string& topic, int qos = 0);
    bool unsubscribe(const std::string& topic);
    
    // Message handling
    bool hasMessage();
    esp_mqtt_event_t* getMessage();
    void clearMessage();

    // Connection status
    bool isConnected() const override { return connected && mqtt_client != nullptr; }

private:
    esp_mqtt_client_handle_t mqtt_client = nullptr;
    std::string client_id;
    std::string username;
    std::string password;
    
    std::queue<esp_mqtt_event_t*> message_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    
    static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
    void handle_event(esp_mqtt_event_t *event);
    
    friend class MQTTMFile;
    friend class MQTTMStream;
};

/********************************************************
 * File implementations
 ********************************************************/

class MQTTMFile: public MFile {
public:
    MQTTMFile(std::string path);
    ~MQTTMFile() override;

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override { return src; };
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override { return false; };
    bool exists() override;
    bool remove() override { return false; }; // MQTT topics can't be "removed"
    bool rename(std::string dest) { return false; }; // Not applicable for MQTT

    // Accessor for session
    std::shared_ptr<MQTTMSession> getSession() { return _session; }

protected:
    std::shared_ptr<MQTTMSession> _session;
    std::string topic;

    friend class MQTTMStream;
};

/********************************************************
 * Streams
 ********************************************************/

class MQTTMStream: public MStream {
public:
    MQTTMStream(std::string path);
    ~MQTTMStream() override {
        close();
    }

    // MStream methods
    bool isOpen() override;
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return false; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    bool seek(uint32_t pos) override { return false; }; // Not applicable for MQTT streams

private:
    std::shared_ptr<MQTTMSession> session;
    std::string topic;
    bool is_subscribed = false;
    std::string read_buffer;
    size_t read_pos = 0;

    friend class MQTTMFile;
};

/********************************************************
 * FS
 ********************************************************/

class MQTTMFileSystem: public MFileSystem
{
public:
    MQTTMFileSystem(): MFileSystem("mqtt") {};

    bool handles(std::string name) {
        std::string pattern = "mqtt:";
        return mstr::startsWith(name, pattern.c_str(), false);
    }

    MFile* getFile(std::string path) override {
        return new MQTTMFile(path);
    }
};

#endif // MEATLOAF_SCHEME_MQTT