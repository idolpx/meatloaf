/* Host-only support for the native archive harness. */
#include <time.h>
struct tm *localtime_r(const time_t *t, struct tm *out) { localtime_s(out, t); return out; }
struct tm *gmtime_r(const time_t *t, struct tm *out) { gmtime_s(out, t); return out; }
