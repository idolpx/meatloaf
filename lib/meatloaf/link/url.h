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

// .URL - Meatloaf URL Links
// 

#ifndef MEATLOAF_LINK_URL
#define MEATLOAF_LINK_URL

#include "network/http.h"

#include "peoples_url_parser.h"


/********************************************************
 * FS
 ********************************************************/

class URLMFileSystem: public MFileSystem
{
public:
    URLMFileSystem(): MFileSystem("url") {};

    bool handles(std::string fileName) override {
        return byExtension( ".url", fileName );
    }

    MFile* getFile(std::string path) override {
        if ( path.size() == 0 )
            return nullptr;

        Debug_printv("path[%s]", path.c_str());

        // Read URL file
        auto reader = Meat::New<MFile>(path);
        auto istream = reader->getSourceStream();

        uint8_t url[istream->size()];
        istream->read(url, istream->size());

        Debug_printv("url[%s]", url);
        std::string ml_url((char *)url);

        return new HTTPMFile(ml_url);
    }
};


#endif // MEATLOAF_LINK_URL