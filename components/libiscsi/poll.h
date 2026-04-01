/* ESP32 shim: redirect <poll.h> to <sys/poll.h> provided by lwIP/newlib */
#pragma once
#include <sys/poll.h>
