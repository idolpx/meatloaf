#include "container_broker.h"

MStream* ContainerStreamBroker::obtain(std::string url) {
    if(repo.find(url)!=repo.end()) {
        return repo.at(url);
    }

    MFile* newFile = MFSOwner::File(url);
    MStream* newStream = newFile->getSourceStream();

    repo.insert(std::make_pair(url, newStream));
    delete newFile;
    return newStream;
}

void ContainerStreamBroker::dispose(std::string url) {
    if(repo.find(url)!=repo.end()) {
        MStream* toDelete = repo.at(url);
        repo.erase(url);
        delete toDelete;
    }
}

void ContainerStreamBroker::purge(std::forward_list<virtualDevice *> devs) {
    // to dispose of them we need to be sure they are not used anymore
    // how do we know they are used, though?
    // the most obvious way would be to check if any of the devices and/or channels on IEC is using a path that contains this particular container
    // so if I cycle through each IEC device number and each channel on this device and I can't find this path - I'm disposing of it
    // now - do you have/keep such info anywhere in your IEC abstraction?

    for (auto& it: repo) {
        // iterate through our broker streams and see if they're still used
        bool used = false;

        for (auto it = devs.begin(); it != devs.end(); ++it ) {
            virtualDevice* d = *it;
            // now iterate through device's open streams and if the path contains it.first, set used to TRUE
        }
        
        if(!used) {
            dispose(it.first);
        }
    }

}