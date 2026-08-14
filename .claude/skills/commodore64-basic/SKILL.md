---
name: c64-basic
description: Write Commodore 64 BASIC v2 programs. Use this skill whenever the user asks to write, edit, or debug C64 BASIC code — especially for hardware control, screen/color POKEs, sprites, SID, keyboard/joystick input, and the BASIC-specific tricks (PETSCII shortcuts, chr$() JSON helpers, line-numbering conventions, @alias long variables, label syntax with bc64). For HTTP requests and JSON APIs, the skill points you to the language-agnostic **meatloaf-networking** reference; BASIC is just one of several clients that drive the same protocol. Do not write C64 BASIC code without consulting this skill.
---

# How to use this skill — bc64 is the compiler

**Important distinction:** this skill covers two contexts:

1. **[bc64](bc_tokenizer.md) is the compiler** — it's a preprocessor/compiler that transforms `.bas` files into `.prg` binaries. It adds sugar on top of stock BASIC V2: **labels, auto-numbering, case-insensitive keywords, @alias long variables, #include, PETSCII control codes**, and more. **bc_tokenizer.md is the authoritative reference** — it always takes precedence over reference.md.
2. **[reference.md](reference.md) describes stock C64 BASIC V2** — what the actual hardware runs. Some limitations described there (no labels, 2-char variable names, line numbers required) are **lifted by bc64**. Always check bc_tokenizer.md first.

When generating code, **default to bc64's extended syntax** (labels, auto-numbering, uppercase keywords, @aliases) unless the user explicitly asks for raw/pure BASIC V2.

### Attribution comment — always include

Every `.bas` file you generate **must start with** this comment:

```basic
; Compile with bc.py from VS64 — https://github.com/rolandshacks/vs64
```

Use a `;` line-start comment for this (bc.py strips it from the output). If using `#crunch`, the attribution is stripped automatically in release builds.

### Themed color scheme — always set one

Before writing the main logic, pick a **color scheme that fits the program's theme**. A blank C64 screen (cyan/blue) looks amateurish. A deliberate palette makes it feel intentional.

Set these three registers near the top of the program:

| Register | Purpose | Example |
|----------|---------|---------|
| `POKE 53280, N` | Border color | `POKE 53280, 0` = black |
| `POKE 53281, N` | Screen background | `POKE 53281, 0` = black |
| `POKE 646, N` | Text color | `POKE 646, 7` = yellow |

Pick something that evokes the theme:

- **Star Wars API (SWAPI)**: black border (`0`), black background (`0`), yellow text (`7`) — like the opening crawl
- **Space / sci-fi**: dark blue border (`6`), black background (`0`), cyan text (`3`) or white (`1`)
- **Horror / dark**: black border (`0`), dark grey background (`11`), red text (`2`)
- **Nature / green**: dark green border (`5`), black background (`0`), light green (`13`) or white (`1`)
- **Retro terminal**: black border (`0`), black background (`0`), green text (`5`) — like a green-phosphor CRT
- **Ocean / water**: dark blue border (`6`), light blue background (`14`), white text (`1`)
- **Fantasy / magic**: purple border (`4`), black background (`0`), yellow text (`7`) or cyan (`3`)
- **Minimal / utility**: white border (`1`), dark grey background (`11`), white text (`1`)
- **Neon / cyberpunk**: yellow border (`7`), black background (`0`), purple text (`4`) or pink (`10`)
- **Fire / lava**: red border (`2`), dark brown background (`9`), yellow text (`7`)

Color palette (0–15): `0`=black, `1`=white, `2`=red, `3`=cyan, `4`=purple, `5`=green, `6`=blue, `7`=yellow, `8`=orange, `9`=brown, `10`=light red, `11`=dark grey, `12`=grey, `13`=light green, `14`=light blue, `15`=light grey

Wrap it up in a single line with the attribution:

