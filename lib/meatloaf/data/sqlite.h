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

// QR:// - QR Code Generator
// 


#ifndef MEATLOAF_SCHEME_HASH
#define MEATLOAF_SCHEME_HASH

#include <string>
#include <vector>

#include "meatloaf.h"

#include "sqlite3.h"
#include "utils.h"


 /********************************************************
  * MStream I
  ********************************************************/
 
 class SQLiteMStream: public MStream 
 {
    std::string hash;

    uint32_t generate(std::string data)
    {
        std::string algorithm = "MD5"; // MD5, SHA1, SHA224, SHA256, SHA384, SHA512
        std::string key; // If specified HMAC Message Digest is returned instead
        auto d = util_tokenize(data, '/');
        if ( d.size() > 1 ) {
            algorithm = d[0];
            mstr::toUpper(algorithm);

            if ( d.size() > 2) {
                key = d[1].c_str();
                data = d[2];
            }
            else
            {
                key.clear();
                key.shrink_to_fit();
                data = d[1];
            }
        }

        Debug_printv("hash algoritm[%s] key[%s] data[%s]", algorithm.c_str(), key.c_str(), data.c_str());
        hash.clear();
        hash.shrink_to_fit();
        hasher.key = key;
        hasher.add_data(data);
        hasher.compute(hasher.from_string(algorithm), true);

        hash = hasher.output_hex();
        mstr::toUpper(hash);
        Debug_printf("hash[%s]\r\n", hash.c_str());

        return hash.size();
    }

 public:
     SQLiteMStream(std::string &path, std::ios_base::openmode m) {
        //url = path;
        path = mstr::drop(path, 6); // drop "HASH:/"
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

        memcpy(buf, (hash.data() + _position), size);
        _position += size;
        Debug_printv("position[%d] size[%d]", _position, size);

        return size;
     };
     uint32_t write(const uint8_t *buf, uint32_t size) override 
     {
        char *s = reinterpret_cast<char*>(const_cast<uint8_t*>(buf));
        _position = 0;
        return generate(std::string(s, size));
    };
 
    bool seek(uint32_t pos) override { return position(pos); };
 };


 /********************************************************
  * MFile
  ********************************************************/
 
class SQLiteMFile: public MFile
{
public:
    
    SQLiteMFile(std::string path): MFile(path)
    {
        Debug_printv("path[%s]", this->path.c_str());
    };

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override 
    {
        std::shared_ptr<MStream> istream = std::make_shared<SQLiteMStream>(url, mode);
        size = istream->size();
        return istream;
    };
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override { return is; };
 };

 
/********************************************************
 * MFileSystem
 ********************************************************/

class SQLiteMFileSystem: public MFileSystem
{
public:
    SQLiteMFileSystem(): MFileSystem("sqlite") {};

    bool handles(std::string name) {
        return mstr::startsWith(name, (char *)"sqlite:", false);
    }

    MFile* getFile(std::string path) override {
        return new SQLiteMFile(path);
    }
};

#endif // MEATLOAF_SCHEME_HASH