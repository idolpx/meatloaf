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

#include "csip.h"

#include "meatloaf.h"

#include "make_unique.h"

/********************************************************
 * MSession implementations
 ********************************************************/

CSIPMSession::CSIPMSession(std::string host, uint16_t port)
    : MSession("csip://" + host + ":" + std::to_string(port), host, port),
      std::iostream(&buf), buf(host, port), currentDir("csip:/")
{
    Debug_printv("CSIPMSession created for %s:%d", host.c_str(), port);
}

CSIPMSession::~CSIPMSession() {
    Debug_printv("CSIPMSession destroyed for %s:%d", host.c_str(), port);
    disconnect();
}

bool CSIPMSession::connect() {
    if (connected) {
        Debug_printv("Already connected to %s:%d", host.c_str(), port);
        return true;
    }

    if (!buf.is_open()) {
        currentDir = "csip:/";
        if (!buf.open()) {
            Debug_printv("Failed to open connection to %s:%d", host.c_str(), port);
            connected = false;
            return false;
        }
    }

    Debug_printv("Successfully connected to %s:%d", host.c_str(), port);
    // If username and password were provided in the URL, login
    if ( !user.empty() && !password.empty() ) {
        sendCommand("user " + user + ", " + password);
    }

    connected = true;
    updateActivity();
    return true;
}

void CSIPMSession::disconnect() {
    if (!connected) {
        return;
    }

    Debug_printv("Disconnecting from %s:%d", host.c_str(), port);
    sendCommand("quit");
    buf.close();
    connected = false;
}

bool CSIPMSession::keep_alive() {
    if (!connected || !buf.is_open()) {
        return false;
    }

    // Send a lightweight command and consume its response to keep the stream in sync
    if (sendCommand("cf /")) {
        bool ok = isOK();
        if (ok) {
            updateActivity();
            return true;
        }
    }

    Debug_printv("Keep-alive failed for %s:%d", host.c_str(), port);
    connected = false;
    return false;
}

bool CSIPMSession::establish() {
    if(!buf.is_open()) {
        currentDir = "csip:/";
        buf.open();
    }

    return buf.is_open();
}

std::string CSIPMSession::readLn() {
    char buffer[80];
    std::string line;
    // telnet line ends with 10;
    for (int attempt = 0; attempt < 3; ++attempt) {
        getline(buffer, 80, 10);
        line = buffer;
        if (!line.empty()) {
            break;
        }
        fnSystem.delay(50);
    }

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    if (!line.empty()) {
        updateActivity();
    } else {
        line = '\x04';
    }

    //Debug_printv("Inside readln got: '%s'", buffer);
    return line;
}

bool CSIPMSession::sendCommand(const std::string& command) {
    std::string c = mstr::toPETSCII2(command);
    // 13 (CR) sends the command
    if(establish()) {
        Debug_printv("command[%s]", c.c_str());
        (*this) << (c+'\r');
        (*this).flush();
        (*this).sync();
        updateActivity();
        sleep(1);
        return true;
    }
    else
        return false;
}

bool CSIPMSession::isOK() {
    auto trimReply = [](const std::string& line) {
        size_t start = 0;
        while (start < line.size()) {
            char c = line[start];
            if (c != ' ' && c != '\t' && c != '\r' && c != '>') {
                break;
            }
            ++start;
        }
        size_t end = line.size();
        while (end > start) {
            char c = line[end - 1];
            if (c != ' ' && c != '\t' && c != '\r') {
                break;
            }
            --end;
        }
        return line.substr(start, end - start);
    };

    std::string reply;
    for (int attempt = 0; attempt < 10; ++attempt) {
        reply = readLn();
        if (reply.size() == 1 && reply[0] == '\x04') {
            continue;
        }

        auto trimmed = trimReply(reply);
        if (trimmed.empty()) {
            continue;
        }

        if (trimmed.find("00 - OK") != std::string::npos) {
            Debug_printv("ok[%s] equals[0]", reply.c_str());
            return true;
        }

        if (trimmed[0] == '?' || trimmed.find("ERROR") != std::string::npos) {
            Debug_printv("ok[%s] equals[1]", reply.c_str());
            return false;
        }

        Debug_printv("ok[%s] equals[1]", reply.c_str());
        return false;
    }

    Debug_printv("ok[] equals[1]");
    return false;
}

