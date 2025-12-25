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

//
// SMB:// - Server Messagee Block Protocol
// https://en.wikipedia.org/wiki/Server_Message_Block
//

#ifndef MEATLOAF_DEVICE_SMB
#define MEATLOAF_DEVICE_SMB

#include "meatloaf.h"

#include <smb2.h>
#include <libsmb2.h>
#include <libsmb2-raw.h>
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
bool parseSMBPath(const std::string& path, std::string& share, std::string& share_path);

/********************************************************
 * SMBMSession - SMB Session Management
 ********************************************************/

class SMBMSession : public MSession {
public:
    SMBMSession(std::string host, uint16_t port = 445) : MSession(host, port) {
        Debug_printv("SMBMSession created for %s:%d", host.c_str(), port);
    }
    ~SMBMSession() override {
        Debug_printv("SMBMSession destroyed for %s:%d", host.c_str(), port);
        disconnect();
    }

    // Set credentials for this session
    void setCredentials(const std::string& user, const std::string& password) {
        _user = user;
        _password = password;
    }

    bool connect() override {
        if (connected) return true;
        
        // Initialize SMB context for IPC$ (share enumeration)
        _smb = smb2_init_context();
        if (!_smb) {
            Debug_printv("Failed to initialize SMB context for %s:%d", host.c_str(), port);
            connected = false;
            return false;
        }

        // Set SMB2 version
        smb2_set_version(_smb, SMB2_VERSION_ANY);

        // Set credentials if available
        if (!_password.empty()) {
            smb2_set_password(_smb, _password.c_str());
        }

        // Try to connect to the server
        // We connect to IPC$ share for session-only purposes (listing shares)
        const char* user_ptr = _user.empty() ? nullptr : _user.c_str();
        if (smb2_connect_share(_smb, host.c_str(), "IPC$", user_ptr) < 0) {
            Debug_printv("Failed to connect to %s:%d: %s", host.c_str(), port, smb2_get_error(_smb));
            if (_smb) {
                smb2_destroy_context(_smb);
                _smb = nullptr;
            }
            connected = false;
            return false;
        }

        connected = true;
        updateActivity();
        Debug_printv("Connected to SMB server at %s:%d", host.c_str(), port);
        return true;
    }

    void disconnect() override {
        if (!connected) return;
        
        if (_smb) {
            smb2_destroy_context(_smb);
            _smb = nullptr;
        }
        
        // Disconnect all share contexts
        for (auto& pair : _share_contexts) {
            if (pair.second) {
                smb2_destroy_context(pair.second);
            }
        }
        _share_contexts.clear();
        
        connected = false;
        Debug_printv("Disconnected from SMB server at %s:%d", host.c_str(), port);
    }

    bool keep_alive() override {
        if (!connected || !_smb) return false;
        
        // For SMB, we can check connection by attempting a simple echo operation
        // or by checking the connection state
        // For now, we'll consider the connection alive if smb context exists
        updateActivity();
        return true;
    }

    struct smb2_context* getContext() { return _smb; }
    
    // Get or create a context for a specific share
    struct smb2_context* getShareContext(const std::string& share) {
        if (share.empty() || share == "IPC$") {
            return _smb;  // Use IPC$ context for share enumeration
        }
        
        // Check if we already have a context for this share
        auto it = _share_contexts.find(share);
        if (it != _share_contexts.end() && it->second != nullptr) {
            //Debug_printv("Reusing existing context for share: %s", share.c_str());
            return it->second;
        }
        
        // Create a new context for this share
        Debug_printv("Creating new context for share: %s with user: %s", share.c_str(), _user.c_str());
        struct smb2_context* smb = smb2_init_context();
        if (!smb) {
            Debug_printv("Failed to initialize SMB context for share %s", share.c_str());
            return nullptr;
        }
        
        smb2_set_version(smb, SMB2_VERSION_ANY);
        
        // Set credentials for this share connection
        if (!_password.empty()) {
            smb2_set_password(smb, _password.c_str());
        }
        
        const char* user_ptr = _user.empty() ? nullptr : _user.c_str();
        if (smb2_connect_share(smb, host.c_str(), share.c_str(), user_ptr) < 0) {
            Debug_printv("Failed to connect to share %s: %s", share.c_str(), smb2_get_error(smb));
            smb2_destroy_context(smb);
            return nullptr;
        }
        
        _share_contexts[share] = smb;
        return smb;
    }

private:
    std::string _user;
    std::string _password;
    struct smb2_context* _smb = nullptr;
    std::map<std::string, struct smb2_context*> _share_contexts;  // Per-share contexts
};

/********************************************************
 * MFile
 ********************************************************/

class SMBMFile: public MFile
{
public:
    std::string basepath = "";
    std::string share = "";
    std::string share_path = "";
    
