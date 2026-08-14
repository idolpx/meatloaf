---
name: c64-jiffydos
description: Commodore 64/128 JiffyDOS reference — use whenever the user is working in a C64/128 emulator (VICE, Ultimate 64, etc.) and you see signs of the JiffyDOS replacement ROM: the boot saying "jiffydos v6.01 (c)1989 cmd", the user mentioning "basic wedge", "@" commands, or "jiffydos" specifically. Also trigger when file operations are painfully slow and the user hasn't already enabled fast disk I/O — JiffyDOS is the canonical fix. This skill is a SUPPLEMENT to the c64-basic skill: c64-basic covers writing BASIC code (bc64 compiler, POKEs, hardware), while this skill covers the JiffyDOS ROM's built-in disk commands and shortcuts used at the READY. prompt. Always check whether JiffyDOS is active before suggesting disk operations — the one-key shortcuts are far more efficient than load"$",8 etc. If you see the user typing open/close/print# boilerplate for disk access on a system that has JiffyDOS, flag it and show the JiffyDOS shortcut.
---

# JiffyDOS — Supplementary Skill for Commodore 64/128 Debugging

This skill is a **companion** to the `c64-basic` skill. The `c64-basic` skill covers writing C64 BASIC v2 code (bc64 compiler, POKEs, SID, sprites, etc.). This skill covers everything about the **JiffyDOS replacement ROM** — detecting it, using its built-in commands, configuring it in emulators, and troubleshooting it.

When both skills apply, use both.

---

## 1. Detecting JiffyDOS

JiffyDOS is a drop-in replacement for the Commodore Kernal ROM and disk drive ROMs. It's **not always present** — the user may be running stock ROMs. Check for it:

### At boot (cold start)
The boot screen says one of:
- **JiffyDOS is active** — you'll see: `jiffydos v6.01 (c)1989 cmd` (or similar version string) on the blue boot screen **in place of** the usual `**** commodore 64 basic v2 ****` or `**** cbm basic v2 ****` message.
- **Stock ROM** — you'll see the standard `**** commodore 64 basic v2 **** 64k ram system  xxxxx basic bytes free`

### Quick test at the READY. prompt
If the boot screen is gone (the user already cleared the screen), ask them to type `@` and press RETURN — **JiffyDOS** responds with `drive not ready` or the drive error channel. **Stock ROM** responds with `?syntax error` — JiffyDOS is not active.

### In VICE emulator
Check `Settings → Cartridge/ROM settings → Kernal ROM` for the loaded ROM. JiffyDOS .rom files are typically named `jiffydos_c64.rom` or similar. Also check `Settings → Drive Settings → ROM settings` for JiffyDOS drive ROMs.

### On Ultimate 64 / Ultimate 64 Elite
JiffyDOS is available via the Ultimate's firmware menu. The user may need to enable it in the cartridge settings.

---

## 2. Disk Drive Error Channel

The most-used JiffyDOS command:

| Command | What it does |
|---------|-------------|
| `@` | **Read the disk drive error channel** — shows the last error from the drive (e.g., `00, okay`, `74, drive not ready`, `62, file not found`). Always use this after an operation fails. |

Stock BASIC requires `open 15,8,15: input#15,e,e$,t,s: close 15` to get the error — with JiffyDOS it's just `@`. Always suggest `@` after failed disk operations before trying other debugging steps.

### Extended DOS commands via `@`

The `@` command is also the gateway to **extended DOS commands** on advanced storage devices. Just append the command after `@` and it gets sent to the drive's command channel. This works with any device that listens on the IEC command channel (secondary address 15):

| Device | Example extended commands |
|--------|-------------------------|
| **sd2iec** (sd card reader) | `@cd:dirname` — change directory, `@cd:_` — back to root, `@cd:` — show current directory path, `@md:newdir` — make directory, `@rd:dirname` — remove directory, `@cp:src=dest` — copy, `@rename:new=old` — rename, `@t:name` — type file, `@mkdir:name` — make directory, `@rmdir:name` — remove directory, `@cwd:name` — change working directory |
| **64hdd** (hard drive solution) | dos commands via the same command channel — directory navigation, file management |
| **cmd hd** / **cmd fd** | `@s:name` — scratch, `@r:new=old` — rename, `@c:new=old` — copy, `@v` — validate, `@n:name,id` — format |
| **meatloaf** (esp32) | `@m:command` — send commands to the meatloaf networking firmware (e.g., rtc-related: `@m:rtc get` to read, `@m:rtc set yyyy-mm-dd hh:mm:ss` to set), `@s:command` — sd card management commands when meatloaf's sd is enabled |
| **ide64** | dos commands for hard disk partitions, directory navigation, file operations |

