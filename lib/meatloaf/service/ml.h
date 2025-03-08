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


#ifndef MEATLOAF_SCHEME_ML
#define MEATLOAF_SCHEME_ML

#include "network/http.h"

#include "peoples_url_parser.h"


/********************************************************
 * FS
 ********************************************************/

class MLMFileSystem: public MFileSystem
{
public:
    MLMFileSystem(): MFileSystem("meatloaf") {};

    bool handles(std::string name) {
        std::string pattern = "ml:";
        return mstr::startsWith(name, pattern.c_str(), false);
    }

    MFile* getFile(std::string path) override {
    //     if ( path.size() == 0 )
    //         return nullptr;

    //     //Debug_printv("MLFileSystem::getFile(%s)", path.c_str());
    //     auto urlParser = PeoplesUrlParser::parseURL( path );
    //     //std::string code = mstr::toUTF8(urlParser->name);

    //     //Debug_printv("url[%s]", urlParser.name.c_str());
    //     std::string ml_url = "https://api.meatloaf.cc/?" + urlParser->name;
    //     if ( urlParser->query.size() > 0)
    //         ml_url += "&" + urlParser->query;
    //     if ( urlParser->fragment.size() > 0)
    //         ml_url += "#" + urlParser->fragment;
    //     Debug_printv("ml_url[%s]", ml_url.c_str());

    //     auto http = new HTTPMFile(ml_url);
    //     auto reader = http->getSourceStream();
    //     auto url = reader->url;
    //     delete http;

    //     Debug_printv("target url[%s]", url.c_str());
    //     return FileBroker::obtain(url);
        return nullptr;
    }

    std::string resolve(std::string path) {
        if ( path.size() == 0 )
            return nullptr;

        //Debug_printv("MLFileSystem::getFile(%s)", path.c_str());
        auto urlParser = PeoplesUrlParser::parseURL( path );
        //std::string code = mstr::toUTF8(urlParser->name);

        //Debug_printv("url[%s]", urlParser.name.c_str());
        std::string ml_url = "https://api.meatloaf.cc/?" + urlParser->name;
        if ( urlParser->query.size() > 0)
            ml_url += "&" + urlParser->query;
        if ( urlParser->fragment.size() > 0)
            ml_url += "#" + urlParser->fragment;
        Debug_printv("ml_url[%s]", ml_url.c_str());

        auto http = new HTTPMFile(ml_url);
        auto reader = http->getSourceStream();
        auto url = reader->url;
        delete http;

        Debug_printv("target url[%s]", url.c_str());
        return url;
    }
};


#endif // MEATLOAF_SCHEME_ML