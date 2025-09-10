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

// #include "teensyrom.h"

// #include <sys/stat.h>
// #include <unistd.h>
// #include <sstream>
// #include <iomanip>

// #include "meatloaf.h"
// #include "../../../include/debug.h"
// #include "peoples_url_parser.h"
// #include "string_utils.h"


// /********************************************************
//  * MFile implementations
//  ********************************************************/

// bool TRMFile::pathValid(std::string path) 
// {
//     auto apath = std::string(basepath + path).c_str();
//     while (*apath) {
//         const char *slash = strchr(apath, '/');
//         if (!slash) {
//             if (strlen(apath) >= FILENAME_MAX) {
//                 // Terminal filename is too long
//                 return false;
//             }
//             break;
//         }
//         if ((slash - apath) >= FILENAME_MAX) {
//             // This subdir name too long
//             return false;
//         }
//         apath = slash + 1;
//     }

//     return true;
// }

// bool TRMFile::isDirectory()
// {
//     //Debug_printv("path[%s]", path.c_str());
//     if(path=="/" || path.empty())
//         return true;

//     struct stat info;
//     stat( std::string(basepath + path).c_str(), &info);
//     return S_ISDIR(info.st_mode);
// }


// std::shared_ptr<MStream> TRMFile::getSourceStream(std::ios_base::openmode mode)
// {
//     std::string full_path = basepath + path;
//     std::shared_ptr<MStream> istream = std::make_shared<TRMStream>(full_path, mode);
//     //auto istream = StreamBroker::obtain<TRMStream>(full_path, mode);
//     //Debug_printv("TRMFile::getSourceStream() 3, not null=%d", istream != nullptr);
//     istream->open(mode);   
//     //Debug_printv("TRMFile::getSourceStream() 4");
//     return istream;
// }

// std::shared_ptr<MStream> TRMFile::getDecodedStream(std::shared_ptr<MStream> is) {
//     return is; // we don't have to process this stream in any way, just return the original stream
// }

// std::shared_ptr<MStream> TRMFile::createStream(std::ios_base::openmode mode)
// {
//     std::string full_path = basepath + path;
//     std::shared_ptr<MStream> istream = std::make_shared<TRMStream>(full_path, mode);
//     return istream;
// }

// time_t TRMFile::getLastWrite()
// {
//     struct stat info;
//     stat( std::string(basepath + path).c_str(), &info);

//     time_t ftime = info.st_mtime; // Time of last modification
//     return ftime;
// }

// time_t TRMFile::getCreationTime()
// {
//     struct stat info;
//     stat( std::string(basepath + path).c_str(), &info);

//     time_t ftime = info.st_ctime; // Time of last status change
//     return ftime;
// }

// bool TRMFile::mkDir()
// {
//     if (m_isNull) {
//         return false;
//     }
//     int rc = mkdir(std::string(basepath + path).c_str(), ALLPERMS);
//     return (rc==0);
// }

// bool TRMFile::rmDir()
// {
//     if (m_isNull) {
//         return false;
//     }
//     int rc = rmdir(std::string(basepath + path).c_str());
//     return (rc==0);
// }

// bool TRMFile::exists()
// {
//     if (m_isNull) {
//         return false;
//     }
//     if (path=="/" || path=="") {
//         return true;
//     }

//     struct stat st;
//     int i = stat(std::string(basepath + path).c_str(), &st);

//     //Debug_printv( "exists[%d] basepath[%s] path[%s]", (i==0), basepath.c_str(), path.c_str() );
//     return (i == 0);
// }


// bool TRMFile::remove() {
//     // musi obslugiwac usuwanie plikow i katalogow!
//     if(path.empty())
//         return false;

//     int rc = ::remove( std::string(basepath + path).c_str() );
//     if (rc != 0) {
//         Debug_printv("remove: rc=%d path=`%s`\r\n", rc, path.c_str());
//         return false;
//     }

