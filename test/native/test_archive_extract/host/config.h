/*
 * libarchive config for the NATIVE test host.
 *
 * libarchive's archive_platform.h does #include "config.h", so putting this
 * directory first on the include path swaps in this file. It defers to the
 * real ESP32 config so the two builds agree on everything that matters to the
 * code under test (which formats and filters are registered, and how they
 * bid), then switches off only the pieces the host cannot provide.
 *
 * Each #undef below removes a dependency this repo vendors for the ESP32 but
 * that isn't built for the host. libarchive compiles a documented
 * "unsupported" fallback for each - the same code an upstream build without
 * that library produces - so the archive/filter set stays honest rather than
 * being faked. None of them affect ZIP/TAR/raw bidding, which is what these
 * tests exercise.
 */
#include "../../../../components/libarchive/config.h"

/* mingw's struct stat has st_atime, not the st_atim/st_atimespec timespec
 * members newlib exposes. */
#undef HAVE_STRUCT_STAT_ST_ATIM
#undef HAVE_STRUCT_STAT_ST_MTIM
#undef HAVE_STRUCT_STAT_ST_CTIM
#undef HAVE_STRUCT_STAT_ST_BIRTHTIM
#undef HAVE_STRUCT_STAT_ST_BIRTHTIME
#undef HAVE_STRUCT_STAT_ST_MTIMESPEC
#undef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#undef HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC

/* Vendored for the firmware, not built for the host. */
#undef HAVE_LZMA_H   /* components/liblzma  */
#undef HAVE_ZSTD_H   /* components/zstd     */
#undef HAVE_EXPAT_H  /* components/expat    */

/* No fork/exec on the host build; the filters that shell out to external
 * lrzip/lzop/grzip binaries compile to their stub form. */
#undef HAVE_FORK
#undef HAVE_VFORK