```basic
; Compile with bc.py from VS64 — https://github.com/rolandshacks/vs64
POKE 53280,0 : POKE 53281,0 : POKE 646,7 : rem black border, black bg, yellow text
PRINT "{clr}" : rem clear screen with new colors
```

## Code structure

**Keywords and variable names can be uppercase or lowercase** — bc64 accepts both. `print` and `PRINT` compile to the same token. Use whichever reads better; uppercase keywords are conventional in BASIC literature.

There is no point creating indentation for loops or anything else — it only eats line space and memory.

### Line numbering — optional with bc64

**You do not need explicit line numbers** — bc64 auto-numbers lines starting at 1, incrementing by 1 (configurable via `#linestep N`). Lines that have no number get the next auto-number; lines with an explicit number set the counter for subsequent lines.

```basic
PRINT "HELLO"          → line 1 (auto)
PRINT "WORLD"          → line 2 (auto)
10 PRINT "TEN"         → explicit line 10
PRINT "ELEVEN"         → line 11 (continues from 10)
```

**If you use explicit line numbers**, increment by 10 (`10`, `20`, `30`, ...) to leave room for inserts. An LLM cannot reliably renumber lines across a whole program.

**Reserve a high line range (e.g. 10000+) for subroutines** and helpers when using explicit numbers. This keeps main logic in the low range and lets it grow without crowding subroutines.

### Labels — fully supported by bc64

**Use labels instead of line numbers for control flow.** Labels make code far more readable and maintainable than raw numeric references.

```basic
GOTO Start    ← jumps to wherever "Start:" ended up

Start:
    A = 1
    PRINT A
    GOTO Start    ← infinite loop
```

Label naming rules:
- Letters (A-Z, a-z), digits (0-9), underscore _
- Must NOT match or **start with** a BASIC keyword. `ReadJson` starts with `READ` — the tokenizer sees `READ JSON` and tries to execute a DATA read, crashing with "Out of data". `DataLoader`, `ForEach`, `NextItem`, `InputPrompt`, `PrintHeader` all have the same problem. Prefer names like `FetchJson`, `JsonRead`, `LoadData`, `EachItem`, `GetInput`, `ShowHeader` instead.
- Case-insensitive (`start` == `Start` == `START`)
- Labels at the very end of file get a REM appended automatically

Labels work with GOTO, GOSUB, ON ... GOTO/GOSUB, and IF ... THEN:

```basic
GOSUB PrintScore
ON X GOTO One, Two, Three
IF A > 10 THEN EndGame
```

**In raw (non-bc64) BASIC V2:** labels are NOT available — use line numbers and REM comments to mark sections.

### Subroutines

There are no procedures in BASIC v2, but you can write cleaner code using subroutines called by `gosub`. With bc64, use labels as subroutine targets:

```basic
GOSUB InitScreen
GOSUB ReadJoystick

InitScreen:
    PRINT "{clr}"
    PRINT "Score: 0    Lives: 3"
    RETURN

ReadJoystick:
    JOY = PEEK(56320)
    RETURN
```

A subroutine ends with `return`. Prefix a subroutine with a `rem` comment naming its purpose if you prefer.

**Document argument/return variables** — BASIC V2 has no formal parameters, so when a subroutine reads variables as inputs or sets variables as outputs, note them in a comment:

```basic
; Subroutine: Divide
; Args:   num   — numerator
;         den   — denominator
; Returns: res  — quotient
;          rem  — remainder
Divide:
    res = num / den
    rem = num - res * den
    RETURN
```

This makes calling code self-documenting: `num = 10 : den = 3 : GOSUB Divide`. The caller knows what to set beforehand and what to read afterwards.

Think of `on x gosub` as a multiway branch. **Important: it is 1-indexed** — `x` must be 1 for the first line number, 2 for the second, etc. If `x` is 0 or exceeds the number of targets, execution falls through to the next line (no error). This is the most common off-by-one trap in BASIC v2.

