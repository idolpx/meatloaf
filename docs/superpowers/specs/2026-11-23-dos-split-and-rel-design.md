# DOS split out of iecDrive, and native REL file support

Date: 2026-11-23
Status: draft, awaiting approval

## Intent

`iecDrive` should support CBM relative (REL) files on its own, without VDrive. Getting there
cleanly means taking the DOS out of `iecDrive::executeData()` (~1000 lines, two back-to-back
switches mixing CBM DOS 2.6, SD2IEC and Meatloaf commands) and making it a separate component
whose variant is chosen by the mounted media.

The longer goal is to **retire VDrive** (`lib/vdrive`, the VICE engine) entirely: everything it does
should live in `iecDrive` or in the appropriate `MFile`/`MStream` classes. This project is a step on
that road, not the removal itself.

## Decisions made with the user

- **VDrive is retired eventually.** Nothing new is built on it. This project does not delete it.
- **One DOS with extensions, not interchangeable DOSes.** CBM DOS is the base. CMD HD DOS and IDE64
  DOS are extensions of CBM DOS. SD2IEC and Meatloaf extensions are part of the default and always on.
- **Approach A (chosen):** a `CbmDos` class with media-specific subclasses (`CmdHdDos`, `Ide64Dos`),
  chosen when the media changes. Channels and bus I/O stay in `iecDrive`.
- **REL is its own `MStream`.**
- **REL must work in both places:** inside CBM disk images (D64/D71/D81/DNP/DHD partitions) and as
  plain files on SD, flash and the network (P00-style containers, as SD2IEC does).

## Goals

1. `CbmDos` owns the command channel (channel 15 `executeData`) and the parsing of OPEN file
   specifications (`@0:name,s,w`, `name,l,<len>`, `//dir/:file`, `#`, `$`). Today the OPEN parsing
   is inline in `iecDrive::open()`, which already carries `// TODO: Use CBM DOS command parser here`.
2. `CmdHdDos` and `Ide64Dos` derive from `CbmDos` and carry what is specific to those media
   (partition commands `CP`/`C-P`, partition-aware paths, `$=P` listings, anything else the builder
   finds that is CMD- or CFS-only today).
3. `iecDrive` creates the right DOS whenever the media changes (`set_cwd()`, `mount()`, `unmount()`).
4. REL files: create, open, position (`P`), read records, write records, expand — in disk images and
   in plain-file containers.
5. A config option to find and list x00 files by their header name instead of their host filename.
6. No behaviour change in phase 1 (see Phasing).

## Non-goals

- Removing VDrive, or the `VD+`/`VD-` toggle. VDrive's code paths stay as they are; the DOS split
  must not change what happens when VDrive is active.
- Porting the remaining VDrive-only functionality other than REL. The builder produces a written
  list of what is still missing (see "What is left to the builder"), as follow-up work.
- Moving channel ownership, `open/read/write/close`, or the fast-loader hooks out of `iecDrive`.
  That was option C and was rejected for this project.
- GEOS VLIR support.

## Anti-goals (would be a failure even if it works)

- **Any change in IEC bus timing.** The DOS runs only on command-channel and OPEN paths, never inside
  the byte loop.
- **Phase 1 changes what any existing command answers.** It is a move, not a rewrite. Status codes,
  status text and side effects must be identical, including the known quirks.
- **A DOS that reaches back into `iecDrive` internals freely.** The DOS talks to the drive through a
  small, named interface. If it needs `m_channels[]` raw, the boundary is wrong.
- **REL support that only works on one of the two media kinds.**

## Constraints (from the codebase)

- ESP-IDF is `-fno-exceptions`; no throwing allocation. Working tables go on the heap, not the stack
  (`console_exec` is 16 KB, `bus_iec` 20 KB).
- `lib/meatloaf` must stay free of `device/iec` includes, or the native test suite stops building.
  `RELMStream` lives in `lib/meatloaf`; the DOS classes live in `lib/device/iec/drive/dos/` (the
  directory already exists, empty).
- Flash-text budget is tight on ESP32 WROVER boards (`iram0_2_seg`); see the August 25 entry in
  `AGENTS.md`. Measure `.flash.text` before and after on `fujiloaf-rev0` and `lolin-d32-pro`.
- `iecMeatloaf` (device 30) overrides `executeData()` to try FujiNet commands first and fall through
  to `iecDrive::executeData()`. That must still work.
- `MStream::read()` returns at most one block; callers loop.
- A media stream's `mode` is uninitialised unless `getSourceStream()` sets it — any directly built
  stream must set it.
- Container streams must be opened `in|out`, never plain `out` (truncates the image).
- `iecChannelHandler` reads ahead `BUFFER_SIZE` (512) bytes. REL reads must not read past a record
  end (see below).

