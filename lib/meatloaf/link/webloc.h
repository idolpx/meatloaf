// .URL - Meatloaf URL Links
// 

#ifndef MEATLOAF_LINK_URL
#define MEATLOAF_LINK_URL

#include "network/http.h"

#include "peoples_url_parser.h"


/********************************************************
 * FS
 ********************************************************/

class MLFileSystem: public MFileSystem
{
public:
    MLFileSystem(): MFileSystem("webloc") {};

    bool handles(std::string fileName) override {
        //printf("handles w dnp %s %d\r\n", fileName.rfind(".dnp"), fileName.length()-4);
        return byExtension( ".webloc", fileName );
    }

    MFile* getFile(std::string path) override {
        if ( path.size() == 0 )
            return nullptr;

        // Read URL file

        //Debug_printv("MLFileSystem::getFile(%s)", path.c_str());
        PeoplesUrlParser *urlParser = PeoplesUrlParser::parseURL( path );
        std::string code = mstr::toUTF8(urlParser->name);

        //Debug_printv("url[%s]", urlParser.name.c_str());
        std::string ml_url = "https://api.meatloaf.cc/?" + code;
        //Debug_printv("ml_url[%s]", ml_url.c_str());
        
        //Debug_printv("url[%s]", ml_url.c_str());
        delete(urlParser);
        return new HTTPMFile(ml_url);
    }
};


#endif // MEATLOAF_LINK_URL