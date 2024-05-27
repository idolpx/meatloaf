#ifndef MEATLOAF_STREAM_BROKER
#define MEATLOAF_STREAM_BROKER

#include <memory>
#include <unordered_map>
#include "meatloaf.h"
#include "iec_pipe_broker.h"
#include <unordered_map>
#include <memory>

#include "../device/drive.h"

// Assuming MStream and MFile classes are properly defined

class StreamBroker {
private:
    // Define a hash function for the combination of streamFile pointer and mode
    struct StreamHash {
        std::size_t operator()(const std::pair<MFile*, std::ios_base::openmode>& p) const {
            std::size_t h1 = std::hash<MFile*>{}(p.first);
            std::size_t h2 = std::hash<int>{}(static_cast<int>(p.second));
            return h1 ^ h2;
        }
    };

    // Define the type for storing cached MStream instances
    std::unordered_map<std::pair<MFile*, std::ios_base::openmode>, std::shared_ptr<MStream>, StreamHash> streamCache;

public:
    // Method to retrieve MStream instance from cache or create a new one if not found
    std::shared_ptr<MStream> getSourceStream(MFile* streamFile, std::ios_base::openmode mode) {
        auto key = std::make_pair(streamFile, mode);
        auto it = streamCache.find(key);
        if (it != streamCache.end()) {
            return it->second; // Found in cache, return the cached instance
        } else {
            // Not found, create a new instance and cache it
            std::shared_ptr<MStream> newStream(streamFile->getSourceStream(mode));
            streamCache[key] = newStream;
            return newStream;
        }
    }

    std::shared_ptr<MStream> getDecodedStream(MFile* streamFile, std::ios_base::openmode mode, std::shared_ptr<MStream> wrappingStream) {
        auto key = std::make_pair(streamFile, mode);
        auto it = streamCache.find(key);
        if (it != streamCache.end()) {
            return it->second; // Found in cache, return the cached instance
        } else {
            std::shared_ptr<MStream> newStream(streamFile->getDecodedStream(wrappingStream));
            streamCache[key] = newStream;
            return newStream;
        }
    }

    // Function to remove streamCache entries without matching iecPipe instances
    void flushInactiveStreams(iecPipeBroker& broker) {
        const std::unordered_map<std::tuple<int, int>, std::unique_ptr<iecPipe>> deviceChannelMap = broker.getDeviceChannelMap();
                
        for (auto keyStreamPair = streamCache.begin(); keyStreamPair != streamCache.end();) {
            bool inUse = false;
            for (const auto& keyPipePair : deviceChannelMap) {
                if (keyPipePair.second->getFilename() == keyStreamPair->first.first->path) {
                    inUse = true;
                }
            }

            if (!inUse) {
                keyStreamPair->second->close(); // Close the stream before removing
                keyStreamPair = streamCache.erase(keyStreamPair); // Remove the entry if no matching iecPipe found
            } else {
                ++keyStreamPair;
            }
        }
    }
};



#endif /* MEATLOAF_STREAM_BROKER */