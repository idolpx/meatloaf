VS64 bc64 — Complete BASIC V2 Compiler Reference for AI Code Generation

    Target audience: LLMs (Claude, GPT, etc.) that need to generate Commodore 64 BASIC V2 source
    files that compile cleanly with the bc64 BASIC compiler ($ bc64 -o game.prg main.bas).

1. Quick Summary
Property 	Value
Executable 	`bc64` (wrapper script, callable from anywhere)
Input 	Plain-text .bas files
Output 	.prg binary with 2-byte load address prefix ($0801)
Dialect 	Commodore 64 BASIC V2 (optional Tuned Simon's BASIC / TSBneo)
Load address 	$0801 (2049 decimal) — standard C64 BASIC start
External deps 	None — fully self-contained Python

Invocation:

bc64 -o output.prg input.bas                  # basic compile
bc64 -o output.prg -c input.bas               # crunched (release)
bc64 -o output.prg -u input.prg               # decompile .prg → .bas

All CLI Flags (exact, from source)
Short 	Long 	What It Does
-h 	--help 	Print usage and exit
-o FILE 	--output FILE 	Output .prg filename
-m FILE 	--map FILE 	Generate source-map file
-n 	--noext 	Disable BASIC extensions (default anyway)
-l 	--lower 	Enable lowercase charset mapping
-t 	--tsb 	Enable Tuned Simon's BASIC (TSBneo) extended tokens
-a 	--aliases 	Enable @alias variable-name preprocessing
-I DIR 	--include DIR 	Add include search directory (repeatable)
-c 	--crunch 	Crunch: strip spaces, remove REMs, renumber lines
-p 	--pretty 	Pretty-print output
-v 	--verbose 	Verbose: dump compiled lines to stdout
-d 	--debug 	Debug: extended debug output (token-level)
-u 	--unpack 	Decompile mode — .prg → readable .bas

Multiple input files are allowed:

bc64 -o game.prg main.bas utils.bas data.bas

2. How the Compiler Works (Mental Model)

┌─────────────────────────────────────────────────────────────┐
│  .bas source (plain text)                                   │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 1. Preprocessor                                     │    │
│  │    • #include "file.bas"  (recursive)               │    │
│  │    • #lowercase / #uppercase / #cset0 / #cset1      │    │
│  │    • #linestep N                                    │    │
│  │    • #lineskip N                                    │    │
│  │    • @alias substitution (if --aliases)             │    │
│  │    • Lines starting with # or ;  → comments          │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 2. Auto-numbering + Label resolution                │    │
│  │    • Lines without numbers get last_line + step     │    │
│  │    • Labels (name:) resolve to next auto-line-num   │    │
│  │    • GOTO label / GOSUB label → resolved line nums  │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 3. Tokenization                                     │    │
│  │    • PRINT → $99, GOTO → $89, etc.                  │    │
│  │    • {? → $99} shorthand works                      │    │
│  │    • PETSCII control codes in strings {clr} etc.    │    │
│  │    • Raw strings with single quotes 'Hello'         │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 4. (Optional) Crunching (--crunch / release build)  │    │
│  │    • Strip all spaces between tokens                │    │
│  │    • Remove REM lines                               │    │
│  │    • Renumber lines 1,2,3...                        │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 5. PRG binary output                                │    │
│  │    [$01 $08]  ← 2-byte load address                 │    │
│  │    [next][num][tokens]0x00 ← each line              │    │
│  │    [0x00 0x00]  ← end-of-program marker             │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘

3. Source File Format Rules
3.1 Line Numbers — Optional

Lines do not need explicit numbers. The compiler auto-numbers starting at 1, incrementing by 1
(or by whatever #linestep you set).

PRINT "HELLO"          → line 1
PRINT "WORLD"          → line 2
10 PRINT "TEN"         → explicit line 10
PRINT "ELEVEN"         → line 11 (continues from 10)

Line number collision is a compile error — you can't have two 10 lines.
3.2 Labels

Use LabelName: on its own line or before code. Labels are resolved to the auto-generated line number.

GOTO Start             ← jumps to wherever "Start:" ended up

Start:
    A = 1
    PRINT A
    GOTO Start         ← infinite loop

Label naming rules:

    Letters (A-Z, a-z), digits (0-9), underscore _
    Must NOT match or **start with** a BASIC keyword. `ReadJson` starts with `READ` — the tokenizer sees `READ JSON` and tries to execute a DATA read, crashing with "Out of data". Prefer `FetchJson` or `JsonRead` instead.
    Labels at the very end of file get a REM statement appended automatically

3.3 Comments
Syntax 	Behavior
# anything 	Preprocessor comment — line must start with `#`. Stripped entirely.
; anything 	Preprocessor comment — line must start with `;`. Stripped entirely. **`;` mid-line is NOT a comment** — it's a BASIC separator (like ` `).
REM text 	BASIC REM — kept in debug, removed in crunch

# This is a preprocessor comment (gone)
; This too (gone)
10 PRINT "hello" : REM inline comment after code
20 REM this stays in debug, vanishes in crunch

⚠️ **`;` only works as a comment at the very start of a line.** Mid-line `;` is a BASIC statement separator — it does NOT introduce a comment. Use `: REM text` for inline comments after code.

3.4 Multi-statement Lines

Use : as statement separator (just like C64 BASIC):

10 A = 1 : B = 2 : PRINT A + B

4. Preprocessor Directives

All directives start with # at the beginning of a line.
Directive 	Aliases 	Effect
#include "file.bas" 	#include 'file.bas' 	Recursively include another .bas file
#lowercase 	#lower, #cset1 	Map text to C64 lowercase charset
#uppercase 	#upper, #cset0 	Map text to C64 uppercase charset
#linestep N 	— 	Set auto-number increment (default: 1)
#lineskip N 	— 	Next line jumps to next multiple of N
Include File Resolution Order

    Absolute path
    Relative to current file's directory
    Relative to CWD
    Each -I include path (in order)

#linestep and #lineskip Example

#linestep 10

50 REM *** START ***

main:
#lineskip 500
    PRINT "main loop"
    GOTO main

sub:
#lineskip 1000
    REM a subroutine
    RETURN

Resulting line numbers: 50, 500, 510, 1000

    Note: In --crunch (release) mode, #linestep/#lineskip are ignored — lines are renumbered 1,2,3...

5. Tokenization Details
5.1 Full BASIC V2 Token Table

The compiler recognizes every C64 BASIC V2 keyword and maps it to its 1-byte token:
Keyword 	Token 		Keyword 	Token
END 	$80 		FOR 	$81
NEXT 	$82 		DATA 	$83
INPUT# 	$84 		INPUT 	$85
DIM 	$86 		READ 	$87
LET 	$88 		GOTO 	$89
RUN 	$8A 		IF 	$8B
RESTORE 	$8C 		GOSUB 	$8D
RETURN 	$8E 		REM 	$8F
STOP 	$90 		ON 	$91
WAIT 	$92 		LOAD 	$93
SAVE 	$94 		VERIFY 	$95
DEF 	$96 		POKE 	$97
PRINT# 	$98 		PRINT 	$99
CONT 	$9A 		LIST 	$9B
CLR 	$9C 		CMD 	$9D
SYS 	$9E 		OPEN 	$9F
CLOSE 	$A0 		GET 	$A1
NEW 	$A2 		TAB( 	$A3
TO 	$A4 		FN 	$A5
SPC( 	$A6 		THEN 	$A7
NOT 	$A8 		STEP 	$A9
+ 	$AA 		- 	$AB
* 	$AC 		/ 	$AD
^ 	$AE 		AND 	$AF
OR 	$B0 		> 	$B1
= 	$B2 		< 	$B3
SGN 	$B4 		INT 	$B5
ABS 	$B6 		USR 	$B7
FRE 	$B8 		POS 	$B9
SQR 	$BA 		RND 	$BB
LOG 	$BC 		EXP 	$BD
COS 	$BE 		SIN 	$BF
TAN 	$C0 		ATN 	$C1
PEEK 	$C2 		LEN 	$C3
STR$ 	$C4 		VAL 	$C5
ASC 	$C6 		CHR$ 	$C7
LEFT$ 	$C8 		RIGHT$ 	$C9
MID$ 	$CA 		GO 	$CB
PI 	$FF 		? 	$99 (shorthand for PRINT)
5.2 Abbreviated Keywords

The compiler also accepts the C64's abbreviated keyword syntax:
Abbreviation 	Expands to
pR (p + capital R) 	PRINT
gO 	GOTO
gO 	GO
sT 	STEP / STR$ / STOP (context-dependent)
lE 	LEFT$ / LET
cL 	CLOSE / CLR / CLR
rE 	RESTORE / RETURN / REM
rI 	RIGHT$
gO + sU (context) 	GOSUB

    ⚠️ Rule of thumb for AI generation: Just write full uppercase keywords (PRINT, GOTO, GOSUB).
    The compiler handles everything correctly. Abbreviations are a legacy C64 typing convention.

5.3 The ? Shorthand

? is a valid single-character token for PRINT ($99):

? "HELLO"        ← compiles same as PRINT "HELLO"

6. PETSCII Control Characters in Strings

Inside double-quoted strings ("..."), wrap control codes in {...}:
6.1 Supported Control Mnemonics (complete list from source)
Category 	Mnemonics
Screen control 	{clr}, {clear}, {home}, {del}, {inst}, {return}
Special keys 	{stop}, {run/stop}, {esc}, {shift-return}
Cursor 	{cursor right}, {crsr right}, {right}, {cursor left}, {crsr left}, {left}, {cursor down}, {crsr down}, {down}, {cursor up}, {crsr up}, {up}
Charset 	{upper}, {uppercase}, {cset0}, {swuc}, {lower}, {lowercase}, {cset1}, {swlc}
Colors 	{black}, {blk}, {white}, {wht}, {red}, {cyan}, {cyn}, {purple}, {pur}, {green}, {grn}, {blue}, {blu}, {yellow}, {yel}, {orange}, {orng}, {brown}, {brn}, {pink}, {light-red}, {lred}, {gray1}, {darkgrey}, {gry1}, {grey}, {gry2}, {lightgreen}, {lgrn}, {lightblue}, {lblu}, {grey3}, {lightgrey}, {gry3}
Video 	{rvs on}, {rvon}, {rvs off}, {rvof}
Shift 	{shift space}, {shift-space}
Function keys 	{f1}, {f2}, {f3}, {f4}, {f5}, {f6}, {f7}, {f8}
Ctrl keys 	{ctrl-c}, {ctrl-e}, {ctrl-h}, {ctrl-i}, {ctrl-m}, {ctrl-n}, {ctrl-r}, {ctrl-s}, {ctrl-t}, {ctrl-q}
Ctrl+number 	{ctrl-1} through {ctrl-0} (ctrl-9 = rvs on, ctrl-0 = rvs off)
Ctrl+symbol 	{ctrl-/}
C= keys 	{c=1} through {c=8}
CBM graphics 	{cbm-k}, {cbm-i}, {cbm-t}, {cbm-@}, {cbm-g}, {cbm-+}, {cbm-m}, {cbm-pound}, {shift-pound}, {cbm-n}, {cbm-q}, {cbm-d}, {cbm-z}, {cbm-s}, {cbm-p}, {cbm-a}, {cbm-e}, {cbm-r}, {cbm-w}, {cbm-h}, {cbm-j}, {cbm-l}, {cbm-y}, {cbm-u}, {cbm-o}, {shift-@}, {cbm-f}, {cbm-c}, {cbm-x}, {cbm-v}, {cbm-b}, {shift-*}, {shift-a} through {shift-z}, {shift-+}, {cbm--}, {shift--}, {shift-^}, {cbm-^}, {cbm-*}
Misc 	{dish}, {ensh}, {spaces}, {rvs}, {off}
6.2 Numeric / Hex / Binary Codes

You can also use raw PETSCII values directly:

"{clr}"       ← mnemonic (147)
"{147}"       ← decimal
"{$93}"       ← hex
"{0x93}"      ← hex with prefix
"{%10010011}" ← binary
"{0b10010011}"← binary with prefix

6.3 Repeating Control Characters (Compute! Magazine Syntax)

"{12 right}"   ← 12 cursor-right characters
"{5 down}"     ← 5 cursor-down characters

Format: {count space mnemonic} where count is a decimal number.
6.4 Raw Strings (Single Quotes)

Inside single-quoted strings ('...'), NO case conversion is applied:

#uppercase
10 PRINT 'Hello World'   ← "Hello World" with mixed case preserved
20 PRINT "HELLO WORLD"   ← same but uppercase charset mode

6.5 Example

PRINT "{clr}{home}Score: {green}100{white}  Lives: {red}{$21}"

7. Label System (Detailed)
7.1 Declaration

A label is an identifier followed by : at the start of a line:

Start:
    A = 1
    PRINT A

GameLoop:
    GOTO GameLoop

7.2 Usage as Jump Targets

Labels can follow GOTO, GOSUB, ON ... GOTO, ON ... GOSUB, and THEN:

GOTO Start
GOSUB PrintScore
ON X GOTO One, Two, Three
IF A > 10 THEN EndGame

7.3 Label Resolution Rules

    Labels are resolved to the auto-generated line number of the next BASIC statement
    Labels at EOF cause a REM to be appended (so the label has a line number)
    Label names are case-insensitive (start == Start == START)
    Labels cannot be BASIC keywords or start with one — `ReadJson` starts with `READ`, so `THEN ReadJson` is tokenized as `THEN READ JSON`, crashing with "Out of data". Prefer `FetchJson` or `JsonRead` instead.

7.4 THEN + Token Edge Case

The compiler handles THEN followed immediately by a BASIC token (no space):

IF A THENPRINT "yes"    ← THEN + PRINT token (valid)
IF A THEN GOTO Label    ← THEN + GOTO (valid, resolved as jump)
IF A THEN Label         ← THEN + label name (valid, resolved as jump)
IF A THEN X = 1         ← THEN + assignment (valid, NOT a jump)

7.5 GO TO / GO SUB Separation

The compiler handles split GO TO and GO SUB as single tokens:

GO TO 100        ← becomes GOTO token
GO SUB 200       ← becomes GOSUB token

8. Alias Preprocessing (--aliases / -a)
8.1 The Problem

C64 BASIC V2 only uses the first 2 characters of a variable name:

score = 0      ← "SC"
screen = 1024  ← also "SC" — COLLISION!

8.2 The Solution

Prefix variables with @ and enable --aliases:

@score = 0       ← becomes A0 = 0
@screen = 1024   ← becomes A1 = 1024
@name$ = "hero"  ← becomes A2$ = "hero"  (type suffix preserved)
@lives% = 3      ← becomes A3% = 3

8.3 Alias Rules

    @ prefix on any variable name
    Type suffixes $ (string) and % (integer) are preserved
    Works in DEF FN, function parameters, and everywhere variables appear
    Generated roots are drawn from base-36 pool (A0...ZZ), avoiding:
        BASIC reserved: TI, ST, IF, TO, FN, ON, OR, GO, PI
        TSB reserved (if --tsb): AT, DO, HI, UP, NO, RC, EL, TR, DI, PA, IN, TE, DE, KE, ME, ER, OU
    Aliases are case-insensitive (@Score == @score)
    Aliases are NOT substituted inside strings
    Lines starting with ' (raw string) skip alias processing — alias substitution stops at `'`

**Don't use `@` on names that are already 2 characters** — `@sc` just becomes another 2-char root, wasting an alias slot. Use `@` only for names longer than 2 characters.

8.4 Example with Functions

def fn @roll(@sides%) = int(rnd(0) * @sides%) + 1
@playerName$ = "hero"
@score% = 0
@diceSides% = 6
@gravity = 9.8
@score% = fn @roll(@diceSides%)

Compiles to:

def fn A0(A1%) = int(rnd(0) * A1%) + 1
A2$ = "hero"
A3% = 0
A4% = 6
A5 = 9.8
A3% = fn A0(A4%)

9. Crunching (--crunch / Release Build)

When crunching is enabled:
Action 	Detail
Spaces stripped 	All spaces between tokens removed
Lines renumbered 	1, 2, 3, ... sequential
REM removed 	All REM statements eliminated
Trailing : dropped 	No empty statement separators
Empty lines 	Replaced with REM to keep structure

# linestep 10
10 PRINT "HELLO"    ' comment
20 REM this is a comment
30 GOTO 10

Crunches to:

1PRINT"HELLO"
2GOTO1

9.1 How to Trigger Crunching

Via CLI:

bc64 -o game.prg -c main.bas

Via VS64 project config:

{
    "name": "game",
    "toolkit": "basic",
    "sources": ["src/main.bas"],
    "build": "release"
}

    ⚠️ The --crunch flag is the CLI equivalent. In VS64 projects, "build": "release" auto-enables crunching.

10. Charset Control
10.1 Via Preprocessor Directives

#lowercase          ← switch to lowercase charset mapping
#uppercase          ← switch back to uppercase

Aliases: #lower, #cset1, #upper, #cset0
10.2 Via CLI Flag

bc64 -o out.prg --lower main.bas

10.3 At Runtime (from within BASIC)

PRINT "{lower}"     ← POKE 53272,23 equivalent
PRINT "{upper}"     ← POKE 53272,21 equivalent

10.4 What It Does

    Uppercase mode (default): a → A, b → B in PETSCII output
    Lowercase mode: preserves a → a, b → b (uses C64's alternate charset)

This is critical for games that mix uppercase/lowercase characters.
11. Multi-File Projects
11.1 Via CLI

bc64 -o game.prg main.bas utils.bas data.bas

Files are processed in order. Auto-numbering continues across files.
11.2 Via #include

; main.bas
GOTO Start
#include "utils.bas"
#include "data.bas"

Start:
    PRINT "Game started"

Includes are recursive — utils.bas can include other files.
11.3 Include Search Order

    Absolute path
    Relative to the file containing the #include
    Relative to CWD
    Each -I path

12. Complete Code Examples
12.1 Minimal Program

PRINT "HELLO, WORLD!"

Compiles to a valid .prg that prints and stops.
12.2 Full Featured Example (from VS64 repo)

#
# BASIC Example — labels, auto-numbering, PETSCII, includes
#

GOTO Start
#include "include.bas"

Start:
    A$="{clr}HELLO, {green}WORLD{$21}{lightblue}"
    B%=0

Loop:
    POKE 53280,B%
    B%=B%+1
    IF B%>15 THEN B%=0
    PRINT A$
    GOSUB PrintLine
    GOSUB Delay
GOTO Loop

With include.bas:

; Helper functions

PrintLine:
    PRINT("--------------------")
    RETURN

Delay:
    POKE 162,0
    WAIT 162,10
    RETURN

12.3 Traditional Line-Numbered Style

10 REM *** STARFIELD DEMO ***
20 POKE 53280,0
30 POKE 53281,6
40 PRINT "{clr}"
50 FOR I = 1 TO 100
60 X = INT(RND(0)*40)
70 Y = INT(RND(0)*25)
80 PRINT "{down}";
90 NEXT I
100 GOTO 50

12.4 Modern Style with Labels + Aliases

; Requires: --aliases flag
GOTO Main

Main:
    @score% = 0
    @lives% = 3
    @name$ = "PLAYER"
    GOSUB InitScreen

GameLoop:
    GOSUB ReadJoystick
    GOSUB UpdateScore
    IF @lives% <= 0 THEN GameOver
    GOTO GameLoop

GameOver:
    PRINT "{clr}GAME OVER  Score: "; @score%
    END

InitScreen:
    PRINT "{clr}"
    PRINT "Score: 0    Lives: 3"
    RETURN

ReadJoystick:
    ; read joystick port 2
    JOY = PEEK(56320)
    RETURN

UpdateScore:
    @score% = @score% + 1
    RETURN

12.5 Formatted Debug Output Example

#lowercase
#linestep 10

50 rem *** basic program ***
:
:

mainloop:
#lineskip 500
rem *** main loop ***
PRINT "hello world!     ";
gosub flashborder:
goto mainloop
:
:

flashborder:
#lineskip 500
rem *** flash border ***
poke 53280,rnd(0)*255
return
:
:

13. PRG Binary Format

The output .prg file has this exact structure:

Offset  Size    Content
------  ----    -------
$0000   2       Load address (little-endian): $01 $08 (= $0801)
$0002   2       Next line address (little-endian)
$0004   2       Line number (little-endian)
$0006   N       Tokenized BASIC code
$0006+N 1       Null byte ($00) — end of line
...repeat for each line...
$FFFE   2       End of program marker: $00 $00

    The load address $0801 is the standard C64 BASIC program start.

14. Project Configuration (VS64)

When generating code for VS64 projects, the project-config.json looks like:

{
    "name": "mygame",
    "description": "My C64 game",
    "toolkit": "basic",
    "sources": [
        "src/main.bas",
        "src/utils.bas"
    ],
    "build": "debug",
    "args": ["--aliases", "--lower"]
}

Key fields for BASIC:

    "toolkit": "basic" — selects bc64
    "sources" — list of .bas files
    "build": "debug" or "release" — release = crunched
    "args" — array of bc64 flags

15. Common Pitfalls for AI Code Generation
Pitfall 	Why It Happens 	Fix
Line number collision 	Two lines with same explicit number 	Remove explicit numbers or use labels
Undefined label 	GOTO Label but no Label: exists 	Define the label before or after
THEN + variable assignment treated as jump 	Compiler sees label-like identifier after THEN 	Ensure assignment has = sign
PETSCII codes outside strings 	{clr} only works inside "..." 	Wrap in double quotes
Reserved word as label 	PRINT: or FOR: 	Use non-keyword names
Variable name collision without aliases 	score and screen both = SC 	Use --aliases with @score, @screen
Wrong charset 	Lowercase appears as symbols 	Use #lowercase + PRINT "{lower}" at runtime
REM still in release build 	--crunch not enabled 	Use "build": "release" or -c flag
Empty source file 	No BASIC statements at all 	Add at least one statement
16. Quick Reference Card

┌─────────────────────────────────────────────────────┐
│  bc64 Quick Reference                              │
├─────────────────────────────────────────────────────┤
│  COMPILE:  bc64 -o out.prg in.bas           │
│  CRUNCH:   bc64 -o out.prg -c in.bas        │
│  LOWER:    bc64 -o out.prg --lower in.bas   │
│  ALIASES:  bc64 -o out.prg -a in.bas        │
│  TSB:      bc64 -o out.prg -t in.bas        │
│  DECOMPILE: bc64 -o out.bas -u in.prg       │
│  VERBOSE:  bc64 -o out.prg -v in.bas        │
│  MAP:      bc64 -o out.prg -m out.map in.bas│
│                                                    │
│  PREPROCESSOR:                                     │
│    #include "file.bas"   — include file             │
│    #lowercase / #lower / #cset1                     │
│    #uppercase / #upper / #cset0                     │
│    #linestep N           — auto-number increment    │
│    #lineskip N           — jump to next multiple    │
│    # ... / ; ...         — line-start comments      │
│                                                    │
│  STRINGS:                                          │
│    "text"   — normal (charset-mapped)               │
│    'text'   — raw (no charset mapping)              │
│    {clr}    — PETSCII control codes in "..."        │
│    {$21}    — hex PETSCII code                      │
│    {147}    — decimal PETSCII code                  │
│    {5 down} — repeating control (Compute! syntax)   │
│                                                    │
│  LABELS:                                           │
│    Name:               — declare label              │
│    GOTO Name           — jump to label              │
│    GOSUB Name          — call subroutine            │
│    IF cond THEN Name   — conditional jump           │
└─────────────────────────────────────────────────────┘
