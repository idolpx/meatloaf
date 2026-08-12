#ifndef ML_HOST_POSIX_COMPAT_H
#define ML_HOST_POSIX_COMPAT_H
/*
 * The two POSIX functions lib/meatloaf uses that mingw does not provide.
 * Force-included for the native test build (see [env:native] build_flags).
 *
 * C++ only: it is on the command line for C sources too (build_flags applies
 * to both), and the C libraries built alongside supply their own equivalents.
 */
#ifdef __cplusplus
#include <cstring>
#include <sys/stat.h>

/* mingw has no libgen.h. GNU/newlib expose both a char* and a const char*
 * overload in C++ mode; lib/meatloaf calls both. */
static inline char *basename(char *p)
{
    if (p == nullptr || *p == '\0') return (char *)".";
    char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}
static inline char *basename(const char *p) { return basename(const_cast<char *>(p)); }

/* mingw's mkdir takes only a path; add the POSIX 2-argument overload. */
static inline int mkdir(const char *p, int mode) { (void)mode; return mkdir(p); }
#endif /* __cplusplus */

#endif
