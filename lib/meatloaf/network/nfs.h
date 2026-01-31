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

// NFS:// - Network File System
// https://en.wikipedia.org/wiki/Network_File_System
// 
// https://github.com/sahlberg/libnfs
//


#ifndef MEATLOAF_DEVICE_NFS
#define MEATLOAF_DEVICE_NFS

#include "meatloaf.h"

#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>

#include <sys/poll.h>
#include <map>

#include "fnFS.h"

#include "../../../include/debug.h"

#include "make_unique.h"

#include "meat_session.h"

#include <dirent.h>
#include <string.h>
#include <fcntl.h>

// Helper function declarations
bool parseNFSPath(const std::string& path, std::string& export_path, std::string& file_path);

/********************************************************
 * NFSMSession - NFS Session Management
 ********************************************************/

class NFSMSession : public MSession {
public:
    NFSMSession(std::string host, uint16_t port = 2049) : MSession(host, port) {
        Debug_printv("NFSMSession created for %s:%d", host.c_str(), port);
    }
    ~NFSMSession() override {
        Debug_printv("NFSMSession destroyed for %s:%d", host.c_str(), port);
        disconnect();
    }

    bool connect() override {
        if (connected) return true;

        // Initialize NFS context
        _nfs = nfs_init_context();
        if (!_nfs) {
            Debug_printv("Failed to initialize NFS context for %s:%d", host.c_str(), port);
            connected = false;
            return false;
        }

        // Disable directory caching to prevent memory leaks on ESP32
        nfs_set_dircache(_nfs, 0);
        
        // Reduce buffer sizes for ESP32 memory constraints
        nfs_set_readmax(_nfs, 255);   // Default is 65536
        nfs_set_writemax(_nfs, 255);  // Default is 65536

        // Don't mount yet - just establish connection
        // Mount will happen when accessing specific exports
        connected = true;
        updateActivity();
        Debug_printv("Connected to NFS server at %s:%d", host.c_str(), port);
        return true;
    }

    void disconnect() override {
        // Disconnect all export contexts
        for (auto& pair : _export_contexts) {
            if (pair.second) {
                // Only try to unmount if the context appears valid
                // Skip unmount to avoid crashes with partially initialized contexts
                nfs_destroy_context(pair.second);
            }
        }
        _export_contexts.clear();
        
        if (_nfs) {
            // Only destroy the context, don't try to unmount as it may not have been mounted
            nfs_destroy_context(_nfs);
            _nfs = nullptr;
        }
        
        connected = false;
        Debug_printv("Disconnected from NFS server at %s:%d", host.c_str(), port);
    }

    bool keep_alive() override {
        if (!connected || !_nfs) return false;
        
        // For NFS, we can check connection by attempting a simple operation
        // For now, we'll consider the connection alive if nfs context exists
        updateActivity();
        return true;
    }

    struct nfs_context* getContext() { return _nfs; }
    
    // Get or create a context for a specific export
    struct nfs_context* getExportContext(const std::string& export_path) {
        if (export_path.empty() || export_path == "/") {
            return _nfs;  // Use root context for root or empty export
        }
        
        // Check if we already have a cached context for this export
        auto it = _export_contexts.find(export_path);
        if (it != _export_contexts.end() && it->second != nullptr) {
            return it->second;
        }
        
        // Create a new context for this export
        Debug_printv("Creating new context for export: %s", export_path.c_str());
        struct nfs_context* nfs = nfs_init_context();
        if (!nfs) {
            Debug_printv("Failed to initialize NFS context for export %s", export_path.c_str());
            return nullptr;
        }

        // Disable directory caching to prevent memory leaks on ESP32
        nfs_set_dircache(nfs, 0);
        
        // Reduce buffer sizes for ESP32 memory constraints
        nfs_set_readmax(nfs, 255);   // Default is 65536
        nfs_set_writemax(nfs, 255);  // Default is 65536

        // Mount the specific export
        std::string mount_path = export_path;
        if (!mount_path.empty() && mount_path[0] != '/') {
            mount_path = "/" + mount_path;
        }
        if (nfs_mount(nfs, host.c_str(), mount_path.c_str()) != 0) {
            Debug_printv("Failed to mount export %s: %s", mount_path.c_str(), nfs_get_error(nfs));
            nfs_destroy_context(nfs);
            return nullptr;
        }
        
        // Cache the context for future use
        _export_contexts[export_path] = nfs;
        Debug_printv("Cached context for export: %s", export_path.c_str());
        
        return nfs;
    }

