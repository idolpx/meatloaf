#include "ipfs.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"


MStream* IPFSFile::meatStream(MFileMode mode) {
    // has to return OPENED stream
    //Debug_printv("[%s]", url.c_str());
    MStream* istream = new IPFSIStream(url);
    istream->open();   
    return istream;
}; 


bool IPFSIStream::open(MFileMode mode) {
    return m_http.GET(url);
};

bool IPFSIStream::seek(size_t pos) {
    return true;
}