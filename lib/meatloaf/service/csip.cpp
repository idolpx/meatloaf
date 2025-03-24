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
 * Client impls
 ********************************************************/
// fajna sciezka do sprawdzenia:
// utilities/disk tools/cie.d64

CSIPMSessionMgr CSIPMFileSystem::session;

bool CSIPMSessionMgr::establishSession() {
    if(!buf.is_open()) {
        currentDir = "csip:/";
        buf.open();
    }
    
    return buf.is_open();
}

std::string CSIPMSessionMgr::readLn() {
    char buffer[80];
    std::string line;
    // telnet line ends with 10;
    getline(buffer, 80, 10);
    line = buffer;
    // if (line.empty()) {
    //     usleep(150000000); // 1.5 sec delay if no data
    //     getline(buffer, 80, 10);
    // }
    // line = buffer;
    if (line.empty()) {
        line = '\x04';
    }

    //Debug_printv("Inside readln got: '%s'", buffer);
    return line;
}

bool CSIPMSessionMgr::sendCommand(std::string command) {
    std::string c = mstr::toPETSCII2(command);
    // 13 (CR) sends the command
    if(establishSession()) {
        Debug_printv("command[%s]", c.c_str());
        (*this) << (c+'\r');
        (*this).flush();
        (*this).sync();
        sleep(1);
        return true;
    }
    else
        return false;
}

bool CSIPMSessionMgr::isOK() {
    // auto a = readLn();

    auto reply = readLn();
    // for(int i = 0 ; i<reply.length(); i++)
    //     Debug_printv("'%d'", reply[i]);

    bool equals = strncmp("00 - OK\x0d", reply.c_str(), 7);

    Debug_printv("ok[%s] equals[%d]", reply.c_str(), equals);

    return equals;
}

bool CSIPMSessionMgr::traversePath(MFile* path) {
    // tricky. First we have to
    // CF / - to go back to root

    //Debug_printv("Traversing path: [%s]", path->path.c_str());

    if(buf.is_open()) {
        // if we are still connected we can smart change dir by just going up or down
        // but for time being, we stick to traversing from root
        if(!sendCommand("cf /"))
            return false;
    }
    else {
        // if we aren't, change dir to root (alos connects the session);
        if(!sendCommand("cf /"))
            return false;
    }

    if(isOK()) {
        Debug_printv("path[%s]", path->path.c_str());
        if(path->path.compare("/") == 0) {
            currentDir = path->url;
            return true;
        }

        std::vector<std::string> chopped = mstr::split(path->path, '/');

        //MFile::parsePath(&chopped, path->path); - nope this doessn't work and crases in the loop!

        Debug_printv("Before loop");
        //Debug_printv("Chopped size:%d\r\n", chopped.size());
        fnSystem.delay(500);

        for(size_t i = 1; i < chopped.size(); i++) {
            //Debug_printv("Before chopped deref");

            auto part = chopped[i];
            
            //Debug_printv("traverse path part: [%s]\r\n", part.c_str());
            if(mstr::endsWith(part, ".d64", false)) 
            {
                // THEN we have to mount the image INSERT image_name
                sendCommand("insert "+part);

                // disk image is the end, so return
                if(isOK()) {
                    currentDir = path->url;
                    return true;
                }
                else {
                    // or: ?500 - DISK NOT FOUND.
                    return false;
                }
            }
            else 
            {
                // CF xxx - to browse into subsequent dirs
                sendCommand("cf "+part);
                if(!isOK()) {
                    // or: ?500 - CANNOT CHANGE TO dupa
                    return false;
                }
            }
        }
        
        currentDir = path->url;
        return true;
    }
    else
        return false; // shouldn't really happen, right?
}

/********************************************************
 * I Stream impls
 ********************************************************/


void CSIPMStream::close() {
    _is_open = false;
};

bool CSIPMStream::open(std::ios_base::openmode mode) {
    auto file = std::make_unique<CSIPMFile>(url);
    _is_open = false;

    if(file->isDirectory())
        return false; // or do we want to stream whole d64 image? :D

    if(CSIPMFileSystem::session.traversePath(file.get())) {
        // should we allow loading of * in any directory?
        // then we can LOAD and get available count from first 2 bytes in (LH) endian
        // name here MUST BE UPPER CASE
        // trim spaces from right of name too
        mstr::rtrimA0(file->name);
        //mstr::toPETSCII2(file->name);
        CSIPMFileSystem::session.sendCommand("load "+file->name);
        // read first 2 bytes with size, low first, but may also reply with: ?500 - ERROR
        uint8_t buffer[2] = { 0, 0 };
        read(buffer, 2);
        // hmmm... should we check if they're "?5" for error?!
        if(buffer[0]=='?' && buffer[1]=='5') {
            Debug_printv("CSIP: open file failed");
            CSIPMFileSystem::session.readLn();
            _is_open = false;
        }
        else {
            _size = buffer[0] + buffer[1]*256; // put len here
            // if everything was ok
            printf("CSIP: file open, size: %lu\r\n", _size);
            _is_open = true;
        }
    }

    return _is_open;
};

