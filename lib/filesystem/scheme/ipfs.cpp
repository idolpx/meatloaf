#include "ipfs.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"


MIStream* IPFSFile::inputStream() {
    // has to return OPENED stream
    //Debug_printv("[%s]", url.c_str());
    MIStream* istream = new IPFSIStream(url);
    istream->open();   
    return istream;
}; 


bool IPFSIStream::open() {
    PeoplesUrlParser urlParser;
    urlParser.parseUrl(url);

    std::string ml_url = "https://dweb.link/" + urlParser.name;
    url = ml_url;
    
    Debug_printv("url[%s]", url.c_str());
    return m_http.GET(url);
};