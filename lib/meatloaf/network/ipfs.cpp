#include "ipfs.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"


MStream* IPFSMFile::getSourceStream(std::ios_base::openmode mode) {
    // has to return OPENED stream
    //Debug_printv("[%s]", url.c_str());
    MStream* istream = new IPFSMStream(url);
    istream->open(mode);   
    return istream;
}; 


bool IPFSMStream::open(std::ios_base::openmode mode) {
    return _http.GET(url);
};

bool IPFSMStream::seek(uint32_t pos) {
    return true;
}