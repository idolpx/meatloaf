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

#include "mqtt.h"
#include "peoples_url_parser.h"
#include "debug.h"

#include <esp_log.h>
#include <cstring>

/********************************************************
 * MQTT Session Implementation
 ********************************************************/

MQTTMSession::MQTTMSession(std::string host, uint16_t port, std::string client_id)
    : MSession("mqtt://" + host + ":" + std::to_string(port), host, port), client_id(client_id)
{
    if (this->client_id.empty()) {
        // Generate a unique client ID
        this->client_id = "meatloaf_" + std::to_string(random());
    }
    
    Debug_printv("MQTTMSession created for %s:%d with client_id %s", host.c_str(), port, this->client_id.c_str());
}

MQTTMSession::~MQTTMSession() {
    disconnect();
    
    // Clear message queue
    std::unique_lock<std::mutex> lock(queue_mutex);
    while (!message_queue.empty()) {
        esp_mqtt_event_t* event = message_queue.front();
        message_queue.pop();
        // Note: We don't free the event data as it's managed by the MQTT client
    }
}

bool MQTTMSession::connect() {
    if (isConnected()) {
        return true;
    }

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = ("mqtt://" + host + ":" + std::to_string(port)).c_str();
    mqtt_cfg.credentials.client_id = client_id.c_str();
    
    if (!user.empty()) {
        mqtt_cfg.credentials.username = user.c_str();
    }
    if (!password.empty()) {
        mqtt_cfg.credentials.authentication.password = password.c_str();
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == nullptr) {
        Debug_printv("Failed to initialize MQTT client");
        return false;
    }

    // Register event handler
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, this);

    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        Debug_printv("Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
        return false;
    }

    // Wait for connection (simplified - in real implementation might need timeout)
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds for connection
    
    if (connected) {
        updateActivity();
        Debug_printv("MQTT connected to %s:%d", host.c_str(), port);
    } else {
        Debug_printv("MQTT connection failed");
        disconnect();
    }
    
    return connected;
}

void MQTTMSession::disconnect() {
    if (mqtt_client != nullptr) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
    }
    connected = false;
    Debug_printv("MQTT disconnected");
}

bool MQTTMSession::keep_alive() {
    if (!isConnected()) {
        return connect();
    }
    
    // MQTT client handles keep-alive automatically
    updateActivity();
    return true;
}

bool MQTTMSession::publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    if (!isConnected()) {
        return false;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic.c_str(), payload.c_str(), payload.length(), qos, retain ? 1 : 0);
    updateActivity();
    
    return (msg_id >= 0);
}

bool MQTTMSession::subscribe(const std::string& topic, int qos) {
    if (!isConnected()) {
        return false;
    }

    int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic.c_str(), qos);
    updateActivity();
    
    return (msg_id >= 0);
}

bool MQTTMSession::unsubscribe(const std::string& topic) {
    if (!isConnected()) {
        return false;
    }

    int msg_id = esp_mqtt_client_unsubscribe(mqtt_client, topic.c_str());
    updateActivity();
    
    return (msg_id >= 0);
}

bool MQTTMSession::hasMessage() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    return !message_queue.empty();
}

esp_mqtt_event_t* MQTTMSession::getMessage() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    if (message_queue.empty()) {
        return nullptr;
    }
    
    esp_mqtt_event_t* event = message_queue.front();
    message_queue.pop();
    return event;
}

void MQTTMSession::clearMessage() {
    // Messages are cleared when retrieved
}

void MQTTMSession::mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    MQTTMSession* session = static_cast<MQTTMSession*>(handler_args);
    esp_mqtt_event_t *event = static_cast<esp_mqtt_event_t *>(event_data);
    session->handle_event(event);
}

void MQTTMSession::handle_event(esp_mqtt_event_t *event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            Debug_printv("MQTT connected");
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            Debug_printv("MQTT disconnected");
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            Debug_printv("MQTT subscribed to topic, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            Debug_printv("MQTT unsubscribed from topic, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            Debug_printv("MQTT published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA: {
            // Queue the message for reading
            std::unique_lock<std::mutex> lock(queue_mutex);
            esp_mqtt_event_t* event_copy = new esp_mqtt_event_t(*event);
            // Copy the data
            if (event->data_len > 0) {
                event_copy->data = new char[event->data_len + 1];
                memcpy((void*)event_copy->data, event->data, event->data_len);
                event_copy->data[event->data_len] = '\0';
            }
            message_queue.push(event_copy);
            queue_cv.notify_one();
            Debug_printv("MQTT received data on topic %.*s: %.*s", 
                        event->topic_len, event->topic, event->data_len, event->data);
            break;
        }
            
        case MQTT_EVENT_ERROR:
            Debug_printv("MQTT error occurred");
            break;
            
        default:
            break;
    }
}

