#ifndef AFP_STATUS_H
#define AFP_STATUS_H

#include <stdint.h>

#include "status_error_codes.h"

// 0 or a negative errno from lib/afp, as the NETWORK_ERROR_* status an N:
// device reports. (FujiNet's nDevStatus_t/NDEV_STATUS is not in this tree.)
uint8_t afp_errno_to_status(int error);

#endif /* AFP_STATUS_H */
