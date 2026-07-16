#include "PWDHelpers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "string_utils.h"

#include "../Console.h"

#include <mutex>

namespace ESP32Console
{
    constexpr char *PWD_DEFAULT = (char*) "/";

    MFile* currentPath = nullptr;

    // Guards currentPath replacement vs. cross-task readers (SessionBroker)
    static std::mutex s_path_mutex;

    MFile* getCurrentPath() {
        if(currentPath == nullptr) {
            currentPath = MFSOwner::File("/");
        }
        return currentPath;
    }

    void setCurrentPath(MFile* path) {
        if (path == nullptr)
            return;
        MFile* old;
        {
            std::lock_guard<std::mutex> lock(s_path_mutex);
            old = currentPath;
            currentPath = path;
        }
        // Delete outside the lock: media MFile destructors can do real work
        if (old != nullptr && old != path)
            delete old;
    }

    std::string getCurrentPathUrl() {
        std::lock_guard<std::mutex> lock(s_path_mutex);
        if (currentPath == nullptr)
            return "/";
        return currentPath->url;
    }

    // const char *console_getpwd()
    // {
    //     char *pwd = getenv("PWD");
    //     if (pwd)
    //     { // If we have defined a PWD value, return it
    //         return pwd;
    //     }

    //     // Otherwise set a default one
    //     setenv("PWD", PWD_DEFAULT, 1);
    //     return PWD_DEFAULT;
    // }

    const char *console_realpath(const char *path, char *resolvedPath)
    {
        std::string in = std::string(path);
        std::string pwd = getCurrentPath()->url; //std::string(console_getpwd());
        std::string result;

        mstr::replaceAll(in, "'", "");

        // If path is not absolute we prepend our pwd
        if (!mstr::startsWith(in, "/"))
        {
            result.reserve(pwd.size() + 1 + in.size());
            result = pwd;
            result += '/';
            result += in;
        }
        else
        {
            result = in;
        }
        
        realpath(result.c_str(), resolvedPath);
        return resolvedPath;
    }

    // int console_chdir(const char *path)
    // {
    //     char buffer[PATH_MAX + 2];
    //     console_realpath(path, buffer);

    //     size_t buffer_len = strlen(buffer);
    //     //If path does not end with slash, add it.
    //     if(buffer[buffer_len - 1] != '/')
    //     {
    //         buffer[buffer_len] = '/';
    //         buffer[buffer_len + 1] = '\0';
    //     }

    //     setenv("PWD", buffer, 1);

    //     return 0;
    // }

}