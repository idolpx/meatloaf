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
#include <cstdlib>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../../include/debug.h"

/********************************************************
 * Base Session Class
 ********************************************************/

class MSession {
public:
    // Cached file data shared across streams — persists for the session lifetime
    struct CachedFile {
        uint8_t* data;
        uint32_t size;
        CachedFile(uint8_t* d, uint32_t s) : data(d), size(s) {}
        ~CachedFile() { if (data) free(data); }
        CachedFile(const CachedFile&) = delete;
        CachedFile& operator=(const CachedFile&) = delete;
    };

    MSession(std::string key, std::string host = "", uint16_t port = 0)
        : key(key), host(host), port(port), connected(false),
          last_activity(std::chrono::steady_clock::now()),
          keep_alive_interval(30000) // Default 30 seconds
    {
        Debug_printv("MSession created: %s", key.c_str());
    }

    virtual ~MSession() {
        Debug_printv("MSession destroyed: %s", key.c_str());
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

    // Get unique key for this session (scheme://host:port)
    std::string getKey() const { return key; }

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

    void acquireIO() {
        io_active.fetch_add(1, std::memory_order_relaxed);
        updateActivity();
    }

    void releaseIO() {
        uint32_t current = io_active.load(std::memory_order_relaxed);
        if (current > 0) {
            io_active.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    bool isBusy() const {
        return io_active.load(std::memory_order_relaxed) > 0;
    }

    // File cache — avoids re-downloading files for random-access container I/O
    std::shared_ptr<CachedFile> getCachedFile(const std::string& path) {
        auto it = file_cache.find(path);
        if (it != file_cache.end()) {
            return it->second;
        }
        return nullptr;
    }

    void cacheFile(const std::string& path, std::shared_ptr<CachedFile> file) {
        file_cache[path] = file;
        Debug_printv("Cached file: %s (%u bytes), cache entries: %d", path.c_str(), file->size, file_cache.size());
    }

    void clearFileCache() {
        if (!file_cache.empty()) {
            Debug_printv("Clearing file cache (%d entries)", file_cache.size());
            file_cache.clear();
        }
    }

protected:
    std::string key;  // scheme://host:port
    std::string host;
    uint16_t port;
    std::string user;
    std::string password;
    bool connected;
    std::chrono::steady_clock::time_point last_activity;
    uint32_t keep_alive_interval;  // in milliseconds
    std::unordered_map<std::string, std::shared_ptr<CachedFile>> file_cache;
    std::atomic<uint32_t> io_active{0};
};


/********************************************************
 * Session Broker
 ********************************************************/

class SessionBroker {
private:
    static std::unordered_map<std::string, std::shared_ptr<MSession>> session_repo;
    static std::chrono::steady_clock::time_point last_keep_alive_check;
    static bool task_running;
    static SemaphoreHandle_t _mutex;

    static void lock() {
        if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    }
    static void unlock() {
        if (_mutex) xSemaphoreGive(_mutex);
    }

    // FreeRTOS task function
    static void session_service_task(void* arg) {
        Debug_printv("SessionBroker task started on core %d", xPortGetCoreID());
        while (task_running) {
            service();
            vTaskDelay(pdMS_TO_TICKS(1000)); // Check every 1 second
        }
        vTaskDelete(NULL);
    }

    // Internal dispose by key (no lock, caller must hold mutex)
    static void disposeByKey(const std::string& key) {
        auto it = session_repo.find(key);
        if (it != session_repo.end()) {
            Debug_printv("Disposing session: %s", key.c_str());
            session_repo.erase(it);
        }
    }

public:
    // Initialize and start the SessionBroker service task
    // This creates a FreeRTOS task on CPU0 to periodically call service()
    static void setup() {
        if (task_running) {
            Debug_printv("SessionBroker task already running");
            return;
        }

        if (!_mutex) {
            _mutex = xSemaphoreCreateMutex();
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
        lock();
        session_repo.clear();
        unlock();
    }

    // Find an existing session by key (does not create)
    template<class T>
    static std::shared_ptr<T> find(const std::string& key) {
        lock();
        auto it = session_repo.find(key);
        if (it != session_repo.end()) {
            auto session = std::static_pointer_cast<T>(it->second);
            session->updateActivity();  // Keep session alive while in use
            unlock();
            return session;
        }
        unlock();
        return nullptr;
    }

    // Add a session to the repo
    static void add(const std::string& key, std::shared_ptr<MSession> session) {
        lock();
        session_repo.insert(std::make_pair(key, session));
        session->updateActivity();
        unlock();
    }

    // Obtain a session (creates if doesn't exist, returns existing if found)
    template<class T>
    static std::shared_ptr<T> obtain(std::string host, uint16_t port = 0) {
        // Create session to get the canonical key (scheme://host:port)
        auto newSession = std::make_shared<T>(host, port);
        std::string key = newSession->getKey();

        // Return existing session if found
        auto existing = find<T>(key);
        if (existing) {
            return existing;
        }

        // Connect the new session (outside lock — connect() is slow)
        if (newSession->connect()) {
            add(key, newSession);
            return newSession;
        }

        Debug_printv("Failed to create session: %s", key.c_str());
        return nullptr;
    }

    // Dispose of a session by key
    static void dispose(const std::string& key) {
        lock();
        disposeByKey(key);
        unlock();
    }

    // Process keep-alive for all sessions
    static void service() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_keep_alive_check);

        // Check every 1 second
        if (elapsed.count() < 1000) {
            return;
        }

        last_keep_alive_check = now;

        // Collect sessions that need keep-alive or removal under lock
        std::vector<std::string> to_remove;
        std::vector<std::pair<std::string, std::shared_ptr<MSession>>> to_check;

        lock();
        for (auto& pair : session_repo) {
            auto& session = pair.second;

            if (!session->isConnected()) {
                to_remove.push_back(pair.first);
                continue;
            }

            if (session->isBusy()) {
                continue;
            }

            uint32_t idle_time = session->getIdleTime();
            if (idle_time >= session->getKeepAliveInterval()) {
                to_check.push_back(pair);
            }
        }
        unlock();

        // Run keep-alive checks outside the lock (network I/O)
        for (auto& pair : to_check) {
            Debug_printv("Sending keep-alive to: %s (idle: %ums)",
                       pair.first.c_str(), pair.second->getIdleTime());

            if (!pair.second->keep_alive()) {
                Debug_printv("Keep-alive failed for: %s", pair.first.c_str());
                to_remove.push_back(pair.first);
            }
        }

        // Remove failed sessions
        if (!to_remove.empty()) {
            lock();
            for (const auto& key : to_remove) {
                Debug_printv("Removing session: %s", key.c_str());
                disposeByKey(key);
            }
            Debug_printv("Active sessions: %d", session_repo.size());
            unlock();
        }
    }

    // Get session count
    static size_t count() {
        lock();
        size_t c = session_repo.size();
        unlock();
        return c;
    }

    // Clear all sessions
    static void clear() {
        lock();
        Debug_printv("Clearing all sessions");
        session_repo.clear();
        unlock();
    }
};


#endif // MEATLOAF_SESSION
