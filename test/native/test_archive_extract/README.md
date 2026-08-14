# Archive format-selection tests (native)

```
pio test -e native -f native/test_archive_extract
```

Runs on the development host. No hardware, no network, no external tools.

## What it covers

`ArchiveMFile::extractAll()` — and therefore the `unzipx` console command, the
IEC drive's archive browsing, and everything else that walks an archive —
depends on libarchive picking the right format bidder inside `Archive::open()`.
These tests drive exactly that path: a real source stream, the real
`Archive::open()`, the real `cb_read`/`cb_skip`/`cb_seek` callbacks, and the
real `ArchiveMStream::nextEntrySimple()` walk.

| Test | Pins |
|---|---|
| `zip_walk_lists_real_entries` | A real multi-entry zip lists its real entries. Guards the fix from becoming "make everything fail". |
| `misaligned_zip_is_not_extracted_as_raw_data` | A source whose bytes are not the archive's first bytes must not come back as one entry named `data`. |
| `unreadable_zip_fails_rather_than_succeeding_with_junk` | Same defect from the caller's side: failure, not fabricated content. |
| `gz_still_reaches_its_content_through_raw` | The raw reader is still registered where it is correct — a single compressed file has no directory. |

### The bug these exist for

`Archive::open()` used to register `archive_read_support_format_raw()`
alongside the real container format. raw bids 1 on *any* byte stream and
synthesizes one entry named `data` spanning the whole input. So whenever the
real format's bidder declined, raw won and the caller "succeeded", writing a
byte-for-byte copy of the container under the name `data`:

```
meatloaf[/sd/.bin]# unzipx https://.../Donnie_Russell_II_d64.zip
  /sd/.bin/data  (0 bytes)
unzipx: extracted 1 entries, 303509 bytes to '/sd/.bin'
```

303509 is the size of the `.zip`. raw is now registered only for
unknown/ambiguous extensions, where it is the right answer.

## Fixtures

The `.gz` fixture is written byte by byte by the test and removed afterwards.

The zip case uses `.archive/zip/Donnie_Russell_II_d64.zip`. Everything under
`.archive/` is gitignored, so those tests `TEST_IGNORE` themselves when the
sample is absent — a green run without it has not tested the zip path. Any
multi-entry zip works if you adjust the expected first entry name.

## How the host build works

`lib/meatloaf/media/archive/archive.cpp` needs libarchive, which is vendored
for the ESP32 in `components/libarchive`. `host/build_libarchive.py` builds it
(plus zlib, bzip2, lz4 and the `lib/compat` pieces mingw lacks) for the host
and links it in; it is wired into `[env:native]` as an `extra_script` and is
inert for the other native suites.

`host/` holds the shims that make that build work:

- `config.h` — defers to the real `components/libarchive/config.h`, then
  switches off only what the host cannot provide (liblzma, zstd, expat, fork).
  libarchive compiles its documented "unsupported" fallback for each, so the
  registered format and filter set stays honest. Read the file; each `#undef`
  says why.
- `host_posix_compat.h`, `host_support.c`, `langinfo.h`, `sys/wait.h` — the
  handful of POSIX functions mingw does not have (`basename`, 2-arg `mkdir`,
  `localtime_r`).
- `freertos/`, `esp_heap_caps.h` — no-op stands-ins so `meat_session.h` and
  `archive.cpp` compile off-target. `meat_session.h`/`.cpp` gained
  `#ifndef TEST_NATIVE` guards around their `device/iec` and `fnFsSD.h`
  includes, the same pattern `meat_media.h` already used.

`engine_sources.cpp` unity-builds the C++ translation units, as the other
native suites do. libarchive's C sources cannot be unity-built — ~70 files
with colliding file-static helper names — which is why they need the script.
`meat_session.cpp` is also kept out of the unity build: it and `archive.cpp`
each define a file-static `psram_malloc()`.
