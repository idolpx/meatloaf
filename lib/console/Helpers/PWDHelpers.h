#pragma once

#include "device.h"
#include "display.h"
#include "meatloaf.h"
#include "string_utils.h"

namespace ESP32Console
{

    //char *canonicalize_file_name(const char *path);
    extern MFile* currentPath;

    MFile* getCurrentPath();

    // Replace the console's current path, deleting the previous MFile.
    // Takes ownership of 'path'.
    void setCurrentPath(MFile* path);

    // Thread-safe copy of the current path URL (safe to call from other
    // tasks, e.g. the SessionBroker service task)
    std::string getCurrentPathUrl();

    /**
     * @brief Returns the current console process working dir
     *
     * @return const char*
     */
    //const char *console_getpwd();

    /**
     * @brief Resolves the given path using the console process working dir
     *
     * @return const char*
     */
    const char *console_realpath(const char *path, char *resolvedPath);

    //int console_chdir(const char *path);

}