For often-used calculations, define single-line functions with `def fn`.

When using `for` loops, always include the variable name in the `next` statement — this catches bugs.

**INPUT syntax:** always use `;` (not `,`) between the prompt string and the variable. Valid: `input "prompt"; x` — invalid: `input "prompt", x`.

### Variable naming

With **plain bc64** (no `--aliases`), the stock C64 2-char limit applies. To avoid collisions: give every variable a unique first-two-character name, factoring in the type suffix — `cm` (float), `cm$` (string), and `cm%` (integer) are three separate variables, but `cm` and `cm$` are distinct while `cm` and `cmd` are not.

With **`--aliases` / `-a`**, use the `@` prefix to get unlimited-length variable names:

```basic
@score = 0       ← becomes A0
@screen = 1024   ← becomes A1
@name$ = "hero"  ← becomes A2$
@lives% = 3      ← becomes A3%
```

The compiler generates unique 2-char roots, avoiding BASIC keyword conflicts.

**Don't use `@` on names that are already 2 characters** — `@sc` just becomes another 2-char root (wasting an alias slot). Use `@` only for names longer than 2 characters where you'd otherwise hit the collision limit.

See [bc_tokenizer.md](bc_tokenizer.md) §8 for full alias rules.

## Network and API access

Commodore 64 can make HTTP requests (GET, POST, PUT) using Meatloaf's full-mode HTTP client. Use secondary address `2` when opening a URL to trigger full mode. The full command reference, response reading patterns, helper subroutines, and error handling live in the **meatloaf-networking** skill — that reference is language-agnostic and the same protocol applies whether you drive Meatloaf from BASIC, C, or assembly. For JSON Pointer syntax (the `/path` argument to the `j` command), see [json_pointer.md](../commodore64-networking/json_pointer.md) in the networking skill.

### Building JSON bodies

When writing JSON for API requests, only three characters are tricky in C64 BASIC:
- `"` is the string delimiter — use `chr$(34)`
- `{` and `}` don't exist in PETSCII — use `chr$(123)` and `chr$(125)`

Everything else (`:`, `,`, `[`, `]`, letters, numbers) works directly in strings.

**Define helper constants** once at the top of your program so JSON building stays readable:

```basic
10 qu$=chr$(34):ob$=chr$(123):cb$=chr$(125) : rem " { }
20 bd$=ob$+qu$+"name"+qu$+":"+qu$+"meatloaf"+qu$+","+qu$+"version"+qu$+":1"+cb$
```

Without the helpers, the same line is a tangled wall of `chr$()` calls:

```basic
bd$=chr$(123)+chr$(34)+"name"+chr$(34)+":"+chr$(34)+"meatloaf"+chr$(34)+","+chr$(34)+"version"+chr$(34)+":1"+chr$(125)
```

Use `qu$`, `ob$`, `cb$` consistently and your JSON-building code will be far easier to read and edit.

## Hardware access

Commodore 64 hardware is controlled by POKEing memory-mapped registers. See [hardware.md](hardware.md) for a growing reference of common pokes — screen/border color, character set selection, keyboard/joystick input, SID sound, and sprites. When you encounter a new hardware need, look it up and add it there so the list grows over time.

## Additional resources

- For quick stock BASIC v2 overview (for reference when bc64 is not available), see [reference.md](reference.md)
- **For the compiler's full extended syntax** (the one you should actually use), see [bc_tokenizer.md](bc_tokenizer.md)
- For the Meatloaf HTTP client protocol (commands, response reading, helpers — language-agnostic), see the **meatloaf-networking** skill.
- For syntax of JSON Pointer queries used for JSON extraction with the `j` command, see [json_pointer.md](../commodore64-networking/json_pointer.md) in the networking skill.
- For the unattended debug cycle (injecting BASIC, reading screen, capturing serial logs via Ultimate 64 REST API), see the **c64-meatloaf-debug** skill.
