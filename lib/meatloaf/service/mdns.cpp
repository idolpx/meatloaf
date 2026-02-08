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

#include "service/mdns.h"

#include "network/afp.h"
#include "network/http.h"
#include "network/nfs.h"
#ifdef WITH_SFTP
#include "network/sftp.h"
#endif
#include "network/smb.h"

#include <esp_log.h>
#include <arpa/inet.h>
#include <sstream>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/********************************************************
 * MDNSMFileSystem Implementation
 ********************************************************/

MFile* MDNSMFileSystem::getFile(std::string path) {
    // If an instance name is specified, use it
    auto parser = PeoplesUrlParser::parseURL(path);
    //parser->dump();
    if (!parser->path.empty()) {
        std::string instance_name = parser->name;

        //Debug_printv("MDNS instance name: %s", instance_name.c_str());

        // If we have an instance name, try to find the specific service
        if (!instance_name.empty()) {
            // Get or create session - use dummy host since MDNS is local
            auto _session = SessionBroker::obtain<MDNSMSession>("mdns", 0);
            
            // Find service
            DiscoveredService* service = _session->findServiceByInstance(instance_name);
            if (service) {
                //Debug_printv("MDNS service found: %s, instance: %s, service type: %s", service->getDisplayName().c_str(), instance_name.c_str(), service->service_type.c_str());

                // Generate URL for service by service type
                MFile* file = nullptr;
                std::string path;
                if (service->service_type == "_nfs") {
                    if (!service->addresses.empty()) {
                        path = "nfs://" + service->addresses[0] + "/";
                        file = new NFSMFile(path);
                    }
                } 
#ifdef WITH_SFTP
                else if (service->service_type == "_sftp" || service->service_type == "_sftp-ssh" || service->service_type == "_sftp-ssh._tcp" || service->service_type == "_ssh" ||
                         service->service_type == "_sftp._tcp" || service->service_type == "_ssh._tcp") {
                    if (!service->addresses.empty()) {
                        path = "sftp://" + service->addresses[0] + "/";
                        file = new SFTPMFile(path);
                    }
                } 
#endif
                else if (service->service_type == "_smb") {
                    if (!service->addresses.empty()) {
                        path = "smb://" + service->addresses[0] + "/";
                        file = new SMBMFile(path);
                    }
                } else {
                    Debug_printv("Unsupported service type: %s", service->service_type.c_str());
                }
                SessionBroker::dispose("mdns", 0);
                if (file) {
                    Debug_printv("Created file for service: %s at %s", service->getDisplayName().c_str(), path.c_str());
                    return file;
                }
            }
        }
    }

    SessionBroker::dispose("mdns", 0);
    return new MDNSMFile(path);
}

/********************************************************
 * MDNSMSession Implementation
 ********************************************************/

MDNSMSession::MDNSMSession(std::string host, uint16_t port) 
    : MSession(host, port), cache_timestamp_ms(0), mdns_initialized(false) {
    Debug_printv("MDNSMSession created");
}

MDNSMSession::~MDNSMSession() {
    disconnect();
    Debug_printv("MDNSMSession destroyed");
}

bool MDNSMSession::connect() {
    if (connected) {
        return true;
    }

    if (!mdns_initialized) {
        esp_err_t err = mdns_init();
        if (err != ESP_OK) {
            Debug_printv("Failed to initialize mDNS: %d", err);
            return false;
        }
        mdns_initialized = true;
        Debug_printv("mDNS initialized successfully");
    }

    connected = true;
    updateActivity();
    return true;
}

void MDNSMSession::disconnect() {
    if (!connected) {
        return;
    }

    clearCache();
    
    if (mdns_initialized) {
        mdns_free();
        mdns_initialized = false;
    }

    connected = false;
    Debug_printv("MDNS session disconnected");
}

bool MDNSMSession::keep_alive() {
    if (!connected) {
        return connect();
    }
    
    updateActivity();
    return true;
}

