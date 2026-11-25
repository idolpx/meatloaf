#ifndef ML_HOST_POSIX_COMPAT_H
#define ML_HOST_POSIX_COMPAT_H
/*
 * The two POSIX functions lib/meatloaf uses that a host may not provide.
 * Force-included for the native test build (see [env:native] build_flags).
 *
 * C++ only: it is on the command line for C sources too (build_flags applies
 * to both), and the C libraries built alongside supply their own equivalents.
 */
#if defined(__cplusplus) && (defined(_WIN32) || defined(__APPLE__))
#include <cstring>

/* mingw has no libgen.h, and macOS's takes only a non-const char*. GNU/newlib
 * expose both a char* and a const char* overload in C++ mode (string.h);
 * lib/meatloaf calls both. */
static inline char *basename(char *p)
{
    if (p == nullptr || *p == '\0') return (char *)".";
    char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}
static inline char *basename(const char *p) { return basename(const_cast<char *>(p)); }
#endif

/* mingw's mkdir takes only a path; add the POSIX 2-argument overload. Only
 * there: on a POSIX host a second mkdir() overload recurses into itself. */
#if defined(__cplusplus) && defined(_WIN32)
#include <sys/stat.h>
static inline int mkdir(const char *p, int mode) { (void)mode; return mkdir(p); }
#endif

#endif