## Approaches considered

- **A. `CbmDos` + media subclasses (chosen).** Matches "extensions of CBM DOS" exactly; channels and
  timing stay put. Only one extension set active at a time — fine, since one medium is never both CMD
  and IDE64.
- **B. Base DOS + chain of plug-in extensions.** More flexible, but its one advantage (combining
  extensions) is not needed, and it adds ordering/precedence rules.
- **C. DOS owns everything including channels.** Most like VDrive and the most VDrive-shaped code
  removed, but rewrites OPEN/read/write/close, fast-loader hooks and console channel commands at
  once — biggest timing risk. A does not block moving to C later.

## Design

### DOS layer

```
lib/device/iec/drive/dos/
  dos_host.h      DosHost: the interface iecDrive offers the DOS
  cbm_dos.h/.cpp  CbmDos: CBM 2.6 + SD2IEC + Meatloaf commands, OPEN spec parsing
  cmd_dos.h/.cpp  CmdHdDos : CbmDos  (CMD HD / FD media: .dhd, .d1m/.d2m/.d4m)
  ide64_dos.h/.cpp Ide64Dos : CbmDos (IDE64 CFS media: .hdd)
```

- `iecDrive` holds `std::unique_ptr<CbmDos> m_dos`, never null (a plain `CbmDos` when nothing
  special is mounted).
- **Selection** happens in one function, `iecDrive::selectDos()`, called after every media change.
  It uses `hdpart::targetFor(m_cwd->url).kind` (`Kind::CMD` → `CmdHdDos`, `Kind::CFS` → `Ide64Dos`,
  else `CbmDos`), which already exists and is already how `changePartition()` decides. The DOS is
  only recreated when the kind changes, so ordinary `CD`s do not churn allocations.
- **Command dispatch:** `iecDrive::executeData()` keeps its pre-DOS work (hotplug sync, LED, the
  VDrive branch, `VD+/-`) and then calls `m_dos->execute(command)`. `CbmDos::execute()` reproduces
  today's order: Meatloaf prefix commands (`CD`, `E?+/-`, the Dolphin `M-R` hack), then the CBM 2.6
  switch, then the SD2IEC switch. Each command family becomes a protected virtual method
  (`cmdBlock`, `cmdMemory`, `cmdNew`, `cmdRename`, `cmdScratch`, `cmdUser`, `cmdPosition`,
  `cmdPartition`, `cmdTime`, …) so a subclass overrides one family, not the switch.
- **OPEN:** `CbmDos::parseOpen(channel, name)` returns a plain `OpenRequest` struct — cleaned name,
  directory path part, mode (read/write/append/modify), overwrite flag, file type (`P/S/U/L/D`),
  record length, direct-access flag, `$` flag. `iecDrive::open()` keeps doing the opening and channel
  creation, driven by that struct. The current inline parsing moves there unchanged.
- **DosHost** is what the DOS may use: status setting, `m_cwd` get/`set_cwd`, channel lookup by number
  (returning the handler, already bounds-checked), `positionChannel`, `seekChannelToBlock`,
  `changePartition`, drive memory, fast-loader enable, tape command. Exact member list is the
  builder's; the rule is that everything the moved code touches today goes through it.
- **device 30:** `iecMeatloaf::executeData()` is unchanged — it still tries FujiNet first and falls
  through to `iecDrive::executeData()`, which now reaches the DOS.

### REL files

REL is two layers, so that both media kinds share everything except storage growth.

**`RELMStream` (lib/meatloaf/media/file/rel.h/.cpp)** wraps a random-access byte stream holding the
record data, plus a record length (1-254). It owns everything that is the same everywhere:

- `setRecord(record, offset)` for the `P` command; past the last record → status 50 RECORD NOT
  PRESENT, and a write there expands the file.
- Reads return the current record up to its last non-`$00` byte, then end-of-record (EOI), per CBM
  DOS semantics.
- Writes fill the current record; overflowing it is 51 OVERFLOW IN RECORD (excess dropped); ending a
  write pads the rest of the record with `$00`; the record pointer then advances.
- New records are initialised as `$FF` followed by `$00`s.
- Growth is delegated through a small interface, `RELStorage`: `size()`, `read/write at offset`,
  `grow(newSize)`. `RELMStream` never knows what holds the bytes.

**Storage implementations:**