bool MDNSMSession::discoverServices(const std::string& service_type, const std::string& proto, uint32_t timeout_ms) {
    if (!connect()) {
        return false;
    }

    Debug_printv("Discovering services: type=%s, proto=%s, timeout=%u ms", 
                 service_type.c_str(), proto.c_str(), timeout_ms);

    // Clear previous results when starting new discovery
    clearCache();
    
    const char* query_proto = proto.empty() ? "_tcp" : proto.c_str();
    
    // If service_type is empty, use DNS-SD meta-query to discover all service types
    if (service_type.empty()) {
        Debug_printv("Stage 1: Querying for all service types via _services._dns-sd._udp");
        
        mdns_result_t *type_results = nullptr;
        esp_err_t err = mdns_query_ptr("_services._dns-sd", "_udp", timeout_ms, 50, &type_results);
        
        Debug_printv("Meta-query result: err=%d, results=%p", err, type_results);
        
        if (err != ESP_OK) {
            Debug_printv("Service type query failed: ESP error code %d (0x%x)", err, err);
            Debug_printv("  ESP_ERR_NOT_FOUND=0x%x, ESP_ERR_NO_MEM=0x%x, ESP_ERR_INVALID_ARG=0x%x", 
                        ESP_ERR_NOT_FOUND, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG);
        } else if (!type_results) {
            Debug_printv("Meta-query succeeded but returned no results");
            Debug_printv("This is normal - many devices don't advertise _services._dns-sd._udp");
        }

        // Extract service types from results
        std::lock_guard<std::mutex> lock(services_mutex);
        discovered_service_types.clear();
        for (mdns_result_t* r = type_results; r != nullptr; r = r->next) {
            // Debug_printv("Meta-query result: instance_name='%s', hostname='%s', port=%d, addr=%p", 
            //             r->instance_name ? r->instance_name : "NULL",
            //             r->hostname ? r->hostname : "NULL", 
            //             r->port,
            //             r->addr);
            if (r->instance_name) {
                // instance_name contains the full service type (e.g., "_http._tcp")
                std::string full_type = r->instance_name;
                //Debug_printv("Found service type: %s", full_type.c_str());
                
                discovered_service_types.push_back(full_type);
            }
        }
        mdns_query_results_free(type_results);

        // Sort and remove duplicates
        std::sort(discovered_service_types.begin(), discovered_service_types.end());
        //discovered_service_types.erase(std::unique(discovered_service_types.begin(), discovered_service_types.end()), discovered_service_types.end());

        Debug_printv("Found %d service types", discovered_service_types.size());
        return !discovered_service_types.empty();
    }
    
    // Query for specific service type
    mdns_result_t *results = nullptr;
    
    Debug_printv("Querying for service: type=%s, proto=%s", service_type.c_str(), query_proto);
    esp_err_t err = mdns_query_ptr(service_type.c_str(), query_proto, timeout_ms, 20, &results);
    
    if (err != ESP_OK) {
        Debug_printv("mDNS query failed: %d", err);
        return false;
    }

    if (results) {
        parseResults(results);
        mdns_query_results_free(results);

        // Sort discovered services by display name
        std::lock_guard<std::mutex> lock(services_mutex);
        std::sort(discovered_services.begin(), discovered_services.end(),
                  [](const DiscoveredService& a, const DiscoveredService& b) {
                      return a.getDisplayName() < b.getDisplayName();
                  });

        Debug_printv("Discovered %d services", discovered_services.size());
        return true;
    }

    Debug_printv("No services discovered");
    return false;
}

