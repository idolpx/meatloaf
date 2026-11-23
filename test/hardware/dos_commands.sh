# dos_commands.sh -- command-channel behaviour baseline for the DOS split refactor.
#
# Run on hardware BEFORE and AFTER the refactor and diff the two transcripts:
#     run /sd/dos_commands.sh          (over serial; copy this file to the SD root)
#
# HOW IT RUNS (lib/console/Commands/CoreCommands.cpp run_script, ~78-122):
#   "run" skips blank lines and lines starting with '#', echoes each command,
#   and does NOT stop on a failing line: it records the failure
#   ("Line N: command returned 1") and carries on. So every command below
#   runs, and a non-zero status (e.g. exec answering 31) shows up in the
#   transcript as a "returned" line, which is itself part of the baseline.
#   Lines are limited to 255 chars.
#
# !! DESTRUCTIVE !!  n0:, s0:, r0:, md:, rd:, u2 and the p/m-w writes below
#   modify the mounted image. Point the SCRATCH paths at COPIES only.
#   Never point them at an image you care about.
#
# EDIT THESE PLACEHOLDERS to real files on your SD card (search/replace):
#   SCRATCH_D64 = /sd/content/d64/scratch.d64      (a writable COPY of a d64)
#   SCRATCH_DHD = /sd/content/hd/dhd/scratch.dhd   (a COPY of a CMD HD image)
#   SCRATCH_HDD = /sd/content/hd/hdd/scratch.hdd   (a COPY of an IDE64 CFS image)
#   SUBDIR      = x                                (created by md:x below)
#
# Syntax rules (AGENTS.md): exec DOS commands are LOWERCASE; binary bytes are
# written 0x + an even run of hex digits; M-W takes a count byte.
# "use" follows the console cwd, so "cd <image>" also moves the drive there.

# ---------------------------------------------------------------- setup
use 8
cd /sd/content/d64
cd scratch.d64
pwd

# ---------------------------------------------------------------- D64 family
exec i0
exec ui
exec pwd
exec md:x
exec cd:x
exec pwd
exec cd_
exec pwd
exec rd:x

# memory commands (M-R: lo hi count ; M-W: lo hi count data...)
exec m-r0x000009
exec m-w0x000003010203
exec m-r0x000003

# block commands without a block window -> expect 31
open 2 "#"
exec b-p 2 5
exec u1 2 0 18 1
channels
exec b-r 2 0 18 1
exec b-p 2 5
exec f-p 2 0
close 2

# command channel via open/read/write on 15
open 15 i0:
read 15
write 15 i0:
read 15

# scratch-copy-only destructive commands
exec n0:test,01
exec s0:nofile
exec r0:b=a
exec md:x
exec rd:x

# partition / misc
exec cp1
exec t-r
exec xr:dos1541
exec ej+
exec vd-
exec auth:a,b

# binary P, current text-token form (drive.cpp ~2443): p <ch> <lo> <hi> <n>
exec p2 0 0 1

# unknown command and a lone 0x00 byte (what C64 OS sends on boot)
exec zz
exec 0x00
close

# ---------------------------------------------------------------- DHD (CMD HD)
cd /sd/content/hd/dhd
cd scratch.dhd
pwd
exec cp1
exec cp0
exec cp256
exec cp255
exec i0

# ---------------------------------------------------------------- HDD (IDE64 CFS)
cd /sd/content/hd/hdd
cd scratch.hdd
pwd
exec cp1
exec cp0
exec cp256
exec cp17
exec i0

# ---------------------------------------------------------------- teardown
close
cd /sd
use 0