**The pattern is always the same:** `@<command>` sends the command to the device on the IEC bus. The device interprets it based on its firmware. JiffyDOS just handles the transport — the `@` prefix is your universal "pass this to the drive" mechanism.

If an extended command doesn't work, check:
1. Does the device support it? (not all devices implement all commands)
2. Is the command spelling correct? (sd2iec uses `cd`, not `chdir`, etc.)
3. Type `@` afterwards to check the device's error response for debugging

---

## 3. DOS Wedge Commands (the "@" commands)

These are typed at the `READY.` prompt and operate on the disk drive directly. No `open 15,8,15` boilerplate needed.

| Command | What it does |
|---------|-------------|
| `@` | Read disk drive error channel (see above) — also used as prefix for extended DOS commands to any device on the IEC bus |
| `@$` | **Display disk directory** — lists filenames to screen **without overwriting a program in memory** (unlike stock `load"$",8` which clobbers BASIC memory) |
| `@i` | **Initialize** (reset) the disk drive — recalibrates the head and re-reads the BAM |
| `@v` | **Validate** disk — rebuilds the Block Availability Map (BAM), fixing directory inconsistencies. Use when files appear to be missing or there are disk errors |
| `@n:diskname,id` | **Format** a new disk — `diskname` can be up to 16 chars, `id` is exactly 2 characters (e.g. `@n:work,01`). **WARNING:** erases everything |
| `@n:diskname` | Short format — auto-generates an id |
| `@s:file1,file2,...` | **Scratch** (delete) files — supports wildcards: `*` for any group, `?` for single char. **`@s:*` deletes everything** |
| `@r:newname=oldname` | **Rename** a file |
| `@c:newname=oldname` | **Copy** a file on the same disk |
| `@l:filename` | **Lock / unlock** a file — toggles the file's protection flag (prevents accidental scratch) |
| `@d:filename` | **List a basic program** — shows the program's lines to screen (catalog-style, tokenized back to text). Great for quickly checking what a .prg contains |
| `@t:filename` | **Type** — lists an ascii/petscii text/seq file to screen. Like `cat` for commodore |
| `@uj` | **Hard reset** the disk drive — power-on equivalent |
| `@#device` | **Set default device number** — e.g. `@#9` switches to device 9 so you don't need `,9` on every load |
| `@q` | **Quit** — disables JiffyDOS wedge commands, falling back to stock ROM behavior. Useful if a game has compatibility issues |
| `@f` | **Disable function key macros** — turns off f1-f8 assignments |
| `@g` | Set **interleave gap** — performance tuning for the gap between disk sectors (rarely needed) |
| `@b` | Disable 1541 **head rattle** — the "bump" sound when the drive locks/unlocks |
| `@o` | **Un-new** — restores a basic program after `new` cleared it (if memory hasn't been overwritten). Saves a lot of heartache |
| `@p` | **Toggle printer output** — redirects output to printer |
| `@x` | Set **destination device** for copy operations between two drives |

### Wildcard patterns in @ commands

- `*` — matches any number of characters (greedy)
- `?` — matches exactly one character

Examples:
- `@s:backup.*` — deletes all files named backup with any extension
- `@s:game?` — deletes game1, gamea, etc.
- `@s:g*,t*,*test*` — multiple patterns in one command (separate with commas)

---

## 4. File Operation Shortcuts (the prefix characters)

These are single-character prefixes you type **before a filename** at the READY. prompt. They replace the full `load"filename",8` or `save"filename",8` commands.

| Shortcut | Expansion | What it does |
|----------|-----------|-------------|
| `/filename` | `load"filename",8` | **Load a BASIC program** from disk |
| `↑filename` | `load"filename",8` + `run` | **Load and run** a BASIC program in one step |
| `←filename` | `save"filename",8` | **Save** the BASIC program currently in memory |
| `%filename` | Load to stored address | **Load a machine-language program** to the address encoded in its PRG header |
| `*"filename"type` | Copy | **Copy** a file — where `type` is one of: `S` (sequential), `P` (program), `U` (user), `R` (relative) |
| `'filename` | Verify | **Verify** a file on disk matches memory |
| `f filename` | Load and jump | **Load and run a machine-language file** |

### Key: How to type these symbols on a C64 keyboard

| Symbol | C64 Keys | Notes |
|--------|----------|-------|
| `↑` (up arrow) | Hold **Shift** + press **K** | Appears as `↑` |
| `←` (left arrow) | Hold **Shift** + press **H** | Appears as `←` |
| `%` | Shift + **5** | Same as modern keyboards |
| `/` | Slash key | Same as modern keyboards |
| `'` (quote) | Single-quote key | Also does string delimiting in BASIC |
| `f` | F-key or letter F | Preceded by a space usually |

**If JiffyDOS is active and the user types these shortcuts and gets `?SYNTAX ERROR`,** it means the wedge got disabled. Tell them to type `@q` then `sys 58451` to re-enable the wedge (see section 7).

---

## 5. Function Key Macros

When JiffyDOS is first booted, the function keys are pre-assigned:

| Key | Inserts / Executes |
|-----|-------------------|
| **F1** | `@$` — Display disk directory |
| **F3** | `/` — Prepare a BASIC load |
| **F5** | `↑` — Prepare a load-and-run |
| **F7** | `%` — Prepare an ML load |

| Shift + | |
|---------|-|
| **F2** | `@d:` — Prepare a directory listing command |
| **F4** | `@t:` — Prepare a type/text command |
| **F6** | `←` — Prepare a save command |
| **F8** | `@s:` — Prepare a scratch command |

The function key macros are **disabled** with `@f`. They can be re-enabled with `sys 58551`.

After `@$` displays a directory, filenames appear on screen and selecting them with function keys pastes the filename into the command line — the user can press F3, then F1 (or whatever key selects the file) to load it without typing the filename.

---

## 6. Control Key Shortcuts

| Keys | What it does |
|------|-------------|
| **Ctrl + S** | Stop/freeze a scrolling `list` output |
| **Ctrl + A** | Toggle ALL file selection for multi-file copy |
| **Ctrl + D** | Toggle default drive between device 8, 9, etc. |
| **Ctrl + P** | Screen dump to printer |
| **Ctrl + W** | Toggle single file for copy |

### shift + run/stop

**shift + run/stop** loads and runs the first program on the disk automatically — no typing required at all. This works on stock ROMs but with JiffyDOS it benefits from the faster load speed.

---

## 7. Re-Enabling JiffyDOS (if it was disabled)

If the wedge gets disabled (via `@q`, or a program reset clears things):

| Command | What it restores |
|---------|-----------------|
| `sys 58451` | Re-enable JiffyDOS wedge commands (C64) — **most common one you'll use** |
| `sys 58551` | Re-enable JiffyDOS function key macros (C64) |
| `sys 65137` | Re-enable JiffyDOS commands (C128 mode) |
| `sys 58492` | Re-enable JiffyDOS (VIC-20) |

**Diagnosis flowchart:**
1. User typed `@` → `?SYNTAX ERROR`? → **Run `sys 58451`** first.
2. If that works, the wedge is back.
3. If that also gives a `?SYNTAX ERROR`, JiffyDOS isn't loaded at all — check the emulator ROM configuration.
4. If `@` works, try `sys 58551` to restore function key macros.

---

## 8. Enabling JiffyDOS in Emulators

### VICE (x64, x64sc, x128, etc.)

**Kernal ROM:**
- `Settings → Cartridge/ROM settings → Kernal ROM` → load a `jiffydos_c64.rom` file
- Or place the file in VICE's ROM directory and name it so VICE auto-loads it (consult VICE docs for your version)

**Drive ROM (needed for fast loading):**
- `Settings → Drive Settings → Drive 8 ROM` → load `jiffydos_1541.rom`
- Repeat for drive 9, drive 10, etc.
- JiffyDOS drive ROMs are needed for the ~10× speed increase. Without them, the Kernal wedge commands still work but loading is at normal speed.

**Both** Kernal and drive ROMs must be loaded for the full JiffyDOS experience. If only the Kernal ROM is loaded, you get the wedge commands (`@`, `/`, `↑`, etc.) but still at stock speeds.

### Ultimate 64 / Ultimate 64 Elite
- Access the Ultimate's firmware configuration menu
- JiffyDOS is built into the Ultimate firmware — no separate ROM file needed
- Enable JiffyDOS in the "ROM" or "C64 System" settings

---

## 9. Performance Characteristics

| Setup | Load time (202-block PRG on 1541) |
|-------|-----------------------------------|
| Stock Kernal + Stock 1541 | ~124 seconds |
| JiffyDOS Kernal + Stock 1541 | Wedge commands OK, still ~124s |
| JiffyDOS Kernal + JiffyDOS 1541 | **~12 seconds** |
| JiffyDOS + 1571 | ~9 seconds |
| JiffyDOS + 1581 | ~8 seconds |

The speedup is because JiffyDOS uses a custom serial protocol that keeps the bus busy almost continuously, versus stock Commodore's "one byte, pause, next byte" approach.

**Wedge commands work regardless of load speed** — the `@` commands and `/` shortcuts work with any drive. The speed boost only kicks in when both the computer and drive have JiffyDOS ROMs.

---

## 10. Compatibility Notes

### What works with JiffyDOS
- All standard Commodore file types (PRG, SEQ, USR, REL)
- GEOS (auto-detected and given compatible timing)
- Most commercial software
- Multi-drive setups
- Files created under JiffyDOS are **byte-identical** to those from stock ROMs — fully portable

### What doesn't work / workarounds
- **Copy-protected software** that relies on exact drive timing may fail. Use `@q` to disable the wedge and try again. If still fails, switch to stock Kernal ROM.
- **Fastload cartridges** (Action Replay, etc.) — JiffyDOS replaces the Kernal, so cartridges that also replace the Kernal conflict. Use one or the other.
- **Custom Kernal patches** — JiffyDOS replaces the entire Kernal, so other ROM patches won't survive.

### Switching between JiffyDOS and stock
For a quick toggle in VICE:
- **Soft toggle**: `@q` disables wedge (can re-enable with `sys 58451`)
- **Full ROM switch**: Use VICE's `Settings → Cartridge/ROM settings → Kernal ROM` and start a new session

---

## 11. Typical Workflow Examples

### Basic: "What's on the disk?"

Prompt: *"I just booted my C64 emulator and want to see what's on the disk."*

```
@$               ← displays directory without overwriting memory
```

### Normal: "Load and run a game"

Prompt: *"I want to load the file 'ASTROIDS' and run it."*

```
@$               ← first check the directory to confirm the file exists
/astroids        ← (or if the filename is in uppercase: /ASTROIDS)
run              ← actually runs it
-- OR, in one step --
↑ASTROIDS        ← load and run in one
```

### Advanced: "My disk has errors, help me fix it"

Prompt: *"I tried loading something but got 'FILE NOT FOUND' even though the directory shows it."*

1. `@` — check the error channel. If it shows `26, write protect on` or the directory was wrong, proceed.
2. `@v` — validate the disk (rebuilds BAM). This often fixes phantom errors.
3. `@$` — check the directory again.
4. If the problem persists, ask "Has this disk been used on a non-JiffyDOS system? Files are compatible but the disk needs initialization."

### Troubleshooting: "JiffyDOS commands don't work"

Prompt: *"I typed @$ but got ?SYNTAX ERROR"*

1. Check if JiffyDOS is actually loaded: ask the user about the boot screen message or to do a cold reset.
2. If it was loaded but got disabled: `sys 58451` re-enables the wedge.
3. If `sys 58451` also errors: JiffyDOS Kernal ROM isn't loaded. Guide them through VICE settings.
4. If the wedge works but loading is still slow: they need the drive ROM, not just the Kernal ROM.

---

## 12. Relationship to the c64-basic skill

| Situation | c64-basic | This skill (c64-jiffydos) |
|-----------|-----------|--------------------------|
| Writing a BASIC program | ✅ Primary reference | ❌ Not needed |
| Debugging disk I/O code | ❌ Not needed | ✅ Use for the wedge shortcuts |
| Loading/saving files | ❌ Not needed | ✅ Primary reference |
| Detecting JiffyDOS | ❌ | ✅ |
| Formatting/validating disks | ❌ | ✅ |
| POKEs, sprites, SID, joystick | ✅ Primary reference | ❌ Not needed |
| bc64 compiler syntax | ✅ Primary reference | ❌ |
| Meatloaf HTTP client | ✅ Refers to meatloaf-networking | ❌ |
| Configuring emulator ROMs | ❌ | ✅ |

When a conversation revolves around disk operations in an emulator context, this skill takes priority. When it revolves around writing new BASIC code, c64-basic takes priority. When the user is doing both, use both.

---

## 13. Quick Reference Card (most-used commands)

```
@           — Check disk error
@$          — List directory (safe for memory)
/name       — Load BASIC program
↑name       — Load and run BASIC program
←name       — Save BASIC program
%name       — Load ML program
@s:name     — Delete file
@r:new=old  — Rename file
@c:new=old  — Copy file on same disk
@n:disk,id  — Format disk (erases everything!)
@v          — Validate/repair disk
@i          — Reset drive
@o          — Un-new (recover BASIC after new)
sys 58451   — Re-enable wedge if disabled
shift+ro/st — Load+run first file on disk
```

---

## 14. Cross-references

- For writing Commodore 64 BASIC v2 programs (bc64 compiler, POKEs,
  sprites, SID, hardware control), see the **c64-basic** skill.
- For the Meatloaf full-mode HTTP client protocol and JSON Pointer
  syntax for API access, see the **meatloaf-networking** skill.
- For the unattended debug cycle (injecting BASIC, reading screen,
  capturing serial logs via Ultimate 64 REST API), see the
  **c64-meatloaf-debug** skill.
