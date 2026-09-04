#ifndef IEC_DRIVE_FILENAME_H
#define IEC_DRIVE_FILENAME_H

#include <string>

// Commodore DOS directory requests may select drive 0 or 1 inside the IEC
// device (for example "$0:*" or "$1:NAME*"). Meatloaf exposes one current
// directory per IEC device, so the selector does not change the device or cwd;
// remove it and let the existing "$", "$*", and CMD-filter handling process
// the remainder of the request.
inline bool iecNormalizeDirectoryDrivePrefix(std::string &name)
{
    if (name.size() >= 3 && name[0] == '$' &&
        (name[1] == '0' || name[1] == '1') && name[2] == ':')
    {
        name.erase(1, 2);
        return true;
    }

    return false;
}

#endif // IEC_DRIVE_FILENAME_H
