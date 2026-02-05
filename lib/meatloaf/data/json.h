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

// JSON: - JSON (Javascript Object Notation) Data Reader
// 
// https://datatracker.ietf.org/doc/html/rfc6901
//

#ifndef MEATLOAF_DATA_JSON
#define MEATLOAF_DATA_JSON

#include <string>
#include <vector>
#include <json.hpp>
#include "meatloaf.h"
#include "utils.h"

using json = nlohmann::json;

/********************************************************
 * MStream I
 ********************************************************/

class JSONMStream: public MStream 
{
    json _data = {};
    std::vector<std::string> _values = {};
    uint8_t _index = 0;
    std::string _receive = {};

    uint32_t generate(std::string data)
    {
        _data = json::parse(data);

        Debug_printv("json data[%s]", data.c_str());

        return data.size();
    }

public:
    JSONMStream(std::string &path, std::ios_base::openmode m): MStream(path) {
        //url = path;
        path = mstr::drop(path, 5); // drop "JSON:"
        _size = generate(path);
    }

     // MStream methods
    bool isOpen() override { return true; };

    bool open(std::ios_base::openmode mode) override { return true; };
    void close() override {};

    uint32_t read(uint8_t* buf, uint32_t size) override
    {
        Debug_printv("position[%d] size[%d]", _position, size);
        if (size > (_size - _position)) {
            size = _size - _position;
        }
        if (size < 1)
            return 0;

        memcpy(buf, (_values[_index].data() + _position), size);
        _position += size;
        _index++;
        Debug_printv("index[%d] position[%d] size[%d]", _index, _position, size);

        return size;
    };
     uint32_t write(const uint8_t *buf, uint32_t size) override 
    {
        char *s = reinterpret_cast<char*>(const_cast<uint8_t*>(buf));
        _position = 0;
        _index = 0;

        std::string selector = std::string(s, size);

        // Is this new json data or a selector?
        if ( mstr::startsWith(selector, "{") || mstr::startsWith(selector,"[") || _receive.size() )
        {   
            if ( _receive.size() )
            {
                // continuing previous data
                _receive += std::string(s, size);
            }
            else
            {
                // starting new data
                _receive = std::string(s, size);
            }

            if ( mstr::endsWith(_receive, "}") || mstr::endsWith(_receive, "]") )
            {
                // not complete yet
                return size;
            }
            // New data, parse it and be ready to serve values
            generate(_receive);
            _receive = {};
        }
        else
        {
            if ( selector == "\n" || selector == "\r\n" )
            {
                // Read next line from source stream and parse
            }
            auto value = _data[selector];
            if ( value.is_string() )
                _values.push_back( value.get<std::string>() );
            else
                _values.push_back( value.dump() );
        }

        return size;
    };

    bool seek(uint32_t pos) override { return position(pos); };
};


/********************************************************
 * MFile
 ********************************************************/

class JSONMFile: public MFile
{
public:
    
    JSONMFile(std::string path): MFile(path)
    {
        Debug_printv("path[%s]", path.c_str());
    };

    bool isDirectory() override { return false; };

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override 
    {
        std::shared_ptr<MStream> istream = std::make_shared<JSONMStream>(url, mode);
        size = istream->size();
        return istream;
    };
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override { return is; };
};


/********************************************************
 * MFileSystem
 ********************************************************/

class JSONMFileSystem: public MFileSystem
{
public:
    JSONMFileSystem(): MFileSystem("json") {};

    bool handles(std::string name) {
        return mstr::startsWith(name, (char *)"json:", false);
    }

    MFile* getFile(std::string path) override {
        return new JSONMFile(path);
    }
};

#endif // MEATLOAF_DATA_JSON