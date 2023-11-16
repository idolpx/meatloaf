#ifndef MEATLOAF_FILESYSTEM_CONTAINER_BROKER
#define MEATLOAF_FILESYSTEM_CONTAINER_BROKER


#include <memory>
#include <unordered_map>

#include "bus.h"
#include "meat_io.h"
#include "meat_stream.h"
#include "../device/drive.h"

class ContainerStreamBroker {
    static std::unordered_map<std::string, MStream*> repo;
public:
    static MStream* obtain(std::string url);
    static void dispose(std::string url);
    static void purge(std::forward_list<virtualDevice *> dev);
};

extern ContainerStreamBroker MStreamBroker;

#endif /* MEATLOAF_FILESYSTEM_CONTAINER_BROKER */