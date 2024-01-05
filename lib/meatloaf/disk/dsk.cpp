#include "dsk.h"

/********************************************************
 * File implementations
 ********************************************************/

MStream* DSKFile::getDecodedStream(std::shared_ptr<MStream> containerIstream) {
    //Debug_printv("[%s]", url.c_str());

    return new DSKIStream(containerIstream);
}
