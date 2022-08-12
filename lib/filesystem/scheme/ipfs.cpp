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
    return m_http.GET(url);
};

bool IPFSIStream::seek(size_t pos) {
    return true;
}