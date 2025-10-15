// // Meatloaf - A Commodore 64/128 multi-device emulator
// // https://github.com/idolpx/meatloaf
// // Copyright(C) 2020 James Johnston
// //
// // Meatloaf is free software : you can redistribute it and/or modify
// // it under the terms of the GNU General Public License as published by
// // the Free Software Foundation, either version 3 of the License, or
// // (at your option) any later version.
// //
// // Meatloaf is distributed in the hope that it will be useful,
// // but WITHOUT ANY WARRANTY; without even the implied warranty of
// // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// // GNU General Public License for more details.
// //
// // You should have received a copy of the GNU General Public License
// // along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// // TR:// - TeensyROM Serial Interface
// // 


// #ifndef MEATLOAF_SCHEME_TR
// #define MEATLOAF_SCHEME_TR

// #include <string>
// #include "qrmanager.h"
// #include "meatloaf.h"
// #include "utils.h"


//  /********************************************************
//   * MStream I
//   ********************************************************/
 
//  class TRMStream: public MStream 
//  {

//     uint32_t generate(std::string data)
//     {
//         Debug_printv("qrcode data[%s]", data.c_str());
//         size_t qr_len = 0;
//         qrManager.encode(data.c_str(), data.length(), 0, 0, &qr_len);
//         qrManager.to_petscii();
//         auto qr = util_hexdump(qrManager.out_buf.data(), qrManager.out_buf.size());
//         Debug_printf("qrcode\r\n%s\r\n", qr.c_str());

//         return qr_len;
//     }

//  public:
//     TRMStream(std::string &path, std::ios_base::openmode m) {
//         //url = path;
//         path = mstr::drop(path, 4); // drop "QR:/"
//         _size = generate(path);
//     }
 
//     // MStream methods
//     bool isOpen() override { return true; };

//     bool open(std::ios_base::openmode mode) override { return true; };
//     void close() override {};
 
//     uint32_t read(uint8_t* buf, uint32_t size) override
//     {
//         Debug_printv("position[%d] size[%d]", _position, size);
//         if (size > (_size - _position)) {
//             size = _size - _position;
//         }
//         if (size < 1)
//             return 0;

//         memcpy(buf, (qrManager.out_buf.data() + _position), size);
//         _position += size;
//         Debug_printv("position[%d] size[%d]", _position, size);

//         return size;
//     };
//     uint32_t write(const uint8_t *buf, uint32_t size) override 
//     {
//         char *s = reinterpret_cast<char*>(const_cast<uint8_t*>(buf));
//         _position = 0;
//         return generate(std::string(s, size));
//     };
 
//     bool seek(uint32_t pos) override { return position(pos); };
//  };


//  /********************************************************
//   * MFile
//   ********************************************************/
 
// class TRMFile: public MFile
// {
// public:
    
//     TRMFile(std::string path): MFile(path)
//     {
//         Debug_printv("path[%s]", this->path.c_str());

//         isWritable = true;
//     };

//     std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override 
//     {
//         std::shared_ptr<MStream> istream = std::make_shared<TRMStream>(url, mode);
//         size = istream->size();
//         return istream;
//     };
//     std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override { return is; };

//     bool isDirectory() override { return false; };
//     bool rewindDirectory() override { return false; };
//     MFile* getNextFileInDir() override { return nullptr; };

//     bool remove() override { return false; };
//     bool rename(std::string dest) { return false; };
//     time_t getLastWrite() override { return 0; };
//     time_t getCreationTime() override { return 0; };
//  };

 
// /********************************************************
//  * MFileSystem
//  ********************************************************/

// class TRMFileSystem: public MFileSystem
// {
// public:
//     TRMFileSystem(): MFileSystem("teensyrom") {
//        isRootFS = true;
//};

//     bool handles(std::string name) {
//         return mstr::startsWith(name, (char *)"tr:", false);
//     }

//     MFile* getFile(std::string path) override {
//         return new TRMFile(path);
//     }
// };

// #endif // MEATLOAF_SCHEME_QRCODE