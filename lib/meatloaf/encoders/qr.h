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

// ML:// - Meatloaf Server Protocol
// 


#ifndef MEATLOAF_SCHEME_QR
#define MEATLOAF_SCHEME_QR

#include "qrmanager.h"


/********************************************************
 * MFileSystem
 ********************************************************/

class QRMFileSystem: public MFileSystem
{
    MFile* getFile(std::string path) override {
        return nullptr;
    }

public:
    QRMFileSystem(): MFileSystem("qrcode") {};

    bool handles(std::string name) {
        return mstr::startsWith(name, (char *)"qr:", false);
    }
};


 /********************************************************
  * MFile
  ********************************************************/
 
 class QRMFile: public MFile
 {
 friend class QRMStream;
 
 public:
     std::string basepath = "";
     
     QRMFile(std::string path): MFile(path) {

        // Generate QRCode data based on path
 
         isWritable = false;
         Debug_printv("path[%s]", this->path.c_str());
     };

     MStream* getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
     MStream* getDecodedStream(std::shared_ptr<MStream> src);
 };

 
 /********************************************************
  * MStream I
  ********************************************************/
 
 class QRMStream: public MStream {
 public:
     QRMStream(std::string& path, std::ios_base::openmode m) {
         localPath = path;
         mode = m;
         handle = std::make_unique<FlashHandle>();
         //url = path;
     }
     ~QRMStream() override {
         close();
     }
 
     // MStream methods
     bool isOpen() override;
     bool isBrowsable() override { return false; };
     bool isRandomAccess() override { return false; };
 
     bool open(std::ios_base::openmode mode) override { return true; };
     void close() override {};
 
     uint32_t read(uint8_t* buf, uint32_t size) override;
 
     virtual bool seek(uint32_t pos) override;
 
     virtual bool seekPath(std::string path) override {
         return true;
     }
 };


#endif // MEATLOAF_SCHEME_QR