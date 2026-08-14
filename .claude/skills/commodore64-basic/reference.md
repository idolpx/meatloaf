Here is a short summary of the commands for the built in Commodore 64 BASIC V2. Note that this is not intended to be a tutorial, but just a quick reference to refresh long unused brain cells.
If you are looking for a BASIC tutorial, take a look at the original C64 manual. It is available as etext at: [http://project64.c64.org](http://project64.c64.org)

> ⚠️ **This document describes stock C64 BASIC V2 as the real hardware runs it.**
> The [bc64](bc_tokenizer.md) compiler **extends** this significantly:
> - **Line numbers are optional** — bc64 auto-numbers lines when omitted (see bc_tokenizer.md §3.1)
> - **Labels** (`Name:`) are fully supported — replaces raw line number references (see bc_tokenizer.md §3.2, §7)
> - **Keywords can be uppercase** — `PRINT` and `print` both work (bc64 is case-insensitive)
> - **@alias variables** (`--aliases` flag) lift the 2-char limit — use `@score` instead of `sc` (see bc_tokenizer.md §8)
>
> **Default to bc64's extended syntax unless explicitly asked for raw BASIC V2.**

In C64 BASIC V2 there are only few types of variables:

-
**Float**: This is the most frequently used type. Float-variables do not carry a type identifier.

 Range +/- \[2.94E-39 ... 1.70E+38\] (+/- \[2<sup>-128</sup> ... 2<sup>127</sup>\])

 Precision: 32 bit mantisse (approx. 9.6 decimal digits) + 8 bit exponent

 
-
**Integer (16 Bit):** This is a rarely used type. Integer variables carry a '%' at the end of their name (eg. X%). Note that all computations are performed with float precision and conversion to Integer is only performed when the value is assigned to a variable. Thus Integer math is SLOWER (!) than Float math.

 
-
**Boolean** Results are expressed using Integers. FALSE is represented by 0 (0x0000) and TRUE by -1 (0xFFFF) or any other non-zero value.

 
-
**Strings:** String variables carry a '$' at the end of their name (eg. X$). Strings can be at most 255 characters long.

Only the first TWO (!) characters of a variable name are significant in stock BASIC V2. Thus 'CO', 'COCOS' and 'COLLOSEUM' (but not 'C' and 'CA') are equivalent names for a single variable. On the other hand the type identifier is used to distinguish variables. Therefore 'CO', 'CO%' and 'CO$' are three different variables.

**⚠️ bc64 lifts this limit.** When using the `--aliases` flag, prefix variables with `@` (`@score`, `@screen`) and they get unique 2-char mappings automatically. See [bc_tokenizer.md](bc_tokenizer.md) §8.

**BASIC keywords are also off-limits as variable names** — `cmd` collides with the `CMD` command, `for` and `next` are keywords, etc. When in doubt, pick a name that does not share its first two characters with any BASIC keyword.

## Program Flow Control

**`FOR...TO...STEP...NEXT`** - The only real loop construct in BASIC

Syntax:
`FOR <Var> = <Start> TO <End> [STEP <Size>]
<Loop Code>
NEXT [<Var>]`

Examples:
```basic
for i=1 to 5: print i;: next -> 1 2 3 4 5
for i=1 to 5 step 2: print i;: next -> 1 3 5
for i=5 to 1 step -2: print i;: next i -> 5 3 1
for i=3 to 1: print i;: next -> 3 (!)
```

**`IF...THEN`** - Conditional Program Execution

Note: There is no such thing as `ELSE` or `ENDIF`.

Syntax:
`IF <Condition> THEN <Statements>` or
`IF <Condition> GOTO <LineNr>` or
`IF <Condition> THEN <LineNr>`

Example:
```basic
100 if a < b then mn = a: goto 120
110 mn = b
120 ....
```

**`GOTO`** or **`GO TO`** - Unconditional Jump

Syntax:
`GOTO <LineNr>` or
`GO TO <LineNr>`

**`GOSUB`** - Unconditional Jump to Subroutine

Note that it is not possible to use formal parameters to a subroutine. Everything must be done using global variables.

Syntax:
`GOSUB <LineNr>`

Example:
```basic
10 print "Main Program"
20 gosub 100
30 print "Back To Main"
40 gosub 100
50 print "Once again Main"
60 end
100 print "This is the Subroutine"
110 return
```

**`RETURN`** - Return from Subroutine

Syntax:
`RETURN`

Example:
See `GOSUB`

**`ON...GOTO`** or **`ON...GOSUB`** - Multiway branch

⚠️ **1-indexed!** This is the most common off-by-one trap in BASIC v2. If X=0, execution falls through to the next line — no jump happens. If X=1, it jumps to the first line number listed. If X exceeds the count of line numbers, execution also falls through.

Syntax:
`ON <IntegerExpr> GOTO <LineNr1>, <LineNr2>` ...
or `ON <IntegerExpr> GOSUB <LineNr1>, <LineNr2>` ...

Example:
`on x goto 100, 200, 300`
This is equivalent to:
```basic
if x = 1 then goto 100
if x = 2 then goto 200
if x = 3 then goto 300
rem x=0 or x>3: falls through to next line
```

**`DEF FN`**\- Define a BASIC Function/Subroutine

Syntax:
`DEF FN <Name>(<Param>) = <Single Line Expression>`

Example:
```basic
def fn si(x) = sin(x)/x
fn si(π/3) -> 0.816993343
```

## Input/Output

**`GET`** - Read One Character from Standard Input without waiting

Syntax:
`GET <VarName>`

Example:
`100 get a$: if a$ = "" then goto 100` -> Wait for any Key

**`INPUT`** - Get Data from Standard Input (usually Keyboard)

Syntax:
`INPUT` `[<Prompt>;] <VarName> [, <VarName> ...]`

Examples:
```basic
input "LOGIN:"; lg$
input "Please Enter A, B and C"; a, b, c
input a
```

**`PRINT`** - Write to Standard Output (usually Screen)

Syntax:
`PRINT <Data>` or
`? <Data>`

Examples:
```basic
print "Hello World"
print "Here", "are", "Tabs" -> Note the ','
print "First Line"; -> Note the ';'
print "Still the same line"
print "Power"; 2*32
```

**`SPC`** - Advance the Cursor by a specific Number of Steps

Syntax:
`SPC(<Cnt>)`

Example:
`SPC(6)`

**`TAB`**\- Advance the Cursor to a Specific Position

Syntax:
`TAB(<Place>)`

Example:
`TAB(6)`

**`POS`** - Current Cursor Position

Syntax:
`POS(<Dummy>)`

## Files

**`LOAD`**\- Load a Program from Disk or Tape

Syntax:
`LOAD <FileName> [, <Device> [, <SecondDev>]]`

Examples:
```basic
load "SuperGame", 8, 1 -> Absolute Load from Disk #8
load "*", 9 -> Load the first program from Disk #9
load "", 1 -> Load the first program from Tape (#1)
```

**`SAVE`** - Save a Program to Disk or Tape

Syntax:
`SAVE <FileName> [, <Device> [, <SecondDev>]]`

Example:
`SAVE "SuperGame", 8 -> Save to Disk #8`
To overwrite an existing file on a disk, prefix the filename with '`@`'

Example:
`SAVE "@SuperGame",8 -> Save to Disk #8, overwriting an old file`

**`VERIFY`** - Check if the Program in Memory and a Program on Disk or Tape are equal. Do not modify anything.

Syntax:
`VERIFY <FileName> [, <Device> [, <SecondDev>]]`

Examples:
```basic
verify "SuperGame", 8 -> Check SuperGame from Disk #8
verify "*", 9 -> Check the first program from Disk #9
verify "", 1 -> Check the first program from Tape #1
```

**`OPEN`**\- Open a File

Syntax:
`OPEN <FileID>, <Device> [, <SecondDev> [, <FNameMode>]]`
The `<SecondDev>` is an optional integer in the range 0-15 with the following meaning`:` 0..Used for `LOAD,` 1..Used for `SAVE,` 2-14..Freely usable for User File Access`,` 15..Command/Error Channel.
`<FNameMode>` uses the format: "`<FileName> [,<FileType> [,<AccessMode>]]"` where `<FileType>` is one of `P` (Program), `S` (Sequential), `L` (Relative) or `U` (User) and `<AccessMode>` is one of `R` (Read), `W` (Write), `A` (Append) or the number of Bytes/Record for Relative Files.

Examples:
```basic
open 1, 4 -> Open a Output File to the Printer #4
open 1, 8, 2, "My File,P,R" -> Open a Program for Reading
open 1, 8, 2, "My File,S,W" -> Open a Sequential File for Writing
open 1, 8, 2, "My File,L,"+chr$(40) -> Open a Relative File with 40 Bytes/Record
open 1, 8, 15 -> Open the Disk Command/Error Channel
```

**`CLOSE`** - Close a File

Syntax:
`CLOSE <FileID>`

Example:
`CLOSE 1`

**`GET#`** - Read One Character from a File

Syntax:
`GET# <FileID>, <VarName>`

Example:
`get#1, a$`

Note that there is no space between '`GET`' and '`#`'.

**`INPUT#`** - Get Data from a File

Syntax:
`INPUT# <FileID>, <VarName> [, <VarName>...]`

Example:
`input#1, en$, er$, tr$, sc$`

Note that there is no space between '`INPUT`' and '`#`'.

**`PRINT#`**

Syntax:
`PRINT# <FileID>, <Data>`

Example:
`print#1, "Power64"`

Note that there is no space between '`PRINT`' and '`#`'. Note also that `?#` is not `PRINT#` also they look the same in a listing.

**`CMD`** - Redirect Standard Output (Input is not affected) and writes a Message to it

Syntax:
`CMD <FileID> [, <Message>]`

Example:
`open 1, 4 : rem Open a File#1 on Printer#4
cmd 1 : rem Make it the standard Output
print "Whatever Output you want"
print "More Output"
print#1 : rem Undo CMD 1
close 1`

**`ST`** \- Device Status (Built-In Variable)

ST = 0 .. Device Ok
Bit 6: 1 .. End of File
Bit 7: 1 .. Device Not Present

**`READ`** - Read Static Data from DATA Statements in the Program

Syntax:
`READ <Var> [, <Var>...]`

Example:
`10 restore
20 read x$
30 print x$;
40 s = 0
50 for i=1 to 3
60 read x
70 s = s + x
80 next i
90 print s
100 data "Power", 12, 34, 18`

**`RESTORE`** - Set Pointer to Next DATA element to the first DATA statement in the program.

Syntax:
`RESTORE`

Example:
See `READ`

**`DATA`**\- Store Static Data

Syntax:
`DATA <Data> [, <Data>...]`

Example:
See `READ`

## Math Functions

**`DIM`** - Array Declaration

Syntax:
`DIM <Name>(<Size> [, <Size>...])`

Examples:
`DIM A(7)` -> An array of 8(!) elements indexed \[0..7\]
`DIM B$(4,5)` -> An array of 30(!) strings
Usage of Elements: `A(3) = 17 : B$(2,3) = "Power64"`

**`+, -, *, /, ^`** - Arithmetic Operators

Example:
`9 + 5 * (15 - 1) / 7 + 2^4 -> 35`

**`<, <=, =, <>, >=, >`** - Comparison Operators

Examples:
`3 <> 6 -> -1 (TRUE)`
`3 > 4 -> 0 (FALSE)`

**`SIN`** - Sine (Argument in Radians)

Syntax:
`SIN(<Value>)`

Example:
`SIN(π/3) -> 0.866025404`

**`COS`** - Cosine (Argument in Radians)

Syntax:
`COS(<Value>)`

Example:
`COS(π/3) -> 0.5`

**`TAN`** - Tangent (Argument in Radians)

Syntax:
`TAN(<Value>)`

Example:
`TAN(π/3) -> 1.73205081`

**`ATN`** - Arcus Tangent (Result in \[-π/2 .. π/2\])

Syntax:
`ATN(<Value>)`

Example:
`ATN(1) -> 0.785398163 ( = π/4)`

**`EXP`** - Exponent (e<sup>x</sup> where e = 2.71828183...)

Syntax:
`EXP(<Value>)`

Example:
`EXP(6.25) -> 518.012825`

**`LOG`** - Natural Logarithm

Syntax:
`LOG(<Value>)`

Example:
`LOG(6.25) -> 1.83258146`

**`SQR`** - Square Root

Syntax:
`SQR(<Value>)`

Example:
`SQR(6.25) -> 2.5`

**`ABS`** - Absolute Value

Syntax:
`ABS(<Value>)`

Examples:
```basic
abs(-6.25) -> 6.25
abs(0) -> 0
abs(6.25) -> 6.25
```

**`SGN`** - Sign

Syntax:
`SGN(<Value>)`

Examples:
```basic
sgn(-6.25) -> -1
sgn(0) -> 0
sgn(6.25) -> 1
```

**`INT`** - Integer (Truncate to greatest integer less or equal to Argument.)

Syntax:
`INT(<Value>)`

Examples:
```basic
int(-6.25) -> -7 (!)
int(-5) -> -5
int(0) -> 0
int(5) -> 5
int(6.25) -> 6
```

**`RND`** - Random Number in \[0.0 .. 1.0\]

Syntax:
`RND(<Seed>)`
If `(<Seed> < 0)` the Random number generator is initialized

Examples:
```basic
rnd(-625) -> 3.85114436E-06
rnd(0) -> 0.464844882
rnd(0) -> 0.0156260729
```

## Logic & Binary Operators

Recall the encoding of Boolean Values:
`FALSE <--> 0 (0x0000) and TRUE <--> -1 (0xFFFF)` or any non-zero value

**`AND`**\- Logical & Binary AND

Syntax:
`<Expr> AND <Expr>`

Examples:
```basic
a>5 and x<=y
12 and 10 -> 8
(%1100 and %1010 = %1000)
```

**`OR`** - Logical & Binary OR

Syntax:
`<Expr> OR <Expr>`

Examples:
```basic
a>5 or x<=y
12 or 10 -> 14 (%1100 or %1010 = %1110)
```

**`NOT`**\- Logical & Binary NOT

Syntax:
`NOT <Expr>`

Examples:
```basic
not a>5
not 2 -> -3
(not $0002 = $fffd)
```

## Character & String Processing

**`+`** - Concatenate Strings

Example:
`"Pow" + "er64" -> "Power64"`

**`<, <=, =, <>, >=, >`** - Comparison Operators

Examples:
`"C64" < "Power64" -> -1 (TRUE)
"Alpha" > "Omega" -> 0 (FALSE)`

**`LEN`** - Stringlength

Syntax:
`LEN(<String>)`

Example:
`LEN("Power64") -> 7`

**`LEFT$`** - Left part of a string

Syntax:
`LEFT$(<String>, <Len>)`

Example:
`left$("Power64", 5) -> "Power"`

**`RIGHT$`** - Right part of a string

Syntax:
`RIGHT$(<String>, <Len>)`

Example:
`right$("Power64", 5) -> "wer64"`

**`MID$`** - Middle part of a string

Syntax:
`MID$(<String>, <Start>, <Len>)`

Example:
`mid$("Power64 for Macintosh", 13, 3) -> "Mac"`
`/* -- 123456789012345678901 -- */`

**`STR$`** - Convert a Number into a String

Syntax:
`STR$(<Value>)`

Examples:
```basic
str$(6.25) -> " 6.25"
str$(-6.25) -> "-6.25"
```

**`VAL`** - Convert a String to a Number

Syntax:
`VAL(<String>)`

Examples:
```basic
val("6.25") -> 6.25
val("6xx25") -> 6
val("x6x25") -> 0
```

**`ASC`**\- ASCII code of the first character of a string

Syntax:
`ASC(<String>)`

Examples:
```basic
asc("P") -> 80
asc("Power64") -> 80
```

**`CHR$`** - Character with a specific ASCII code

Syntax:
`CHR$(<Value>)`

Example:
`CHR$(80) -> "P"`

## Memory Access

**`PEEK`** - Read Byte from Memory

Syntax:
`PEEK(<Addr>)`

Example:
`peek(53280)` -> Current Frame Color

**`POKE`** - Write Byte to Memory

Syntax:
`POKE <Addr>, <Value>`

Example:
`poke 53280, 7` \-> Yellow Frame

**`WAIT`** - Wait until a Byte in Memory has a specific value

Syntax:
`WAIT <Addr>, <Mask> [, <Invert>]`
`WAIT` will halt the program until `((PEEK(<Addr>) EXOR <Invert>) AND <Mask>) != 0`
If <`Invert`\> is not specified it is assumed to be 0.

Example:
`wait 198, 255` -> Wait for a key in the key buffer.

## Interface to Assembler Programs

**`SYS`** - System - Call a Assembler Program

Syntax:
`SYS <Addr> [, <Param> ...]`

The number of parameters depends on the actual program called.

**`USR`**\- User Command

Syntax:
`USR`(<Param>)

Similar to `SYS` but the <`Addr`\> is fixed to `$0310` and the first and only <`Param`\> is already evaluated and stored in FloatAccu1 (`FAC1`) when the Assembler Program is called. Less flexible than SYS and thus rarely used.

## Program Execution

**`run`** - Start the BASIC Program

Syntax:
`RUN [<Line>]`

If no <`Line`\> is given, the program is started on its first line.

Example:
`run`

**`STOP`**\- Stops program execution

Syntax:
`STOP`

`STOP` is similar to `END`, but prints the message `BREAK IN <Line>` when executed.

**`END`** - End program execution

Syntax:
`END`

**`CONT`** - Continue program execution

Syntax:
`CONT`

When program execution has interrupted by `STOP`, `END` or the Run/Stop key, the command `CONT` can be used to resume execution.

## Miscellaneous

**`REM`** - Remark

Syntax:
`REM <Text>`

Example:
`rem This line contains a comment`

**`LIST`** - Display the listing of the current BASIC program

Syntax:
`LIST [<Line> | <From>- | -<To> | <From>-<To>]`
Without argument, the entire program is listed.

Examples:
```basic
list
list -40
list 100-200
```

**`NEW`** - Delete the current program and all variables from memory

Syntax:
`NEW`

If the `NEW` command was accidentally issued, the deleted program can be recovered by using the `NEW` Magician described in Section 7.2.

**`CLR`** - Delete all variables

Syntax:
`CLR`

**`FRE`** - Free Memory

Syntax:
`FRE(<Dummy>)`

Example:
`FRE(0)` -> -26627 (immediately after Power-on)

Returns the number of Bytes free for BASIC programs as a signed 16 Bit integer. If the free memory exceeds 32KByte then a negative number (the actual number of free Bytes - 65536) will be returned. Thus -26627 should be read as 65536-26627 = 38909.

**`π`** - Pi = 3.14159265

**`TI`** - Timer Ticks since Power-On (1 Tick = 1/60 Second)

**`TI$`** - Timer since Power-On in Hour/Minute/Second Format

`TI$` (but not TI) can be assigned a value!
The accuracy of the Timer is very poor (>1% drift)