//     return true;
// }


// bool TRMFile::rename(std::string pathTo) {
//     if(pathTo.empty())
//         return false;

//     int rc = ::rename( std::string(basepath + path).c_str(), std::string(basepath + pathTo).c_str() );
//     if (rc != 0) {
//         return false;
//     }
//     return true;
// }


// void TRMFile::openDir(std::string path) 
// {
//     if (!isDirectory()) { 
//         dirOpened = false;
//         return;
//     }
    
//     // Debug_printv("path[%s]", apath.c_str());
//     if(path.empty()) {
//         dir = opendir( "/" );
//     }
//     else {
//         dir = opendir( path.c_str() );
//     }

//     dirOpened = true;
//     if ( dir == NULL ) {
//         dirOpened = false;
//     }
//     // else {
//     //     // Skip the . and .. entries
//     //     struct dirent* dirent = NULL;
//     //     dirent = readdir( dir );
//     //     dirent = readdir( dir );
//     // }
// }


// void TRMFile::closeDir() 
// {
//     if(dirOpened) {
//         closedir( dir );
//         dirOpened = false;
//     }
// }


// bool TRMFile::rewindDirectory()
// {
//     _valid = false;
//     rewinddir( dir );

//     // // Skip the . and .. entries
//     // struct dirent* dirent = NULL;
//     // dirent = readdir( dir );
//     // dirent = readdir( dir );

//     return (dir != NULL) ? true: false;
// }


// MFile* TRMFile::getNextFileInDir()
// {
//     // Debug_printv("base[%s] path[%s]", basepath.c_str(), path.c_str());
//     if(!dirOpened)
//         openDir(std::string(basepath + path).c_str());

//     if(dir == nullptr)
//         return nullptr;

//     // Debug_printv("before readdir(), dir not null:%d", dir != nullptr);
//     struct dirent* dirent = NULL;
//     do
//     {
//         dirent = readdir( dir );
//     } while ( dirent != NULL && mstr::startsWith(dirent->d_name, ".") ); // Skip hidden files
    
//     if ( dirent != NULL )
//     {
//         //Debug_printv("path[%s] name[%s]", this->path.c_str(), dirent->d_name);
//         std::string entry_name = this->path + ((this->path == "/") ? "" : "/") + std::string(dirent->d_name);

//         auto file = new TRMFile(entry_name);
//         file->extension = " " + file->extension;

//         if(file->isDirectory()) {
//             file->size = 0;
//         }
//         else {
//             struct stat info;
//             stat( std::string(entry_name).c_str(), &info);
//             file->size = info.st_size;
//         }

//         return file;
//     }
//     else
//     {
//         closeDir();
//         return nullptr;
//     }
// }


// bool TRMFile::readEntry( std::string filename )
// {
//     std::string apath = (basepath + pathToFile()).c_str();
//     if (apath.empty()) {
//         apath = "/";
//     }

//     Debug_printv( "path[%s] filename[%s] size[%d]", apath.c_str(), filename.c_str(), filename.size());

//     DIR* d = opendir( apath.c_str() );
//     if(d == nullptr)
//         return false;

//     // Read Directory Entries
//     if ( filename.size() > 0 )
//     {
//         struct dirent* dirent = NULL;
//         bool found = false;
//         bool wildcard =  ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );
//         while ( (dirent = readdir( d )) != NULL )
//         {
//             std::string entryFilename = dirent->d_name;

//             Debug_printv("path[%s] filename[%s] entry.filename[%.16s]", apath.c_str(), filename.c_str(), entryFilename.c_str());

//             // Read Entry From Stream
//             if ( dirent->d_type != DT_DIR ) // Only want to match files not directories
//             {
//                 if ( mstr::compareFilename(filename, entryFilename, wildcard) )
//                 {
//                     found = true;
//                 }

//                 if ( found )
//                 {
//                     resetURL(apath + "/" + entryFilename);
//                     _exists = true;
//                     closedir( d );
//                     return true;
//                 }
//             }
//         }