bool CSIPMSession::traversePath(std::string path) {
    // tricky. First we have to
    // CF / - to go back to root

    Debug_printv("Traversing path: path[%s]", path.c_str());

    if(mstr::endsWith(path, ".d64", false))
    {
        // Already mounted? Skip INSERT
        if (mstr::equals(currentDir, path, false)) {
            Debug_printv("D64 already inserted: [%s]", path.c_str());
            return true;
        }

        // THEN we have to mount the image INSERT image_name
        sendCommand("insert " + path);

        // disk image is the end, so return
        if(isOK()) {
            currentDir = path;
            return true;
        }
        else {
            // or: ?500 - DISK NOT FOUND.
            return false;
        }
    }

    // CF xxx - to browse into subsequent dirs
    sendCommand("cf " + path);
    if(!isOK()) {
        // or: ?500 - CANNOT CHANGE TO dupa
        return false;
    }

    return true;
}

/********************************************************
 * MFile implementations
 ********************************************************/

CSIPMFile::CSIPMFile(std::string path, size_t filesize): MFile(path) {
    //Debug_printv("path[%s] size[%d]", path.c_str(), filesize);
    this->size = filesize;

    media_blocks_free = 65535;

    // Obtain or create CSIP session via SessionBroker
    // CSIP always uses commodoreserver.com:1541
    std::string sessionKey = "csip://commodoreserver.com:1541";
    _session = SessionBroker::find<CSIPMSession>(sessionKey);
    if (!_session) {
        _session = std::make_shared<CSIPMSession>("commodoreserver.com", 1541);
        if (_session->connect()) {
            SessionBroker::add(sessionKey, _session);
            _session->user = user;
            _session->password = password;
        } else {
            _session.reset();
        }
    }

    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain CSIP session");
        m_isNull = true;
        return;
    }

    isPETSCII = true;
    m_isNull = false;
}

CSIPMFile::~CSIPMFile() {
    // Session is managed by SessionBroker, don't disconnect here
    if (dirHoldsIo && _session) {
        _session->releaseIO();
        dirHoldsIo = false;
    }
    _session.reset();
}

/********************************************************
 * MStream implementations
 ********************************************************/


void CSIPMStream::close() {
    _is_open = false;
    if (_session) {
        if (_holds_io) {
            _session->releaseIO();
            _holds_io = false;
        }
        _session.reset();
    }
}

bool CSIPMStream::open(std::ios_base::openmode mode) {
    _is_open = false;

    // Obtain or create CSIP session via SessionBroker
    // CSIP always uses commodoreserver.com:1541
    std::string sessionKey = "csip://commodoreserver.com:1541";
    _session = SessionBroker::find<CSIPMSession>(sessionKey);
    if (!_session) {
        _session = std::make_shared<CSIPMSession>("commodoreserver.com", 1541);
        if (_session->connect()) {
            SessionBroker::add(sessionKey, _session);
        } else {
            _session.reset();
        }
    }

    if (!_session || !_session->isConnected()) {
        Debug_printv("Failed to obtain CSIP session for %s", url.c_str());
        return false;
    }

    _session->acquireIO();
    _holds_io = true;

    auto parser = PeoplesUrlParser::parseURL(url);
    std::string full_path = parser->path;
    if (parser->name.length()) {
        std::string suffix = "/" + parser->name;
        if (!mstr::endsWith(full_path, suffix.c_str(), false)) {
            full_path += suffix;
        }
    }

    // should we allow loading of * in any directory?
    // then we can LOAD and get available count from first 2 bytes in (LH) endian
    // name here MUST BE UPPER CASE
    // trim spaces from right of name too
    mstr::rtrimA0(full_path);

    // Check if path goes through a D64 container image - must INSERT it first
    std::string load_target = full_path;
    std::string lower_path = full_path;
    for (auto& c : lower_path) c = tolower(c);
    size_t d64_sep = lower_path.find(".d64/");
    if (d64_sep != std::string::npos) {
        std::string container_path = full_path.substr(0, d64_sep + 4);
        load_target = full_path.substr(d64_sep + 5);
        if (!_session->traversePath(container_path)) {
            Debug_printv("CSIP: failed to mount image [%s]", container_path.c_str());
            if (_holds_io) {
                _session->releaseIO();
                _holds_io = false;
            }
            return false;
        }
    }

    _session->sendCommand("load " + load_target);
    // read first 2 bytes with size, low first, but may also reply with: ?500 - ERROR
    uint8_t buffer[2] = { 0, 0 };
    read(buffer, 2);
    // hmmm... should we check if they're "?5" for error?!
    if(buffer[0]=='?' && buffer[1]=='5') {
        Debug_printv("CSIP: open file failed");
        _session->readLn();
        _is_open = false;
    }
    else {
        _size = buffer[0] + buffer[1]*256; // put len here
        _position = 0;
        // if everything was ok
        printf("CSIP: file open, size: %lu\r\n", _size);
        _is_open = true;
    }

    if (!_is_open && _holds_io) {
        _session->releaseIO();
        _holds_io = false;
    }

    return _is_open;
}