- **Disk images (`D64MStream` family):** a REL entry's data chain plus its side sectors. Random access
  uses the side sectors (6 per group of 120 data blocks; D81/D82/DNP add a super side sector). `grow()`
  allocates data blocks with the existing allocator (`getNextFreeBlock()`), links them, and writes the
  side sectors — creating new side-sector blocks and, for D81/D82/DNP, the super side sector. Creating a
  REL file writes a directory entry with type `$84`, `rel_start_track/sector` and
  `rel_record_length` (fields already in `D64MStream`'s entry struct). Delete/scratch must free side
  sectors too.
- **Plain files:** the P00 container already parsed by `P00MStream` (record length in the header's
  `rel_flag`). Needs write support and random access. `grow()` appends. How the file is named and
  found (decided with the user):
  - **Default — host filename.** `OPEN "SCORES,L,…"` finds `scores.r00`; creating a REL file writes
    `<name>.r00` with a P00 header whose name field holds the CBM name. Characters a host filesystem
    cannot hold (`/ \ : * ? " < > |`, control bytes) are replaced when the host name is built; the
    exact mapping is the builder's, but it must be deterministic so the same CBM name always maps to
    the same host file. Only the REL path changes.
  - **Optional — header name (SD2IEC style), off by default.** Enabled by a config option
    (Claude's call: boolean `settings.directory.x00_header_names` in `config.json`, read through
    `json_int()`, default 0). When on, `.?00` files (`.p00/.s00/.u00/.r00`) on plain filesystems are
    looked up AND listed by the 16-character CBM name in their header rather than by host filename.
    This touches the directory layer of every plain filesystem (SD, flash, network), costs one header
    read per `.?00` file during a listing or lookup, and must be measured on a large SD folder.
- **Network:** works where the underlying filesystem supports random-access writes; elsewhere REL
  opens read-only, and writing reports 26 WRITE PROTECT.

**Drive side:**

- `parseOpen` recognises `name,l,<len>` (`<len>` as one raw byte, per CBM) and, for an existing REL
  file, `name` alone (the type and length come from the directory/P00 header). Opening an existing
  file with a different length is 50 / 64 FILE TYPE MISMATCH as CBM DOS does.
- A new `iecChannelHandlerRel` serves one record at a time (no read-ahead past the record end), so
  EOI lands on each record boundary.
- `P` becomes the real binary form: `P` + channel (`$60` + sa) + record lo + record hi + byte
  position (1-based, 0 treated as 1). Today's text-token `P` command is replaced; any existing caller
  relying on it is listed by the builder before removal.

### Phasing

1. **Phase 1 — move.** Create `CbmDos`/`CmdHdDos`/`Ide64Dos` and `DosHost`, move all of
   `executeData()`'s DOS code and the OPEN parsing, no behaviour change. Verify by driving every
   command family from the console (`exec`, `open`/`read`/`write`) and comparing status replies with
   the pre-move build, plus the native suites.
2. **Phase 2 — REL.** `RELMStream`, both storage kinds, `iecChannelHandlerRel`, binary `P`, `,L`
   OPEN.
3. **Phase 3 — header-name option.** `settings.directory.x00_header_names` lookup and listing.

## Verification

- **Native tests** for `RELMStream` against an in-memory `RELStorage` (record read/write, overflow,
  padding, expansion, `$FF` initialisation), and for D64/D81 REL creation and expansion across a side
  sector boundary (>120 blocks) and, for D81, past the first super-side-sector group.
- **Oracle:** VICE's `c1541` can read REL files from an image, and VDrive itself implements REL. A
  REL file written natively must read back identically through VDrive (`VD+`) and `c1541`; a REL
  file written by VDrive or `c1541` must read correctly natively.
- **Hardware:** a REL program driven from the console (`open 2 name,l,<len>`, `write 15 p…`) on
  lolin-d32-pro, then from a real C64 BASIC program.
- Phase 1: identical status for each command family before and after, recorded in the PR.

## Calls made during review (Claude's, not the user's — check these)

- **VDrive keeps serving disk images while it is active.** With `use_vdrive` on, `set_cwd()`/`open()`
  hand D64/D81 images to VDrive, and this project does not change that routing; native REL on images
  runs when VDrive is off, which is the default (`USE_VDRIVE` is commented out in
  `platformio.ini.sample`). Hardware testing runs with VDrive off.
- **Target REL behaviour is VDrive's (`lib/vdrive/vdrive-rel.c`), quirks included** — status codes
  for `P` past the end, `P` to record 0, reads past the last record, expansion on write. VICE models a
  1541/1581, and matching it is what makes "reads back identically through VDrive" a meaningful check.
  Where 1541 and 1581 differ, follow what VDrive does for that image type.
- **REL entries inside read-only archives (T64, LNX, ARK, …) are out of scope** and keep their current
  behaviour (served as plain bytes).
- **Phase 1 may fix the out-of-bounds channel index** in today's text-form `P`
  (`m_channels[pti[0]]` unbounded). It is undefined behaviour, not a quirk worth preserving; the
  `DosHost` channel lookup is bounds-checked and has no raw-index escape hatch.

## What is left to the builder

- Exact `DosHost` member list and the virtual-method breakdown inside `CbmDos`.
- Which existing commands belong in `CmdHdDos`/`Ide64Dos` rather than `CbmDos` (inventory the current
  `hdpart::`/partition-specific code).
- The list of VDrive-only features still missing natively after REL (e.g. `B-A`, `B-F`, any `M-R`/`M-W`
  gaps, validate differences), written into the spec's follow-up section.
- Naming and file layout inside `drive/dos/`.
- The callers (if any) of today's text-form `P` command.

## Follow-up: remaining VDrive-only features

Inventory of `lib/vdrive/vdrive-command.c` (and `vdrive-iec.c` for OPEN-level behaviour) against
`CbmDos` + `iecDrive` as of the end of Task 4. "Stub" means `CbmDos` recognises the command and logs
it but does nothing. These are what must be written natively before VDrive can be removed.

- `vdrive_command_copy` (C:new=old[,old2...], file concatenation) -- stub in `CbmDos::cmdCbm`.
- `vdrive_command_lockunlock` (L) -- no `case 'L'` in `CbmDos`; falls through to 31.
- `vdrive_command_renameheader` (R-H) -- not present; `R` always takes the file-rename path.
- `vdrive_command_renamepart` (R-P, CMD partition rename) -- not present.
- `vdrive_command_getpartinfo` (G-P) -- stub in `cmdSd2iec`.
- `vdrive_command_deletepart` (D-P) -- not present (`D` is a "duplicate" stub).
- `vdrive_command_chcmdpart` / `vdrive_command_chpart` (CP on 1581/native, `/` partition change inside
  a disk) -- `/` is an empty case in `cmdSd2iec`; `CP` works only through `hdpart::` on CMD/CFS images.
- `vdrive_command_switchtraverse`, `vdrive_command_switch`, `vdrive_command_return` (partition
  traversal for paths like `1581part/file`) -- no native equivalent beyond `D64MFile` sub-partition paths.
- `vdrive_command_chdir` / `mkdir` / `rmdir` on 1581 and native images (`CD:`, `MD:`, `RD:`) -- native
  `MD`/`RD` exist via `MFile`; `CD` is handled by `cmdMeatloafPrefix`; semantic parity with VDrive
  (header-block entry, error codes 39/62/63) is unverified.
- `vdrive_command_validate` / `vdrive_command_validate_worker` (V) -- native `V` calls
  `validateBAM()`; VDrive's GEOS-aware validate and its status codes differ.
- `vdrive_command_format_internal` / `vdrive_command_format_worker` (N:name,id) -- native N: exists;
  VDrive-only: 1541/1571/1581/native-specific layout options.
- `vdrive_command_initialize` (I) -- native `I` is `dosReset()` only (no BAM reload).
- `vdrive_command_position` (P, binary form `P<ch><rec lo><rec hi><pos>`; one trailing CR is always dropped first, vdrive-command.c) --
  RECONCILED 2026-11-24: binary `P` is now native (Task 9; `parsePositionCommand`, REL record positioning through `RELMStream`). The old text
  form (`p<ch> <hi> <mid> <lo>`) is gone.
- `vdrive_command_time` (T-R/T-W) -- native has its own RTC `T-` handling.
- `vdrive_command_memory`, `vdrive_command_memory_write/_exec/_read` (M-R/M-W/M-E) -- native
  `driveMemory`; the 1541/1581 job-queue and ROM-specific behaviour is not emulated.
- `vdrive_command_block` (B-R/B-W/B-A/B-F/B-E/B-P, U1/U2/UA/UB/UI/UJ and user jumps U3-U8) -- native
  B-R/B-W/U1/U2/B-P/B-A/B-F are present as seeks; U3-U8 jumps and UI/U9 are not.
- `vdrive_command_allocate_chain` (internal to validate) -- no native counterpart.
- `vdrive_command_set_error` status table -- native status codes are a subset; DOS-version-specific
  messages (73 text per drive type) are not.
- OPEN-level (`vdrive-iec.c`): REL create (`,L,<len>`) and REL read/write/expand are now native (Phase 2,
  see AGENTS.md 2026-11-24); still VDrive-only: `@:` replace, `,A` append, wildcard `$` filters (`$:*=P`, `$=T`), the
  `#` buffer allocation with buffer number, and the per-channel error `60/61/64` coverage.

Callers of the old text-form `P` (`p<ch> <hi> <mid> <lo>`) found:

- `.claude/skills/commodore64-meatloaf/SKILL.md:457` -- `print#15,"p 2,0,1,0"` documentation example.
- `docs/superpowers/plans/2026-11-23-dos-split-and-rel.md:45` -- hardware baseline script mention.
- No callers in `lib/console/` or other `docs/` (console `f-p`/`b-p` are separate commands).
