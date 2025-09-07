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


#ifndef MEATLOAF_SCHEME_QRCODE
#define MEATLOAF_SCHEME_QRCODE

#include <string>
#include <vector>
#include "qrmanager.h"
#include "meatloaf.h"
#include "utils.h"


 /********************************************************
  * MStream I
  ********************************************************/
 
 class QRMStream: public MStream 
 {
    std::vector<uint8_t> qrcode;

    uint32_t generate(std::string data)
    {
        uint8_t version = 0; // default version
        uint8_t ecc = 0;     // default ecc
        auto d = util_tokenize(data, '/');
        if ( d.size() > 1 ) {
            version = atoi(d[0].c_str());

            if ( d.size() > 2) {
                ecc = atoi(d[1].c_str());
                data = d[2];
            }
            else
            {
                data = d[1];
            }
        }

        //Debug_printv("qrcode version[%d] ecc[%d] data[%s]", version, (qr_ecc_t)ecc, data.c_str());
        // uint16_t len = data.size();
        // void *b45data = malloc(len * 3);
        // void *b45data_len = malloc(sizeof(uint16_t));
        // qrcode_encodeBase45((unsigned char*)b45data, (uint16_t*)&b45data_len, (unsigned char*)data.c_str(), len);
        // data = (char*)b45data;
        // free(b45data);

        qrcode.clear();
        qrcode.shrink_to_fit();
        QRManager qrManager = QRManager(version, (qr_ecc_t)ecc, QR_OUTPUT_MODE_PETSCII);
        qrcode = qrManager.encode(data.c_str());

        //auto qr = util_hexdump(qrcode.data(), qrcode.size());
        //Debug_printf("qrcode [%d]\r\n%s\r\n", qrcode.size(), qr.c_str());

        return qrcode.size();
    }

 public:
     QRMStream(std::string &path, std::ios_base::openmode m) {
        //url = path;
        path = mstr::drop(path, 4); // drop "QR:/"
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

        memcpy(buf, (qrcode.data() + _position), size);
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
 
class QRMFile: public MFile
{
public:
    
    QRMFile(std::string path): MFile(path)
    {
        Debug_printv("path[%s]", this->path.c_str());
    };

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override 
    {
        std::shared_ptr<MStream> istream = std::make_shared<QRMStream>(url, mode);
        size = istream->size();
        return istream;
    };
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override { return is; };

    bool isDirectory() override { return false; };
    bool rewindDirectory() override { return false; };
    MFile* getNextFileInDir() override { return nullptr; };

    bool remove() override { return false; };
    bool rename(std::string dest) { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };
 };

 
/********************************************************
 * MFileSystem
 ********************************************************/

class QRMFileSystem: public MFileSystem
{
public:
    QRMFileSystem(): MFileSystem("qrcode") {};

    bool handles(std::string name) {
        return mstr::startsWith(name, (char *)"qr:", false);
    }

    MFile* getFile(std::string path) override {
        return new QRMFile(path);
    }
};

#endif // MEATLOAF_SCHEME_QRCODE