/********************************************************
 * MQTT File Implementation
 ********************************************************/

MQTTMFile::MQTTMFile(std::string path) : MFile(path) {
    // Parse the URL to extract broker, port, and topic
    auto urlParser = PeoplesUrlParser::parseURL(path);
    
    std::string broker_host = urlParser->host;
    uint16_t broker_port = 1883;
    if (!urlParser->port.empty()) {
        broker_port = std::stoi(urlParser->port);
    }
    
    // Extract topic from path (everything after the host/port)
    topic = urlParser->path;
    if (!topic.empty() && topic[0] == '/') {
        topic = topic.substr(1);
    }
    
    // Create or get existing session for this broker
    std::string session_key = broker_host + ":" + std::to_string(broker_port);
    
    // For now, create a new session. In a real implementation, you might want to cache sessions
    _session = std::make_shared<MQTTMSession>(broker_host, broker_port);
    
    Debug_printv("MQTTMFile created for topic: %s on %s:%d", topic.c_str(), broker_host.c_str(), broker_port);
}

MQTTMFile::~MQTTMFile() {
    // Session is shared, so don't disconnect here
}

std::shared_ptr<MStream> MQTTMFile::getSourceStream(std::ios_base::openmode mode) {
    return createStream(mode);
}

std::shared_ptr<MStream> MQTTMFile::createStream(std::ios_base::openmode mode) {
    auto stream = std::make_shared<MQTTMStream>(url);
    stream->session = _session;
    stream->topic = topic;
    return stream;
}

bool MQTTMFile::exists() {
    // For MQTT, "exists" means we can connect to the broker
    return _session && _session->isConnected();
}

/********************************************************
 * MQTT Stream Implementation
 ********************************************************/

MQTTMStream::MQTTMStream(std::string path) : MStream(path) {
    // Parse topic from path
    auto urlParser = PeoplesUrlParser::parseURL(path);
    topic = urlParser->path;
    if (!topic.empty() && topic[0] == '/') {
        topic = topic.substr(1);
    }
}

bool MQTTMStream::isOpen() {
    return session && session->isConnected();
}

bool MQTTMStream::open(std::ios_base::openmode mode) {
    if (!session) {
        return false;
    }
    
    // Connect to MQTT broker
    if (!session->connect()) {
        return false;
    }
    
    this->mode = mode;
    
    if (mode & std::ios_base::in) {
        // Subscribe to topic for reading
        if (!session->subscribe(topic)) {
            return false;
        }
        is_subscribed = true;
    }
    
    return true;
}

void MQTTMStream::close() {
    if (is_subscribed && session) {
        session->unsubscribe(topic);
        is_subscribed = false;
    }
    // Don't disconnect session as it might be shared
}

uint32_t MQTTMStream::read(uint8_t* buf, uint32_t size) {
    if (!isOpen() || !(mode & std::ios_base::in)) {
        return 0;
    }
    
    // If we have buffered data, use it first
    if (!read_buffer.empty() && read_pos < read_buffer.size()) {
        size_t available = read_buffer.size() - read_pos;
        size_t to_read = std::min((size_t)size, available);
        memcpy(buf, read_buffer.c_str() + read_pos, to_read);
        read_pos += to_read;
        
        if (read_pos >= read_buffer.size()) {
            read_buffer.clear();
            read_pos = 0;
        }
        
        return to_read;
    }
    
    // Wait for a message
    if (!session->hasMessage()) {
        // Non-blocking for now - in a real implementation you might want to wait with timeout
        return 0;
    }
    
    esp_mqtt_event_t* event = session->getMessage();
    if (event && event->event_id == MQTT_EVENT_DATA) {
        read_buffer.assign(event->data, event->data_len);
        read_pos = 0;
        
        // Clean up
        delete[] event->data;
        delete event;
        
        // Now read from the buffer
        return read(buf, size);
    }
    
    if (event) {
        delete event;
    }
    
    return 0;
}

uint32_t MQTTMStream::write(const uint8_t *buf, uint32_t size) {
    if (!isOpen() || !(mode & std::ios_base::out)) {
        return 0;
    }
    
    std::string payload((const char*)buf, size);
    if (session->publish(topic, payload)) {
        return size;
    }
    
    return 0;
}