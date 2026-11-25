#include "afpStatus.h"

#include <cerrno>

uint8_t afp_errno_to_status(int error)
{
    switch (-error)
    {
    case 0:
        return NETWORK_ERROR_SUCCESS;
    case ENOENT:
        return NETWORK_ERROR_FILE_NOT_FOUND;
    case EEXIST:
        return NETWORK_ERROR_FILE_EXISTS;
    case EACCES:
    case EPERM:
    case EROFS:
        return NETWORK_ERROR_ACCESS_DENIED;
    case ENOSPC:
        return NETWORK_ERROR_NO_SPACE_ON_DEVICE;
    case EISDIR:
    case ENOTDIR:
    case EINVAL:
    case ENAMETOOLONG:
        return NETWORK_ERROR_INVALID_DEVICESPEC;
    case EHOSTUNREACH:
    case ENETUNREACH:
        return NETWORK_ERROR_NETWORK_UNREACHABLE;
    case ECONNREFUSED:
        return NETWORK_ERROR_CONNECTION_REFUSED;
    case ETIMEDOUT:
        return NETWORK_ERROR_SOCKET_TIMEOUT;
    case ENOTCONN:
        return NETWORK_ERROR_NOT_CONNECTED;
    default:
        return NETWORK_ERROR_GENERAL;
    }
}