void MDNSMSession::parseResults(mdns_result_t* results) {
    std::lock_guard<std::mutex> lock(services_mutex);
    
    // Don't clear - we may be accumulating results from multiple queries
    
    for (mdns_result_t* r = results; r != nullptr; r = r->next) {
        DiscoveredService service;
        
        if (r->instance_name) {
            service.instance_name = r->instance_name;
        }
        if (r->service_type) {
            service.service_type = r->service_type;
        }
        if (r->proto) {
            service.proto = r->proto;
        }
        if (r->hostname) {
            service.hostname = r->hostname;
        }
        service.port = r->port;
        service.ttl = r->ttl;
        
        // Parse IP addresses
        for (mdns_ip_addr_t* addr = r->addr; addr != nullptr; addr = addr->next) {
            char ip_str[48];  // Large enough for IPv6
            if (addr->addr.type == ESP_IPADDR_TYPE_V4) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&addr->addr.u_addr.ip4));
                service.addresses.push_back(ip_str);
            } else if (addr->addr.type == ESP_IPADDR_TYPE_V6) {
                snprintf(ip_str, sizeof(ip_str), IPV6STR, IPV62STR(addr->addr.u_addr.ip6));
                service.addresses.push_back(ip_str);
            }
        }
        
        // Parse TXT records
        if (r->txt && r->txt_count > 0) {
            for (size_t i = 0; i < r->txt_count; i++) {
                if (r->txt[i].key && r->txt[i].value) {
                    service.txt_records[r->txt[i].key] = r->txt[i].value;
                }
            }
        }
        
        discovered_services.push_back(service);
        
        Debug_printv("Found service: %s.%s.%s @ %s:%d", 
                     service.instance_name.c_str(),
                     service.service_type.c_str(),
                     service.proto.c_str(),
                     service.hostname.c_str(),
                     service.port);
    }
}

std::vector<DiscoveredService> MDNSMSession::getDiscoveredServices() const {
    std::lock_guard<std::mutex> lock(services_mutex);
    return discovered_services;
}

DiscoveredService* MDNSMSession::findServiceByInstance(const std::string& instance_name) {
    std::lock_guard<std::mutex> lock(services_mutex);
    
    for (auto& service : discovered_services) {
        //Debug_printv("Checking service: %s, instance: %s", service.getDisplayName().c_str(), instance_name.c_str()); 
        if (service.instance_name == instance_name || service.hostname == instance_name) {
            return &service;
        }
    }
    
    return nullptr;
}

std::vector<std::string> MDNSMSession::getServiceTypes() const {
    std::lock_guard<std::mutex> lock(services_mutex);
    
    if (!discovered_service_types.empty()) {
        return discovered_service_types;
    }
    
    std::vector<std::string> types;
    for (const auto& service : discovered_services) {
        std::string type;
        if (service.service_type == "_services._dns-sd") {
            // This is from a meta-query, instance_name contains the service type
            type = service.instance_name;
        } else {
            // Normal service discovery
            type = service.service_type + "." + service.proto;
        }
        if (!type.empty() && std::find(types.begin(), types.end(), type) == types.end()) {
            types.push_back(type);
        }
    }
    
    return types;
}

std::vector<DiscoveredService> MDNSMSession::getServicesOfType(const std::string& service_type) const {
    std::lock_guard<std::mutex> lock(services_mutex);
    
    std::vector<DiscoveredService> filtered;
    for (const auto& service : discovered_services) {
        // Check both the base type and the full type with protocol
        bool type_matches = (service.service_type == service_type || 
                           service.service_type == service_type + "._tcp" ||
                           service.service_type == service_type + "._udp");
        
        // Exclude services where the instance name matches the service type (avoid listing the type itself as an instance)
        bool is_not_type_entry = (service.instance_name != service_type &&
                                service.instance_name != service_type + "._tcp" &&
                                service.instance_name != service_type + "._udp");
        
        if (type_matches && is_not_type_entry) {
            filtered.push_back(service);
        }
    }
    
    return filtered;
}

void MDNSMSession::clearCache() {
    std::lock_guard<std::mutex> lock(services_mutex);
    discovered_services.clear();
    discovered_service_types.clear();
    cached_services.clear();
    cached_service_types.clear();
    cache_timestamp_ms = 0;
}


/********************************************************
 * MDNSMFile Implementation
 ********************************************************/

MDNSMFile::MDNSMFile(std::string path) : MFile(path), dirOpened(false), dir_index(0) {
    //Debug_printv("MDNSMFile created: %s", path.c_str());
    
    // Get or create session - use dummy host since MDNS is local
    _session = SessionBroker::obtain<MDNSMSession>("mdns", 0);
    
    parseUrl();
}

MDNSMFile::~MDNSMFile() {
    //Debug_printv("MDNSMFile destroyed");
}