// MStream methods

uint32_t CSIPMStream::write(const uint8_t *buf, uint32_t size) {
    return -1;
}

uint32_t CSIPMStream::read(uint8_t* buf, uint32_t size)  {
    if (!_session || !_session->isConnected()) {
        return 0;
    }

    // Don't read past end of file
    if (_size > 0 && _position >= _size) {
        return 0;
    }
    if (_size > 0 && size > _size - _position) {
        size = _size - _position;
    }

    uint32_t bytesRead = _session->receive(buf, size);
    _position += bytesRead;

    //Debug_printv("size[%lu] bytesRead[%lu] _position[%lu] _size[%lu]", size, bytesRead, _position, _size);

    return bytesRead;
}

bool CSIPMStream::isOpen() {
    return _is_open && _session && _session->isConnected();
}


/********************************************************
 * File impls
 ********************************************************/

bool CSIPMFile::isDirectory() {
    if (is_dir > -1) return is_dir;
    // if penultimate part is .d64 - it is a file
    // otherwise - false

    //Debug_printv("trying to chop [%s]", path.c_str());

    auto chopped = mstr::split(path,'/');

    if(path.empty()) {
        // rood dir is a dir
        return true;
    }
    if(chopped.size() == 1) {
        // we might be in an image in the root
        return mstr::endsWith((chopped[0]), ".d64", false);
    }
    if(chopped.size()>1) {
        auto second = chopped.end()-2;
        
        //auto x = (*second);
        // Debug_printv("isDirectory second from right: [%s]", (*second).c_str());
        if ( mstr::endsWith((*second), ".d64", false))
            return false;
        else
            return true;
    }
    return false;
};

std::shared_ptr<MStream> CSIPMFile::getSourceStream(std::ios_base::openmode mode) {
    std::shared_ptr<MStream> istream = std::make_shared<CSIPMStream>(url);
    //auto istream = StreamBroker::obtain<CSIPMStream>(url, mode);
    istream->open(mode);   
    return istream;
};



std::shared_ptr<MStream> CSIPMFile::createStream(std::ios_base::openmode mode)
{
    std::shared_ptr<MStream> istream = std::make_shared<CSIPMStream>(url);
    return istream;
}

bool CSIPMFile::rewindDirectory() {
    dirIsOpen = false;
    if (dirHoldsIo && _session) {
        _session->releaseIO();
        dirHoldsIo = false;
    }

    // Strip trailing slash for proper D64 detection (but preserve root "/")
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    if(!isDirectory())
        return false;

    if (!_session || !_session->isConnected()) {
        Debug_printv("No valid session available");
        return false;
    }

    //Debug_printv("pre traverse path");

    if(!_session->traversePath(path)) return false;

    //Debug_printv("post traverse path");

    if(mstr::endsWith(path, ".d64", false))
    {
        dirIsImage = true;
        // to list image contents we have to run
        Debug_printv("cserver: this is a d64 img, sending $ command!");
        _session->sendCommand("$");
        auto line = _session->readLn(); // mounted image name
        if(_session->isConnected() && line.size()) {
            dirIsOpen = true;
            if (!dirHoldsIo) {
                _session->acquireIO();
                dirHoldsIo = true;
            }
            if (line.size() > 5) {
                media_image = line.substr(5);
            } else {
                media_image.clear();
            }
            line = _session->readLn(); // dir header
            auto last_quote = line.find_last_of("\"");
            if (last_quote != std::string::npos && last_quote >= 2) {
                media_header = line.substr(2, last_quote - 1);
                if (last_quote + 2 <= line.size()) {
                    media_id = line.substr(last_quote + 2);
                } else {
                    media_id.clear();
                }
            } else {
                media_header.clear();
                media_id.clear();
            }
            return true;
        }
        else
            return false;
    }
    else
    {
        dirIsImage = false;
        // to list directory contents we use
        Debug_printv("cserver: this is a directory!");
        _session->sendCommand("disks");
        auto line = _session->readLn(); // dir header
        //Debug_printv("line[%s]", line.c_str());
        if(_session->isConnected() && line.size()) {
            auto last_bracket = line.find_last_of("]");
            if (last_bracket != std::string::npos && last_bracket >= 2) {
                media_header = line.substr(2, last_bracket - 1);
            } else {
                media_header.clear();
            }
            media_id = "C=SVR";
            dirIsOpen = true;
            if (!dirHoldsIo) {
                _session->acquireIO();
                dirHoldsIo = true;
            }

            return true;
        }
        else
            return false;
    }
}

