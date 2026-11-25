/* Host-only support for the native archive harness. */
/* mingw only: PlatformIO's test builder compiles every source in the test
 * folder, so this must compile to nothing where localtime_r/gmtime_r exist. */
#ifdef _WIN32
#include <time.h>
struct tm *localtime_r(const time_t *t, struct tm *out) { localtime_s(out, t); return out; }
struct tm *gmtime_r(const time_t *t, struct tm *out) { gmtime_s(out, t); return out; }
#endif
