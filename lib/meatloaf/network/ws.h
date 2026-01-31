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

// WS:// - WebSockets
// https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API
// https://en.wikipedia.org/wiki/WebSocket
//

// WSS:// - WebSockets Secure
// https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API
// https://en.wikipedia.org/wiki/WebSocket
//

#ifndef MEATLOAF_SCHEME_WS
#define MEATLOAF_SCHEME_WS

#include "meatloaf.h"
#include "../../include/global_defines.h"

#include "string_utils.h"

/********************************************************
 * Streams
 ********************************************************/

class WSMStream: public MStream {
public:
    WSMStream(MFile* file, bool isServer) : m_isServer(isServer) {
        // drop ws:// from url and it's OK!
        address = websockets::WSInterfaceString(mstr::drop(file->url, 5).c_str());
        if(!file->port.empty())
            port = std::stoi(file->port);
    }

    ~WSMStream() {
        close();
    }

    void close() override {
        client.close();        
    }

    bool open() override {
        if(m_isServer) {
            server.listen(port);
            client = server.accept();
            prepareClientCallbacks();
            _is_open = server.available();
        }
        else {
            prepareClientCallbacks();
            _is_open = client.connect(address);
            // waring - client->poll() required to keep it working!
        }

        return _is_open;
    };

    // MStream methods
    uint32_t position() override { return 0; };
    uint32_t available() override { return INT_MAX; };
    bool isOpen() { return _is_open; };
    bool seek(uint32_t pos) { return false; };
    uint32_t size() { return INT_MAX; };

    uint32_t read(uint8_t* buf, uint32_t size) override {
        //auto msg = client.readBlocking(); // we don't want to block. We'll store a message from callback in msg

        if(!_is_open)
            return 0;

        if(!lastMsg.isEmpty()) {
            auto maxLen = (size < lastMsg.length()) ? size : lastMsg.length();
            strncpy((char *)buf, lastMsg.c_str(), maxLen);
            Debug_printv("Sending last msg and polling!");
            lastMsg = "";
            client.poll();
            return maxLen;
        }
        else {
            Debug_printv("Polling!");
            client.poll();
            return 0;
        }
    }
    size_t write(const uint8_t *buf, size_t size) override {
        if(!_is_open)
            return 0;

        websockets::WSInterfaceString message((char *)buf);
        client.send(message);
        return message.length();
    }

protected:
    websockets::WSInterfaceString address; // format: www.myserver.com:8080
    uint16_t port = 80;
    bool _is_open;
    bool m_isServer;
    websockets::WebsocketsClient client;
    websockets::WebsocketsServer server;
    websockets::WSInterfaceString lastMsg;

    void prepareClientCallbacks() {
        client.onMessage([this](websockets::WebsocketsMessage msg){
            //
            // I think this get called only if you do client.poll()
            //
            Debug_printv("Got message: %s", msg.data().c_str());

            if(msg.isText()) {
                //(*msgPtr) = (std::string)msg.data();    
                this->lastMsg = msg.data();
            }
        });

        client.onEvent([this](websockets::WebsocketsEvent event, String data) {
            if(event == websockets::WebsocketsEvent::ConnectionOpened) {
                Debug_printv("Socket Connnection Opened");
                this->_is_open = true;
            } else if(event == websockets::WebsocketsEvent::ConnectionClosed) {
                Debug_printv("Socket Connnection Closed");
                this->_is_open = false;
            } else if(event == websockets::WebsocketsEvent::GotPing) {
                Debug_printv("Got a Ping!");
            } else if(event == websockets::WebsocketsEvent::GotPong) {
                Debug_printv("Got a Pong!");
            }
        });
    };
};

/********************************************************
 * File implementations
 ********************************************************/

class WSMFile: public MFile {

public:
    WSMFile(std::string path): MFile(path) {};

    bool isDirectory() override { return false; }
    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override {
        // input stream = SERVER socket
        return std::make_shared<WSMStream>(this, true);
    }; 
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };
    bool rewindDirectory() override { return false; };
    MFile* getNextFileInDir() override { return nullptr; };
    bool mkDir() override { return false; };
    bool exists() override { return false; };
    //size_t size() override { return 0; };
    bool remove() override { return false; };
    bool rename(std::string dest) { return false; };
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) {
        return nullptr;
    };

};



/********************************************************
 * FS
 ********************************************************/

class WSFileSystem: public MFileSystem 
{
public:
    WSFileSystem(): MFileSystem("ws") {};

    bool handles(std::string name) {
        std::string pattern = "ws:";
        return mstr::equals(name, pattern, false);
    }

    MFile* getFile(std::string path) override {
        return new WSMFile(path);
    }
};



#endif /* MEATLOAF_SCHEME_WS */