MFile* CSIPMFile::getNextFileInDir() {

    //Debug_printv("pre rewind");

    if(!dirIsOpen)
        rewindDirectory();

    //Debug_printv("pre dir is open");

    if(!dirIsOpen)
        return nullptr;

    if (!_session || !_session->isConnected()) {
        return nullptr;
    }

    std::string name;
    size_t size;
    std::string new_url = url;

    if(url.size()>8) // If we are not at root then add additional "/"
        new_url += "/";

    //Debug_printv("pre dir is image");

    auto line = _session->readLn();
    //Debug_printv("line[%s]", line.c_str());

    if(dirIsImage) {
        //Debug_printv("next file in dir got %s", line.c_str());
        // 'ot line:'0 ␒"CIE�������������" 00�2A�
        // 'ot line:'2   "CIE+SERIAL      " PRG   2049
        // 'ot line:'1   "CIE-SYS31801    " PRG   2049
        // 'ot line:'1   "CIE-SYS31801S   " PRG   2049
        // 'ot line:'1   "CIE-SYS52281    " PRG   2049
        // 'ot line:'1   "CIE-SYS52281S   " PRG   2049
        // 'ot line:'658 BLOCKS FREE.

        if(line.find('\x04')!=std::string::npos) {
            Debug_printv("No more!");
            dirIsOpen = false;
            if (dirHoldsIo && _session) {
                _session->releaseIO();
                dirHoldsIo = false;
            }
            return nullptr;
        }
        if(line.find("BLOCKS FREE.")!=std::string::npos) {
            media_blocks_free = atoi(line.substr(0, line.find_first_of(" ")).c_str());
            dirIsOpen = false;
            if (dirHoldsIo && _session) {
                _session->releaseIO();
                dirHoldsIo = false;
            }
            return nullptr;
        }
        else {
            name = line.substr(5,16);
            size = atoi(line.substr(0, line.find_first_of(" ")).c_str());
            mstr::rtrim(name);
            mstr::replaceAll(name, "/", "\\");
            //Debug_printv("xx: %s -- %s %d", line.c_str(), name.c_str(), size);
            //return new CSIPMFile(path() +"/"+ name);
            new_url += name;
            return new CSIPMFile(new_url, size);
        }
    } else {
        // Got line:''
        // Got line:''
        // 'ot line:'FAST-TESTER DELUXE EXCESS.D64
        // 'ot line:'EMPTY.D64
        // 'ot line:'CMD UTILITIES D1.D64
        // 'ot line:'CBMCMD22.D64
        // 'ot line:'NAV96.D64
        // 'ot line:'NAV92.D64
        // 'ot line:'SINGLE DISKCOPY 64 (1983)(KEVIN PICKELL).D64
        // 'ot line:'LYNX (19XX)(-).D64
        // 'ot line:'GEOS DISK EDITOR (1990)(GREG BADROS).D64
        // 'ot line:'FLOPPY REPAIR KIT (1984)(ORCHID SOFTWARE LABORATOR
        // 'ot line:'1541 DEMO DISK (19XX)(-).D64

        // 32 62 91 68 73 83 75 32 84 79 79 76 83 93 13 No more! = > [DISK TOOLS]

        //Debug_printv("line[%s]", line.c_str());
        if(line.find('\x04')!=std::string::npos) {
            Debug_printv("No more!");
            dirIsOpen = false;
            if (dirHoldsIo && _session) {
                _session->releaseIO();
                dirHoldsIo = false;
            }
            return nullptr;
        }
        else {

            if((*line.begin())=='[') {
                name = line.substr(1,line.length()-2);
                size = 0;
            }
            else {
                name = line;
                size = (683 * 256);
            }
            mstr::rtrim(name);
            //Debug_printv("url[%s] name[%s] size[%d]", url.c_str(), name.c_str(), size);
            if(name.size() > 0)
            {
                new_url += name;
                //Debug_printv("new_url[%s]", new_url.c_str());
                return new CSIPMFile(new_url, size);
            }
            else
                return nullptr;
        }
    }
};

bool CSIPMFile::exists() {
    return true;
} ;

// uint32_t CSIPMFile::size() {
//     return m_size;
// };

bool CSIPMFile::mkDir() { 
    // but it does support creating dirs = MD FOLDER
    return false; 
};

bool CSIPMFile::remove() { 
    // but it does support remove = SCRATCH FILENAME
    return false; 
};

 