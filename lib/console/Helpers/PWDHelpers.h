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

    // Resolve a path the user typed, relative to the current path. A single
    // leading '/' is ABSOLUTE on the host whatever the current path is:
    // MFile::cd() follows CBM DOS there (on a network path "/DIR" descends
    // from the current directory), so with the console standing in
    // fsp://host/dir, "ls /sd" listed fsp://host/dir/sd. "//" keeps cd()'s
    // meaning (the root of the current image). The caller owns the result.
    MFile* resolveConsolePath(const std::string& arg);

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