//         Debug_printv( "Not Found! file[%s]", filename.c_str() );
//     }

//     closedir( d );
//     return false;
// }



// /********************************************************
//  * MStream implementations
//  ********************************************************/

// bool TRMStream::open(std::ios_base::openmode mode) {
//     if(isOpen())
//         return true;

//     //Debug_printv("IStream: trying to open flash fs, calling isOpen");

//     //Debug_printv("IStream: wasn't open, calling obtain");
//     if(mode == std::ios_base::in)
//         handle->obtain(localPath, "r");
//     else if(mode == std::ios_base::out) {
//         Debug_printv("TRMStream: ok, we are in write mode!");
//         handle->obtain(localPath, "w");
//     }
//     else if(mode == std::ios_base::app)
//         handle->obtain(localPath, "a");
//     else if(mode == (std::ios_base::in | std::ios_base::out))
//         handle->obtain(localPath, "r+");
//     else if(mode == (std::ios_base::in | std::ios_base::app))
//         handle->obtain(localPath, "a+");
//     else if(mode == (std::ios_base::in | std::ios_base::out | std::ios_base::trunc))
//         handle->obtain(localPath, "w+");
//     else if(mode == (std::ios_base::in | std::ios_base::out | std::ios_base::app))
//         handle->obtain(localPath, "a+");

//     // The below code will definitely destroy whatever open above does, because it will move the file pointer
//     // so I just wrapped it to be called only for in
//     if( isOpen() && ((mode==std::ios_base::in) || (mode==(std::ios_base::in|std::ios_base::out)))  ) {
//         //Debug_printv("IStream: past obtain");
//         // Set file size
//         fseek(handle->file_h, 0, SEEK_END);
//         //Debug_printv("IStream: past fseek 1");
//         _size = ftell(handle->file_h);
//         _position = 0;
//         //Debug_printv("IStream: past ftell");
//         fseek(handle->file_h, 0, SEEK_SET);
//         //Debug_printv("IStream: past fseek 2");
//         return true;
//     }
//     return false;
// };

// void TRMStream::close() {
//     if(isOpen()) handle->dispose();
// };

// uint32_t TRMStream::read(uint8_t* buf, uint32_t size) {
//     if (!isOpen() || !buf) {
//         Debug_printv("Not open");
//         return 0;
//     }

//     uint32_t count = 0;
    
//     if ( size > 0 )
//     {
//         if ( size > available() )
//             size = available();

//         count = fread((void*) buf, 1, size, handle->file_h );
//         // Debug_printv("count[%d]", count);
//         // auto hex = mstr::toHex(buf, count);
//         // Debug_printv("[%s]", hex.c_str());
//         _position += count;
//     }

//     return count;
// };

// uint32_t TRMStream::write(const uint8_t *buf, uint32_t size) {
//     if (!isOpen() || !buf) {
//         Debug_printv("Not open");
//         return 0;
//     }

//     //Debug_printv("buf[%02X] size[%lu]", buf[0], size);

//     // buffer, element size, count, handle
//     uint32_t count = fwrite((void*) buf, 1, size, handle->file_h );
//     _position += count;

//     //Debug_printv("count[%lu] position[%lu]", count, _position);
//     return count;
// };


// bool TRMStream::seek(uint32_t pos) {
//     // Debug_printv("pos[%d]", pos);
//     if (!isOpen()) {
//         Debug_printv("Not open");
//         return false;
//     }
//     _position = pos;
//     return ( fseek( handle->file_h, pos, SEEK_SET ) ) ? false : true;
// };

// bool TRMStream::isOpen() {
//     // Debug_printv("Inside isOpen, handle notnull:%d", handle != nullptr);
//     auto temp = handle != nullptr && handle->file_h != nullptr;
//     // Debug_printv("returning");
//     return temp;
// }
