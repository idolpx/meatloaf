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

#ifndef MEATLOAF_SESSION
#define MEATLOAF_SESSION

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../include/debug.h"

/********************************************************
 * Base Session Class
 ********************************************************/

class MSession {
public:
    MSession(std::string host, uint16_t port = 0)
        : host(host), port(port), connected(false),
          last_activity(std::chrono::steady_clock::now()),
          keep_alive_interval(30000) // Default 30 seconds
    {
        Debug_printv("MSession created for %s:%d", host.c_str(), port);
    }

    virtual ~MSession() {
        Debug_printv("MSession destroyed for %s:%d", host.c_str(), port);
        // Don't call disconnect() here - it's pure virtual
        // Derived classes should call disconnect() in their destructors
    }

    // Establish connection to the server
    virtual bool connect() = 0;

    // Close connection to the server
    virtual void disconnect() = 0;

    // Send keep-alive to maintain the connection
    virtual bool keep_alive() = 0;

    // Some sessions might need to refresh
    // SSDP, DNS-SD, DIAL, SMB SHARES, etc.
    virtual bool refresh() { return true; }

    // Check if session is connected
    virtual bool isConnected() const { return connected; }

    // Get unique key for this session
    virtual std::string getKey() const {
        return host + ":" + std::to_string(port);
    }

    // Get time since last activity in milliseconds
    uint32_t getIdleTime() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity);
        return static_cast<uint32_t>(elapsed.count());
    }

    // Update last activity timestamp
    void updateActivity() {
        last_activity = std::chrono::steady_clock::now();
    }

    // Get keep-alive interval in milliseconds
    uint32_t getKeepAliveInterval() const { return keep_alive_interval; }

    // Set keep-alive interval in milliseconds
    void setKeepAliveInterval(uint32_t interval_ms) {
        keep_alive_interval = interval_ms;
    }

protected:
    std::string host;
    uint16_t port;
    bool connected;
    std::chrono::steady_clock::time_point last_activity;
    uint32_t keep_alive_interval;  // in milliseconds
};


/********************************************************
 * Session Broker
 ********************************************************/

class SessionBroker {
private:
    static std::unordered_map<std::string, std::shared_ptr<MSession>> session_repo;
    static std::chrono::steady_clock::time_point last_keep_alive_check;
    static bool task_running;

    // FreeRTOS task function
    static void session_service_task(void* arg) {
        Debug_printv("SessionBroker task started on core %d", xPortGetCoreID());
        while (task_running) {
            service();
            vTaskDelay(pdMS_TO_TICKS(1000)); // Check every 1 second
        }
        vTaskDelete(NULL);
    }

public:
    // Initialize and start the SessionBroker service task
    // This creates a FreeRTOS task on CPU0 to periodically call service()
    static void setup() {
        if (task_running) {
            Debug_printv("SessionBroker task already running");
            return;
        }

        task_running = true;
        Debug_printv("Starting SessionBroker service task");

        // Create task on CPU0 (same core as WiFi)
        // Lower priority than IEC bus task
        // Increased stack size to 8192 for FSP/TNFS network operations
        xTaskCreatePinnedToCore(
            session_service_task,    // Task function
            "session_broker",        // Task name
            8192,                    // Stack size (increased from 4096)
            NULL,                    // Parameters
            5,                       // Priority (lower than IEC bus priority 17)
            NULL,                    // Task handle
            0                        // Core 0 (WiFi core)
        );
    }

    // Stop the SessionBroker service task
    static void shutdown() {
        Debug_printv("Stopping SessionBroker service task");
        task_running = false;
        clear();
    }

    // Obtain a session (creates if doesn't exist, returns existing if found)
    template<class T>
    static std::shared_ptr<T> obtain(std::string host, uint16_t port = 0) {
        std::string key = host + ":" + std::to_string(port);

        // Return existing session if found
        if (session_repo.find(key) != session_repo.end()) {
            //Debug_printv("Session found: %s", key.c_str());
            auto session = std::static_pointer_cast<T>(session_repo.at(key));
            session->updateActivity();
            return session;
        }

        // Create new session
        //Debug_printv("Creating new session: %s", key.c_str());
        auto newSession = std::make_shared<T>(host, port);

        if (newSession->connect()) {
            session_repo.insert(std::make_pair(key, newSession));
            newSession->updateActivity();
            return newSession;
        }

        Debug_printv("Failed to create session: %s", key.c_str());
        return nullptr;
    }

    // Dispose of a session
    static void dispose(std::string host, uint16_t port = 0) {
        std::string key = host + ":" + std::to_string(port);
        if (session_repo.find(key) != session_repo.end()) {
            Debug_printv("Disposing session: %s", key.c_str());
            session_repo.erase(key);
        }
    }

    // Process keep-alive for all sessions
    // IMPORTANT: This method should be called periodically from the main event loop
    // Recommended integration points:
    //   - In src/main.cpp main loop (every iteration or on a timer)
    //   - As a FreeRTOS task (xTaskCreate with appropriate priority)
    //   - In the systemBus service loop
    // Example: SessionBroker::service(); // Call every main loop iteration
    static void service() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_keep_alive_check);

        // Check every 1 second
        if (elapsed.count() < 1000) {
            return;
        }

        last_keep_alive_check = now;

        // Iterate through all sessions
        std::vector<std::string> to_remove;

        for (auto& pair : session_repo) {
            auto& session = pair.second;

            if (!session->isConnected()) {
                // Mark disconnected sessions for removal
                to_remove.push_back(pair.first);
                continue;
            }

            // Check if keep-alive is needed
            uint32_t idle_time = session->getIdleTime();
            if (idle_time >= session->getKeepAliveInterval()) {
                Debug_printv("Sending keep-alive to: %s (idle: %ums)",
                           pair.first.c_str(), idle_time);

                if (!session->keep_alive()) {
                    Debug_printv("Keep-alive failed for: %s", pair.first.c_str());
                    to_remove.push_back(pair.first);
                }
            }
        }

        // Remove failed sessions
        for (const auto& key : to_remove) {
            dispose(key);
        }

        if (!to_remove.empty()) {
            Debug_printv("Active sessions: %d", session_repo.size());
        }
    }

    // Get session count
    static size_t count() {
        return session_repo.size();
    }

    // Clear all sessions
    static void clear() {
        Debug_printv("Clearing all sessions");
        session_repo.clear();
    }
};


#endif // MEATLOAF_SESSION