void MDNSMFile::parseUrl() {
    // URL format: mdns://[service_type]/[instance_name]
    // Examples:
    //   mdns://              - list all services
    //   mdns://_http._tcp/   - list all HTTP services
    //   mdns://_http._tcp/MyServer - get info about MyServer HTTP service
    
    std::string path_str = url;
    
    // Remove mdns:// prefix
    if (mstr::startsWith(path_str, "mdns://")) {
        path_str = path_str.substr(6);
    } else if (mstr::startsWith(path_str, "mdns:/")) {
        path_str = path_str.substr(5);
    }
    
    // Remove leading slash if present
    if (!path_str.empty() && path_str[0] == '/') {
        path_str = path_str.substr(1);
    }
    
    // Split by /
    size_t slash_pos = path_str.find('/');
    if (slash_pos != std::string::npos) {
        service_type = path_str.substr(0, slash_pos);
        instance_name = path_str.substr(slash_pos + 1);
        
        // Remove trailing slash
        if (!instance_name.empty() && instance_name.back() == '/') {
            instance_name.pop_back();
        }
    } else {
        service_type = path_str;
        instance_name = "";
    }
    
    // Remove trailing slash from service_type
    if (!service_type.empty() && service_type.back() == '/') {
        service_type.pop_back();
    }
    
    // Add leading "_" if missing from service_type
    if (!service_type.empty() && service_type[0] != '_') {
        service_type = "_" + service_type;
    }
    
    // Reconstruct URL with proper format
    url = "mdns://";
    if (!service_type.empty()) {
        url += service_type;
        if (!instance_name.empty()) {
            url += "/" + instance_name;
        }
    }
    
    // Debug_printv("Parsed URL - service_type: '%s', instance_name: '%s'", 
    //              service_type.c_str(), instance_name.c_str());
}

