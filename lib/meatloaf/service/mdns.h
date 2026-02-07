// MDNS:// - mDNS (Multicast DNS) Network Service Discovery
// DNS-SD, SSDP, Bonjour, DIAL, Zeroconf
//
// https://en.wikipedia.org/wiki/Zero-configuration_networking
// https://jonathanmumm.com/tech-it/mdns-bonjour-bible-common-service-strings-for-various-vendors/
// https://pyatv.dev/documentation/protocols/
// https://www.rfc-editor.org/rfc/rfc6763.html
// https://www.w3.org/TR/discovery-api/
// https://www.w3.org/TR/presentation-api/
// https://www.w3.org/TR/remote-playback/
// https://developer.spotify.com/documentation/commercial-hardware/implementation/guides/zeroconf
// https://github.com/tronikos/androidtvremote2/tree/main
// 

#ifndef MEATLOAF_SERVICE_MDNS
#define MEATLOAF_SERVICE_MDNS

#include "meatloaf.h"
#include "meat_session.h"

extern "C" {
#include <mdns.h> // ESP-IDF mDNS API
}

// Forward declarations to avoid circular dependencies
class SMBMFile;
class HTTPMFile;
class SFTPMFile;
class AFPMFile;

#include "../../../include/debug.h"
#include "make_unique.h"
#include "string_utils.h"

#include <vector>
#include <map>
#include <memory>
#include <mutex>

/********************************************************
 * Discovered Service Structure
 ********************************************************/

struct DiscoveredService {
    std::string instance_name;      // Service instance name
    std::string service_type;       // Service type (_http, _smb, etc.)
    std::string proto;              // Protocol (_tcp, _udp)
    std::string hostname;           // Hostname
    uint16_t port;                  // Service port
    std::vector<std::string> addresses; // IP addresses
    std::map<std::string, std::string> txt_records; // TXT record key-value pairs
    uint32_t ttl;                   // Time to live
    
    // Generate a unique key for this service
    std::string getKey() const {
        return instance_name + "." + service_type + "." + proto + "@" + hostname;
    }
    
    // Get display name
    std::string getDisplayName() const {
        if (!instance_name.empty()) {
            return instance_name;
        }
        return hostname + ":" + std::to_string(port);
    }
};

/********************************************************
 * MSession - MDNS Session Management
 ********************************************************/

class MDNSMSession : public MSession {
public:
    MDNSMSession(std::string host = "mdns", uint16_t port = 0);
    ~MDNSMSession() override;

    bool connect() override;
    void disconnect() override;
    bool keep_alive() override;

    // Service discovery operations
    bool discoverServices(const std::string& service_type = "", const std::string& proto = "_tcp", uint32_t timeout_ms = 5000);
    std::vector<DiscoveredService> getDiscoveredServices() const;
    DiscoveredService* findServiceByInstance(const std::string& instance_name);
    std::vector<std::string> getServiceTypes() const;
    std::vector<DiscoveredService> getServicesOfType(const std::string& service_type) const;
    
    // Clear cached results
    void clearCache();
    
    uint32_t cache_timestamp_ms;  // Public for MDNSMFile access
    std::vector<DiscoveredService> cached_services;
    std::vector<std::string> cached_service_types;  // For root directory listing

private:
    bool mdns_initialized;
    std::vector<DiscoveredService> discovered_services;
    std::vector<std::string> discovered_service_types;
    mutable std::mutex services_mutex;
    
    // Helper to parse mDNS results
    void parseResults(mdns_result_t* results);
    
    friend class MDNSMFile;
    friend class MDNSMStream;
};


/********************************************************
 * MFile - MDNS File (represents discovered services)
 ********************************************************/

class MDNSMFile: public MFile
{
public:
    MDNSMFile(std::string path);
    ~MDNSMFile() override;

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override { return src; };
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    bool exists() override;
    bool remove() override { return false; }; // Can't remove discovered services
    bool rename(std::string dest) override { return false; }; // Not applicable
    
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };
    uint32_t size();

protected:
    std::shared_ptr<MDNSMSession> _session;
    std::string service_type;     // Target service type (empty = all)
    std::string instance_name;    // Specific instance (empty = list all)
    
    // Directory iteration state
    bool dirOpened;
    size_t dir_index;
    
    void parseUrl();
    void refreshServiceList();
    
    friend class MDNSMStream;
};


/********************************************************
 * MStream - MDNS Stream (service information)
 ********************************************************/

class MDNSMStream: public MStream
{
public:
    MDNSMStream(std::string path);
    ~MDNSMStream() override {
        close();
    }

    // MStream methods
    bool isOpen() override;
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;
    bool seek(uint32_t pos) override;
    
    uint32_t size() override { return content.size(); };
    uint32_t available() override { return content.size() - position(); };
    uint32_t position() override { return _position; };

private:
    std::shared_ptr<MDNSMSession> _session;
    std::string service_type;
    std::string instance_name;
    
    bool _is_open;
    std::string content;      // Service information as text
    uint32_t _position;
    
    void parseUrl();
    void generateContent();   // Generate text representation of service(s)
    
    friend class MDNSMFile;
};


/********************************************************
 * MFileSystem - MDNS Filesystem
 ********************************************************/

class MDNSMFileSystem: public MFileSystem
{
public:
    MDNSMFileSystem(): MFileSystem("mdns") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        std::string pattern = "mdns:";
        return mstr::startsWith(name, pattern.c_str(), false);
    }

    MFile* getFile(std::string path) override;
};


#endif // MEATLOAF_SERVICE_MDNS