// MStream methods

uint32_t CSIPMStream::write(const uint8_t *buf, uint32_t size) {
    return -1;
}

uint32_t CSIPMStream::read(uint8_t* buf, uint32_t size)  {
    uint32_t bytesRead = CSIPMFileSystem::session.receive(buf, size);
    _position+=bytesRead;

    Debug_printv("size[%lu] bytesRead[%lu] _position[%lu]", size, bytesRead, _position);

    return bytesRead;
};

bool CSIPMStream::isOpen() {
    return _is_open;
}


/********************************************************
 * File impls
 ********************************************************/

bool CSIPMFile::isDirectory() {
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

MStream* CSIPMFile::getSourceStream(std::ios_base::openmode mode) {
    MStream* istream = new CSIPMStream(url);
    //auto istream = StreamBroker::obtain<CSIPMStream>(url, mode);
    istream->open(mode);   
    return istream;
};



MStream* CSIPMFile::createStream(std::ios_base::openmode mode)
{
    MStream* istream = new CSIPMStream(url);
    return istream;
}

bool CSIPMFile::rewindDirectory() {    
    dirIsOpen = false;

    if(!isDirectory())
        return false;


    //Debug_printv("pre traverse path");

    if(!CSIPMFileSystem::session.traversePath(this)) return false;

    //Debug_printv("post traverse path");

    if(mstr::endsWith(path, ".d64", false))
    {
        dirIsImage = true;
        // to list image contents we have to run
        Debug_printv("cserver: this is a d64 img, sending $ command!");
        CSIPMFileSystem::session.sendCommand("$");
        auto line = CSIPMFileSystem::session.readLn(); // mounted image name
        if(CSIPMFileSystem::session.is_open() && line.size()) {
            dirIsOpen = true;
            media_image = line.substr(5);
            line = CSIPMFileSystem::session.readLn(); // dir header
            media_header = line.substr(2, line.find_last_of("\""));
            media_id = line.substr(line.find_last_of("\"")+2);
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
        CSIPMFileSystem::session.sendCommand("disks");
        auto line = CSIPMFileSystem::session.readLn(); // dir header
        //Debug_printv("line[%s]", line.c_str());
        if(CSIPMFileSystem::session.is_open() && line.size()) {
            media_header = line.substr(2, line.find_last_of("]")-1);
            media_id = "C=SVR";
            dirIsOpen = true;

            return true;
        }
        else 
            return false;
    }
};

MFile* CSIPMFile::getNextFileInDir() {

    //Debug_printv("pre rewind");

    if(!dirIsOpen)
        rewindDirectory();

    //Debug_printv("pre dir is open");

    if(!dirIsOpen)
        return nullptr;

    std::string name;
    size_t size;
    std::string new_url = url;

    if(url.size()>8) // If we are not at root then add additional "/"
        new_url += "/";

    //Debug_printv("pre dir is image");

    if(dirIsImage) {
        auto line = CSIPMFileSystem::session.readLn();
        Debug_printv("next file in dir got %s", line.c_str());
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
            return nullptr;
        }
        if(line.find("BLOCKS FREE.")!=std::string::npos) {
            media_blocks_free = atoi(line.substr(0, line.find_first_of(" ")).c_str());
            dirIsOpen = false;
            return nullptr;
        }
        else {
            name = line.substr(5,15);
            size = atoi(line.substr(0, line.find_first_of(" ")).c_str());
            mstr::rtrim(name);
            Debug_printv("xx: %s -- %s %d", line.c_str(), name.c_str(), size);
            //return new CSIPMFile(path() +"/"+ name);
            new_url += name;
            return new CSIPMFile(new_url, size);
        }
    } else {
        auto line = CSIPMFileSystem::session.readLn();
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
            return nullptr;
        }
        else {

            if((*line.begin())=='[') {
                name = line.substr(1,line.length()-3);
                size = 0;
            }
            else {
                name = line.substr(0, line.length()-1);
                size = (683 * 256);
            }

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

 