    // Get list of exports on this server (cached after first enumeration)
    const std::vector<std::string>& getExports();

private:
    void enumerateExports();

private:
    struct nfs_context* _nfs = nullptr;
    std::map<std::string, struct nfs_context*> _export_contexts;  // Per-export contexts
    std::vector<std::string> _exports_list;  // Cached list of exports
    bool _exports_enumerated = false;  // Flag to track if exports have been enumerated
};

/********************************************************
 * MFile
 ********************************************************/

class NFSMFile: public MFile
{
public:
    std::string basepath = "";
    std::string export_path = "";
    std::string file_path = "";
    
    NFSMFile(std::string path): MFile(path) {
        // Obtain or create NFS session via SessionBroker
        uint16_t nfs_port = port.empty() ? 2049 : std::stoi(port);
        _session = SessionBroker::obtain<NFSMSession>(host, nfs_port);

        if (!_session || !_session->isConnected()) {
            Debug_printv("Failed to obtain NFS session for %s:%d", host.c_str(), nfs_port);
            m_isNull = true;
            return;
        }

        // Extract export path from path
        parseNFSPath(this->path, export_path, file_path);

        // Create/obtain export context if we have a specific export
        if (!export_path.empty()) {
            _export_context = _session->getExportContext(export_path);
            if (!_export_context) {
                Debug_printv("Failed to get export context for: %s", export_path.c_str());
                m_isNull = true;
                return;
            }
        }

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry( name );

        if (!pathValid(this->path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;
    };
    
    ~NFSMFile() {
        closeDir();
        // Don't destroy export context - it's owned and cached by NFSMSession
        _export_context = nullptr;
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src);
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override;
    time_t getCreationTime() override;
    uint64_t getAvailableSpace() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;
    bool exists() override;

    bool remove() override;
    bool rename(std::string dest);

    bool readEntry( std::string filename );

protected:
    bool dirOpened = false;

    std::shared_ptr<NFSMSession> _session;
    struct nfs_context* _export_context = nullptr;  // Export-specific context owned by session
    struct nfsdir *_handle_dir = nullptr;

    // Helper to get NFS context - use the export context if available, otherwise root
    struct nfs_context* getNFS() { 
        if (_export_context) {
            return _export_context;  // Use pre-obtained export context
        }
        if (!_session) return nullptr;
        return _session->getContext();  // Fall back to root context for export enumeration
    }

private:
    virtual void openDir(std::string path);
    virtual void closeDir();

    bool pathValid(std::string path);

    int entry_index;
    static std::vector<std::string> exports;  // Local export list during enumeration
};


/********************************************************
 * NFSHandle
 ********************************************************/

class NFSHandle {
public:
    NFSHandle() : _session(nullptr), _handle(nullptr) {
    };
    ~NFSHandle();
    void obtain(std::string localPath, int nfs_mode);
    void dispose();

    struct nfs_context* getNFS() { return _session ? _session->getContext() : nullptr; }

private:
    std::shared_ptr<NFSMSession> _session;
    struct nfsfh *_handle = nullptr;
    int flags = 0;
};


/********************************************************
 * MStream
 ********************************************************/

class NFSMStream: public MStream {
public:
    NFSMStream(std::string& path): MStream(path) {
        // Obtain or create NFS session via SessionBroker
        // Parse URL to get host and port
        auto parser = PeoplesUrlParser::parseURL(path);
        if (parser && parser->scheme == "nfs") {
            uint16_t nfs_port = parser->port.empty() ? 2049 : std::stoi(parser->port);
            _session = SessionBroker::obtain<NFSMSession>(parser->host, nfs_port);
        }
    }
    ~NFSMStream() override {
        close();
        // Don't destroy export context - it's owned and cached by NFSMSession
        _export_context = nullptr;
    }

    // MStream methods
    bool isOpen() override;
    // bool isBrowsable() override { return false; };
    // bool isRandomAccess() override { return false; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    virtual bool seek(uint32_t pos) override;
    virtual bool seek(uint32_t pos, int mode) override;    

    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }

protected:
    std::shared_ptr<NFSMSession> _session;
    struct nfsfh *_handle = nullptr;
    std::string _export;  // Store the export path for context selection
    struct nfs_context* _export_context = nullptr;  // Export-specific context owned by this stream

    struct nfs_context* getNFS() { 
        if (_export_context) {
            return _export_context;  // Use our own export context
        }
        if (!_session) return nullptr;
        return _session->getContext();  // Fall back to root context
    }
};


/********************************************************
 * MFileSystem
 ********************************************************/

class NFSMFileSystem: public MFileSystem
{
public:
    NFSMFileSystem(): MFileSystem("nfs") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"nfs:", false) )
            return true;

        return false;
    }

    MFile* getFile(std::string path) override {
        return new NFSMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_NFS
