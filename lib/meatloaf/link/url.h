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
    MLFileSystem(): MFileSystem("url") {};

    bool handles(std::string fileName) override {
        //printf("handles w dnp %s %d\r\n", fileName.rfind(".dnp"), fileName.length()-4);
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