void MDNSMFile::refreshServiceList() {
    if (!_session || !_session->connect()) {
        Debug_printv("Failed to connect MDNS session");
        return;
    }
    
    // Check if cache is still valid (less than 5 seconds old)
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t cache_age = (_session->cache_timestamp_ms > 0) ? (now_ms - _session->cache_timestamp_ms) : 0;
    
    if ((_session->cached_services.size() || _session->cached_service_types.size()) && 
        _session->cache_timestamp_ms > 0 && cache_age < 5000) {
        Debug_printv("Using cached service list (age: %u ms)", cache_age);
        return;
    }
    
    Debug_printv("Refreshing service list (cache age: %u ms)", cache_age);
    
    // At root level (mdns:/), discover all services and get service types
    if (service_type.empty() || mstr::startsWith(service_type, "_services")) {
        Debug_printv("Root directory - discovering all services to get types");
        _session->discoverServices("", "_udp", 3000);
        _session->cached_service_types = _session->getServiceTypes();
        _session->cached_services.clear();
        Debug_printv("Found %d unique service types", _session->cached_service_types.size());
    } else {
        // In a service type directory, get instances of that type
        std::string proto = "_tcp";  // Default to TCP
        std::string type = service_type;
        
        size_t proto_pos = service_type.rfind('.');
        if (proto_pos != std::string::npos) {
            type = service_type.substr(0, proto_pos);
            proto = service_type.substr(proto_pos + 1);
        }
        
        Debug_printv("Service type directory - discovering instances of %s.%s", type.c_str(), proto.c_str());
        // Discover services of this specific type
        _session->discoverServices(type, proto, 3000);
        // Then filter to get only this type
        _session->cached_services = _session->getServicesOfType(type);
        _session->cached_service_types.clear();
        
        Debug_printv("Found %d instances of %s:", _session->cached_services.size(), type.c_str());
        for (const auto& svc : _session->cached_services) {
            Debug_printv("  Instance: '%s', Type: '%s', Host: '%s'", 
                        svc.instance_name.c_str(), svc.service_type.c_str(), svc.hostname.c_str());
        }
    }
    
    // Update cache timestamp
    _session->cache_timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

bool MDNSMFile::isDirectory() {
    // It's a directory if no instance is specified
    //return instance_name.empty();
    return true;
}

bool MDNSMFile::exists() {
    if (!_session || !_session->connect()) {
        return false;
    }
    
    if (instance_name.empty()) {
        // Directory always exists
        return true;
    }
    
    // Check if specific service exists
    return _session->findServiceByInstance(instance_name) != nullptr;
}

bool MDNSMFile::rewindDirectory() {
    dir_index = 0;
    dirOpened = true;
    refreshServiceList();

    // Set Media Info Fields
    media_header = "NETWORK EXPLORER";
    media_id = "{{id}} ";
    if (path.size() == 1) {
        media_partition = _session->discovered_service_types.size();
        media_id += mstr::format("%02i", media_partition); // Number of service types as "ID"
    } else {
        media_partition = _session->discovered_services.size();
        media_id += mstr::format("%02i", media_partition); // Number of services as "ID"
    }

    Debug_printv("media_header[%s] media_id[%s]", media_header.c_str(), media_id.c_str());

    return true;
}

MFile* MDNSMFile::getNextFileInDir() {
    if (!dirOpened) {
        rewindDirectory();
    }
    
    // At root level, return service type directories
    if (service_type.empty()) {
        if (dir_index >= _session->cached_service_types.size()) {
            dirOpened = false;
            return nullptr;
        }
        
        const auto& type = _session->cached_service_types[dir_index++];
        std::string file_path = "mdns:///" + type; // This requires the extra /
        
        MDNSMFile* file = new MDNSMFile(file_path);
        //Debug_printv("Returning service type directory: %s", file_path.c_str());
        return file;
    }
    
    // In a service type directory, return service instances
    if (dir_index >= _session->cached_services.size()) {
        dirOpened = false;
        return nullptr;
    }
    
    const auto& service = _session->cached_services[dir_index++];
    
    // Create file entry for this service instance
    std::string file_path = "mdns://" + service_type + "/" + service.getDisplayName();
    
    MDNSMFile* file = new MDNSMFile(file_path);
    //Debug_printv("Returning service instance: %s", file_path.c_str());
    return file;
}

uint32_t MDNSMFile::size() {
    if (instance_name.empty()) {
        return 0; // Directory has no size
    }
    
    // Size is the length of the service information text
    auto stream = std::static_pointer_cast<MDNSMStream>(createStream(std::ios_base::in));
    if (stream) {
        return stream->size();
    }
    
    return 0;
}

std::shared_ptr<MStream> MDNSMFile::getSourceStream(std::ios_base::openmode mode) {
    return createStream(mode);
}

std::shared_ptr<MStream> MDNSMFile::createStream(std::ios_base::openmode mode) {
    return std::make_shared<MDNSMStream>(url);
}


/********************************************************
 * MDNSMStream Implementation
 ********************************************************/

MDNSMStream::MDNSMStream(std::string path) 
    : MStream(path), _is_open(false), _position(0) {
    Debug_printv("MDNSMStream created: %s", path.c_str());
    
    // Get or create session - use dummy host since MDNS is local
    _session = SessionBroker::obtain<MDNSMSession>("mdns", 0);
    
    parseUrl();
}

void MDNSMStream::parseUrl() {
    // Same parsing as MDNSMFile
    std::string path_str = url;
    
    if (mstr::startsWith(path_str, "mdns://")) {
        path_str = path_str.substr(6);
    }
    
    size_t slash_pos = path_str.find('/');
    if (slash_pos != std::string::npos) {
        service_type = path_str.substr(0, slash_pos);
        instance_name = path_str.substr(slash_pos + 1);
        
        if (!instance_name.empty() && instance_name.back() == '/') {
            instance_name.pop_back();
        }
    } else {
        service_type = path_str;
        instance_name = "";
    }
    
    if (!service_type.empty() && service_type.back() == '/') {
        service_type.pop_back();
    }
}

void MDNSMStream::generateContent() {
    content.clear();
    
    if (!_session || !_session->connect()) {
        content = "Error: Failed to connect to MDNS service\n";
        return;
    }
    
    // Extract protocol from service_type if present
    std::string proto = "_udp";
    std::string type = service_type;
    
    size_t proto_pos = service_type.rfind('.');
    if (proto_pos != std::string::npos) {
        type = service_type.substr(0, proto_pos);
        proto = service_type.substr(proto_pos + 1);
    }
    
    // Discover services
    _session->discoverServices(type, proto, 3000);
    
    if (instance_name.empty()) {
        // List all services of this type
        std::vector<DiscoveredService> services;
        if (service_type.empty()) {
            services = _session->getDiscoveredServices();
        } else {
            services = _session->getServicesOfType(type);
        }
        
        std::stringstream ss;
        ss << "Network Service Discovery Results\n";
        ss << "==================================\n\n";
        
        if (services.empty()) {
            ss << "No services found.\n";
        } else {
            for (const auto& service : services) {
                ss << "Service: " << service.getDisplayName() << "\n";
                ss << "  Type: " << service.service_type << "." << service.proto << "\n";
                ss << "  Host: " << service.hostname << "\n";
                ss << "  Port: " << service.port << "\n";
                
                if (!service.addresses.empty()) {
                    ss << "  Addresses: ";
                    for (size_t i = 0; i < service.addresses.size(); i++) {
                        if (i > 0) ss << ", ";
                        ss << service.addresses[i];
                    }
                    ss << "\n";
                }
                
                if (!service.txt_records.empty()) {
                    ss << "  TXT Records:\n";
                    for (const auto& txt : service.txt_records) {
                        ss << "    " << txt.first << " = " << txt.second << "\n";
                    }
                }
                
                ss << "\n";
            }
        }
        
        content = ss.str();
    } else {
        // Show specific service details
        DiscoveredService* service = _session->findServiceByInstance(instance_name);
        
        if (!service) {
            content = "Error: Service not found: " + instance_name + "\n";
            return;
        }
        
        std::stringstream ss;
        ss << "Service Details: " << service->getDisplayName() << "\n";
        ss << "==========================================\n\n";
        ss << "Instance Name: " << service->instance_name << "\n";
        ss << "Service Type:  " << service->service_type << "." << service->proto << "\n";
        ss << "Hostname:      " << service->hostname << "\n";
        ss << "Port:          " << service->port << "\n";
        ss << "TTL:           " << service->ttl << " seconds\n";
        
        if (!service->addresses.empty()) {
            ss << "\nIP Addresses:\n";
            for (const auto& addr : service->addresses) {
                ss << "  " << addr << "\n";
            }
        }
        
        if (!service->txt_records.empty()) {
            ss << "\nTXT Records:\n";
            for (const auto& txt : service->txt_records) {
                ss << "  " << txt.first << " = " << txt.second << "\n";
            }
        }
        
        content = ss.str();
    }
}

bool MDNSMStream::isOpen() {
    return _is_open;
}

bool MDNSMStream::open(std::ios_base::openmode mode) {
    if (_is_open) {
        return true;
    }
    
    Debug_printv("Opening MDNS stream: %s", url.c_str());
    
    // Generate content
    generateContent();
    
    _position = 0;
    _is_open = true;
    
    return true;
}

void MDNSMStream::close() {
    if (_is_open) {
        content.clear();
        _position = 0;
        _is_open = false;
        Debug_printv("MDNS stream closed");
    }
}

uint32_t MDNSMStream::read(uint8_t* buf, uint32_t size) {
    if (!_is_open) {
        if (!open(std::ios_base::in)) {
            return 0;
        }
    }
    
    uint32_t available_bytes = content.size() - _position;
    uint32_t bytes_to_read = std::min(size, available_bytes);
    
    if (bytes_to_read > 0) {
        memcpy(buf, content.data() + _position, bytes_to_read);
        _position += bytes_to_read;
    }
    
    return bytes_to_read;
}

uint32_t MDNSMStream::write(const uint8_t *buf, uint32_t size) {
    // MDNS is read-only
    return 0;
}

bool MDNSMStream::seek(uint32_t pos) {
    if (pos <= content.size()) {
        _position = pos;
        return true;
    }
    return false;
}
