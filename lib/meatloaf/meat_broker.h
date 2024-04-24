#ifndef MEATLOAF_STREAM_BROKER
#define MEATLOAF_STREAM_BROKER

#include <memory>
#include <unordered_map>
#include "meatloaf.h"

#include "../device/drive.h"

class StreamBroker {
    static std::unordered_map<std::string, MStream*> repo;
public:
    static MStream* obtain(std::string url);
    static void dispose(std::string url);
    static void purge(std::forward_list<virtualDevice *> dev);
};

#endif /* MEATLOAF_STREAM_BROKER */