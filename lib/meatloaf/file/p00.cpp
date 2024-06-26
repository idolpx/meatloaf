#include "p00.h"

/********************************************************
 * Streams
 ********************************************************/

uint16_t P00IStream::readFile(uint8_t* buf, uint16_t size) {
    uint16_t bytesRead = 0;

    bytesRead += containerStream->read(buf, size);
    _position += bytesRead;

    return bytesRead;
}



/********************************************************
 * File implementations
 ********************************************************/

uint32_t P00File::size() {
    // Debug_printv("[%s]", streamFile->url.c_str());
    // use P00 to get size of the file in image
    auto image = ImageBroker::obtain<P00IStream>(streamFile->url);

    return image->size();
}

