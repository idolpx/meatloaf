// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#include "ipfs.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"


std::shared_ptr<MStream> IPFSMFile::getSourceStream(std::ios_base::openmode mode) {
    // has to return OPENED stream
    //Debug_printv("[%s]", url.c_str());
    std::string requestUrl = buildRequestUrl();
    return openStreamWithCache(
        requestUrl,
        mode,
        [](const std::string& openUrl, std::ios_base::openmode openMode) -> std::shared_ptr<MStream> {
            std::string mutableUrl = openUrl;
            auto stream = std::make_shared<IPFSMStream>(mutableUrl);
            stream->open(openMode);
            return stream;
        });
}; 


bool IPFSMStream::open(std::ios_base::openmode mode) {
    return _session->client->GET(url);
};

bool IPFSMStream::seek(uint32_t pos) {
    return true;
}