    SMBMFile(std::string path): MFile(path) {

        // Obtain or create SMB session via SessionBroker
        uint16_t smb_port = port.empty() ? 445 : std::stoi(port);
        _session = SessionBroker::obtain<SMBMSession>(host, smb_port);

        if (!_session || !_session->isConnected()) {
            Debug_printv("Failed to obtain SMB session for %s:%d", host.c_str(), smb_port);
            m_isNull = true;
            return;
        }

        // Set credentials on the session if available
        if (!user.empty() || !password.empty()) {
            _session->setCredentials(user, password);
        }

        // extract share from path
        parseSMBPath(this->path, share, share_path);
        Debug_printv("path[%s] share[%s] share_path[%s]", this->path.c_str(), share.c_str(), share_path.c_str());

        // If no share specified, enumerate available shares
        if (share.empty()) {
            Debug_printv("ENUMERATE path[%s] share[%s] share_path[%s]", this->path.c_str(), share.c_str(), share_path.c_str());
            auto smb = getSMB();
            if (smb) {
                // Clear previous share list
                shares.clear();
                is_finished = 0;
                
                struct pollfd pfd;
                if (smb2_share_enum_async(smb, share_enumerate_cb, NULL) != 0) {
                    Debug_printv("smb2_share_enum failed: %s", smb2_get_error(smb));
                } else {
                    // Wait for enumeration to complete
                    while (!is_finished) {
                        pfd.fd = smb2_get_fd(smb);
                        pfd.events = smb2_which_events(smb);

                        if (poll(&pfd, 1, 1000) < 0) {
                            Debug_printv("Poll failed");
                            break;
                        }
                        if (pfd.revents == 0) {
                            continue;
                        }
                        if (smb2_service(smb, pfd.revents) < 0) {
                            Debug_printv("smb2_service failed: %s", smb2_get_error(smb));
                            break;
                        }
                    }
                }
            }
        }

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry( name );

        if (!pathValid(this->path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        Debug_printv("SMB path[%s] valid[%d]", this->path.c_str(), !m_isNull);
    };
    ~SMBMFile() {
        closeDir();
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
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

    std::shared_ptr<SMBMSession> _session;
    struct smb2dir *_handle_dir = nullptr;

    // Helper to get SMB context from session - use share-specific context if available
    struct smb2_context* getSMB() { 
        if (!_session) return nullptr;
        if (!share.empty()) {
            return _session->getShareContext(share);
        }
        return _session->getContext();
    }

private:
    virtual void openDir(std::string path);
    virtual void closeDir();

    bool pathValid(std::string path);

    int entry_index;
    static int is_finished;
    static std::vector<std::string> shares;
    static void share_enumerate_cb(struct smb2_context *smb2, int status, void *command_data, void *private_data);
};


/********************************************************
 * SMBHandle
 ********************************************************/

class SMBHandle {
public:
    SMBHandle() : _session(nullptr), _handle(nullptr) {
        //Debug_printv("*** Creating SMB handle");
    };
    ~SMBHandle();
    void obtain(std::string localPath, int smb_mode);
    void dispose();

    struct smb2_context* getSMB() { return _session ? _session->getContext() : nullptr; }

private:
    std::shared_ptr<SMBMSession> _session;
    struct smb2fh *_handle = nullptr;
    int flags = 0;
};


/********************************************************
 * MStream I
 ********************************************************/

class SMBMStream: public MStream {
public:
    SMBMStream(std::string& path): MStream(path) {
        // Obtain or create SMB session via SessionBroker
        // Parse URL to get host and port
        auto parser = PeoplesUrlParser::parseURL(path);
        if (parser && parser->scheme == "smb") {
            uint16_t smb_port = parser->port.empty() ? 445 : std::stoi(parser->port);
            _session = SessionBroker::obtain<SMBMSession>(parser->host, smb_port);
        }
    }
    ~SMBMStream() override {
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

    virtual bool seek(uint32_t pos) override;
    virtual bool seek(uint32_t pos, int mode) override;    

    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }


protected:
    std::shared_ptr<SMBMSession> _session;
    struct smb2fh *_handle = nullptr;
    std::string _share;  // Store the share name for context selection

    struct smb2_context* getSMB() { 
        if (!_session) return nullptr;
        // Use the share-specific context if we have a share name
        if (!_share.empty()) {
            return _session->getShareContext(_share);
        }
        return _session->getContext();
    }
};


/********************************************************
 * MFileSystem
 ********************************************************/

class SMBMFileSystem: public MFileSystem
{
public:
    SMBMFileSystem(): MFileSystem("smb") {
        isRootFS = true;
    };

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"smb:", false) )
            return true;

        return false;
    }

    MFile* getFile(std::string path) override {
        return new SMBMFile(path);
    }
};


#endif // MEATLOAF_DEVICE_SMB
