"""
Builds the C libraries the archive tests need (libarchive + zlib + bzip2 +
lz4) for the NATIVE test host, and puts their headers on the include path.

Why a script instead of the usual engine_sources.cpp "unity build" trick the
other native suites use: those concatenate C++ files into one translation
unit, which works because they have no colliding file-static symbols.
libarchive is ~70 C files that each define file-static helpers with the same
names (bid, init, read_data, ...), so concatenating them does not compile.
They have to stay separate translation units, which means real build rules.

PlatformIO's library dependency finder is off for [env:native] (see
test_disk_write/engine_sources.cpp), so nothing here is discovered
automatically - every source is listed or globbed explicitly below.

Wired in from [env:native] in platformio.ini:

    extra_scripts = test/native/test_archive_extract/host/build_libarchive.py

It is harmless for the other native suites: they simply never reference any
of these symbols, and the linker drops them.
"""

import os
import sys
from glob import glob

Import("env")  # noqa: F821  (injected by SCons/PlatformIO)

PROJECT_DIR = env.subst("$PROJECT_DIR")
HOST_DIR = os.path.join(PROJECT_DIR, "test", "native", "test_archive_extract", "host")

LIBARCHIVE = os.path.join(PROJECT_DIR, "components", "libarchive")
ZLIB = os.path.join(PROJECT_DIR, "components", "zlib", "zlib")
BZIP2 = os.path.join(PROJECT_DIR, "components", "bzip2", "lib")
LZ4 = os.path.join(PROJECT_DIR, "components", "lz4", "lib")

IS_WINDOWS = sys.platform.startswith("win")

# libarchive sources that are not part of the read path these tests exercise,
# or that need host facilities the test host does not have. Dropping them
# keeps the build honest: nothing here participates in format bidding.
SKIP_PREFIXES = (
    "archive_write",          # write path - unused, and needs host disk APIs
    "archive_read_disk",      # reading from a real directory tree
    "archive_read_extract",   # extract-to-disk
)
SKIP_EXACT = {
    "archive_read_open_filename",  # tests drive the callbacks, not a filename
    "archive_random",              # Windows BCrypt/arc4random
    "archive_match",               # unused
    "filter_fork_posix",           # Windows uses filter_fork_windows.c
}


def libarchive_sources():
    out = []
    for path in sorted(glob(os.path.join(LIBARCHIVE, "lib", "*.c"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if name in SKIP_EXACT or name.startswith(SKIP_PREFIXES):
            continue
        if name == "filter_fork_windows" and not IS_WINDOWS:
            continue
        out.append(path)
    return out


ZLIB_SOURCES = [
    os.path.join(ZLIB, f + ".c")
    for f in ("adler32", "crc32", "inflate", "inftrees", "inffast", "zutil",
              "deflate", "trees", "compress", "uncompr")
]

# The include path order matters: HOST_DIR must come first so libarchive's
# #include "config.h" resolves to host/config.h (see the header of that file).
C_INCLUDES = [HOST_DIR, LIBARCHIVE, os.path.join(LIBARCHIVE, "lib"), ZLIB, BZIP2, LZ4]

# The C++ side's include paths live in [env:native] build_flags, not here:
# PlatformIO builds the test translation units with an environment cloned
# before extra_scripts run, so a CPPPATH appended here never reaches them.
if IS_WINDOWS:
    env.Append(LIBS=["bcrypt"])

c_env = env.Clone()
c_env.Append(CPPPATH=C_INCLUDES, CPPDEFINES=["HAVE_CONFIG_H"])
# -w: vendored third-party C, not this project's code to fix.
c_env.Append(CFLAGS=["-w", "-include", os.path.join(HOST_DIR, "libarchive_shim.h")])

objects = []


def build(source_env, path, group, extra_cflags=None):
    build_env = source_env
    if extra_cflags:
        build_env = source_env.Clone()
        build_env.Append(CFLAGS=extra_cflags)
    # Grouped by library: zlib and bzip2 both have a compress.c, and one
    # flat object directory would give them the same target.
    target = os.path.join("$BUILD_DIR", "archive_host", group,
                          os.path.splitext(os.path.basename(path))[0])
    objects.append(build_env.Object(target, path))


for src in libarchive_sources():
    name = os.path.basename(src)
    if name == "archive_util.c" and IS_WINDOWS:
        # Uses BCRYPT_ALG_HANDLE for its temp-file helper; the Windows headers
        # that declare it are not reachable through libarchive's own includes.
        build(c_env, src, "libarchive", ["-include", "windows.h", "-include", "bcrypt.h"])
    else:
        build(c_env, src, "libarchive")

z_env = env.Clone()
z_env.Append(CFLAGS=["-w"])
for src in ZLIB_SOURCES:
    build(z_env.Clone(CPPPATH=[ZLIB]), src, "zlib")
for src in sorted(glob(os.path.join(BZIP2, "*.c"))):
    build(z_env.Clone(CPPPATH=[BZIP2]), src, "bzip2")
for src in sorted(glob(os.path.join(LZ4, "*.c"))):
    build(z_env.Clone(CPPPATH=[LZ4]), src, "lz4")

# lib/compat: strlcpy/strlcat/compat_gettimeofday, which lib/utils/utils.cpp
# calls and mingw does not provide. Plus localtime_r/gmtime_r for libarchive.
COMPAT = os.path.join(PROJECT_DIR, "lib", "compat")
for src in ("strlcpy.c", "strlcat.c", "compat_gettimeofday.c"):
    build(z_env.Clone(CPPPATH=[COMPAT]), os.path.join(COMPAT, src), "compat")
build(z_env, os.path.join(HOST_DIR, "host_support.c"), "host")

# Link as a static library rather than loose objects: PlatformIO's test
# builder does not consume PIOBUILDFILES, but it does honour LIBS/LIBPATH.
archive_lib = env.StaticLibrary(os.path.join("$BUILD_DIR", "archive_host", "archive_host"), objects)
env.Prepend(LIBS=[archive_lib])
