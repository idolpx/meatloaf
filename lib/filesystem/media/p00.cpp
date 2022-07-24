#include "p00.h"

/********************************************************
 * Streams
 ********************************************************/

size_t P00IStream::readFile(uint8_t* buf, size_t size) {
    size_t bytesRead = 0;

    bytesRead += containerStream->read(buf, size);
    m_bytesAvailable -= bytesRead;

    return bytesRead;
}



/********************************************************
 * File implementations
 ********************************************************/

MIStream* P00File::createIStream(std::shared_ptr<MIStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new P00IStream(containerIstream);
}


size_t P00File::size() {
    // Debug_printv("[%s]", streamFile->url.c_str());
    // use P00 to get size of the file in image
    auto image = ImageBroker::obtain<P00IStream>(streamFile->url);

    return image->size();
}

