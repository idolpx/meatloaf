#include "ml.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"


MIStream* MLFile::inputStream() {
    // has to return OPENED stream
    //Debug_printv("[%s]", url.c_str());
    MIStream* istream = new MLIStream(url);
    istream->open();   
    return istream;
}; 


bool MLIStream::open() {
    PeoplesUrlParser urlParser;
    urlParser.parseUrl(url);

    std::string ml_url = "https://api.meatloaf.cc/?" + urlParser.name;
    url = ml_url;
    
    // a new way to do that:
    // 1. put an instance of MeatHttpClient somewhere in this class as a class field
    MeatHttpClient client;
    // 2. if you need to process some headers, use this lambda:
    client.setOnHeader([this](char* header, char* value) {
        // did we receive ml_media_header?
        if(strcmp("ml_media_header", header)==0) {
            // assign this header value to something
            this->url = value;
        }
        if(strcmp("ml_media_header", header)==0) {
            // assign this header value to something
            this->url = value;
        }
        if(strcmp("ml_media_header", header)==0) {
            // assign this header value to something
            this->url = value;
        }
        if(strcmp("ml_media_header", header)==0) {
            // assign this header value to something
            this->url = value;
        }
        return ESP_OK;
    });
    return client.GET(url);
};