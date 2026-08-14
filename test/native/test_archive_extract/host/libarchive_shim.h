#ifndef ML_HOST_SHIM_H
#define ML_HOST_SHIM_H
#include <errno.h>
#include <time.h>
typedef unsigned int __ml_gid_t;
typedef unsigned int __ml_uid_t;
#define gid_t __ml_gid_t
#define uid_t __ml_uid_t
#ifndef EFTYPE
#define EFTYPE EINVAL
#endif
#ifdef __cplusplus
extern "C" {
#endif
struct tm *localtime_r(const time_t *t, struct tm *out);
struct tm *gmtime_r(const time_t *t, struct tm *out);
#ifdef __cplusplus
}
#endif
#endif
