---
title: Command Reference
description: Auto-generated encyclopedia of all 63 CK-DOS 5.0 commands, full-screen DOSUI applications, and memory utilities.
---
# CK-DOS 5.0 Command Reference

> [!NOTE]
> Auto-generated deterministically by `ckdist gen-docs` from `os/ck-dos-5/src/CMD/HELP/topics.json` (`fdc6286971d8`, 63 topics). Run `ckdist gen-docs --check` in CI to verify synchronization.

Every CK-DOS 5.0 internal command and external utility supports `COMMAND /?` for concise inline usage and `HELP [topic]` for the full-screen **CK Help** (`HELP.COM`) hyperlinked browser.

## Quick Index

| Command | Summary |
|---|---|
| [`ATTRIB`](#attrib) | Displays or changes read-only and archive file attributes. |
| [`BACKUP`](#backup) | Backs up files from one disk onto another. |
| [`BREAK`](#break) | Controls extended Ctrl+C checking. |
| [`CALL`](#call) | Calls a batch or executable file and returns. |
| [`CD`](#cd) (`CHDIR`) | Displays or changes the current directory. |
| [`CHCP`](#chcp) | Displays or changes the active code page. |
| [`CHKDSK`](#chkdsk) | Checks a disk and reports its file and memory status. |
| [`CLS`](#cls) | Clears the screen. |
| [`COMMAND`](#command) | Starts a secondary DOS command processor. |
| [`COMP`](#comp) | Compares the contents of two files. |
| [`COPY`](#copy) | Copies or combines files. |
| [`CTTY`](#ctty) | Changes the controlling terminal device. |
| [`DATE`](#date) | Displays or sets the system date. |
| [`DEBUG`](#debug) | Loads a program for testing with the DOS debugger. |
| [`DEL`](#del) (`ERASE`) | Deletes files; /P prompts before each deletion. |
| [`DIR`](#dir) | Displays a directory; /P pauses and /W uses wide format. |
| [`DISKCOMP`](#diskcomp) | Compares the contents of two diskettes. |
| [`DISKCOPY`](#diskcopy) | Copies the whole contents of one diskette to another. |
| [`DOSKEY`](#doskey) | Edits command lines, recalls commands, and creates macros. |
| [`ECHO`](#echo) | Displays text or controls command echoing. |
| [`EDIT`](#edit) | Starts CK Edit (EDIT.COM) to create and edit ASCII text files. |
| [`EDLIN`](#edlin) | Edits a text file one numbered line at a time. |
| [`EXE2BIN`](#exe2bin) | Converts an executable file to binary format. |
| [`EXIT`](#exit) | Exits a secondary command processor. |
| [`EXPAND`](#expand) | Decompresses one or more compressed distribution files. |
| [`FC`](#fc) | Compares two files and lists the differences. |
| [`FIND`](#find) | Searches files for a text string. |
| [`FOR`](#for) | Runs a command for each item in a set. |
| [`FORMAT`](#format) | Formats a disk for use with DOS. |
| [`GOTO`](#goto) | Branches to a label in the current batch file. |
| [`HELP`](#help) | Starts CK Help (HELP.COM) to browse CK-DOS 5 command topics. |
| [`IF`](#if) | Conditionally runs a batch command. |
| [`JOIN`](#join) | Joins a disk drive to a directory on another drive. |
| [`LABEL`](#label) | Creates, changes, or deletes a disk volume label. |
| [`LH`](#lh) (`LOADHIGH`) | Loads a program into upper memory blocks (UMBs) when available. |
| [`LOADFIX`](#loadfix) | Ensures a program is loaded above the first 64 KB of conventional memory. |
| [`MD`](#md) (`MKDIR`) | Creates a directory. |
| [`MEM`](#mem) | Displays used and free memory. |
| [`MORE`](#more) | Displays output one screen at a time. |
| [`PATH`](#path) | Displays or sets the executable search path. |
| [`PAUSE`](#pause) | Suspends batch processing until a key is pressed. |
| [`PROMPT`](#prompt) | Changes the command prompt. |
| [`QBASIC`](#qbasic) | Starts CK Basic (QBASIC.EXE) to edit and run BASIC programs. |
| [`RD`](#rd) (`RMDIR`) | Removes an empty directory. |
| [`RECOVER`](#recover) | Recovers a file or a disk with damaged sectors. |
| [`REM`](#rem) | Adds a comment to a batch file. |
| [`REN`](#ren) (`RENAME`) | Renames a file or directory. |
| [`REPLACE`](#replace) | Replaces or adds files from one directory to another. |
| [`RESTORE`](#restore) | Restores files that were saved by BACKUP. |
| [`SET`](#set) | Displays, sets, or removes environment variables. |
| [`SETVER`](#setver) | Displays or modifies the DOS version number reported to specific programs. |
| [`SHIFT`](#shift) | Shifts batch parameters %1 through %9. |
| [`SORT`](#sort) | Sorts text and writes the result. |
| [`SUBST`](#subst) | Substitutes a drive letter for a path. |
| [`SYS`](#sys) | Copies the DOS system files to a disk. |
| [`TIME`](#time) | Displays or sets the system time. |
| [`TREE`](#tree) | Displays the directory structure of a drive. |
| [`TRUENAME`](#truename) | Displays the fully resolved path. |
| [`TYPE`](#type) | Displays a text file. |
| [`VER`](#ver) | Displays the CK-DOS 5 operating-system version. |
| [`VERIFY`](#verify) | Controls write verification. |
| [`VOL`](#vol) | Displays a disk volume label. |
| [`XCOPY`](#xcopy) | Copies files and directory trees. |

## Detailed Command Entries

### ATTRIB

**File attributes** — Displays or changes read-only and archive file attributes.

```text
ATTRIB [+R|-R] [+A|-A] [drive:][path]file [/S]
```

ATTRIB reports the read-only and archive attributes of every matching file. A plus or minus operand sets or clears an attribute before the report is written.

This build parses only the R and A attribute letters; hidden and system letters are not accepted. /S extends the operation to matching files in every directory below the named path.

**See also:** [`DEL`](#del) · [`DIR`](#dir) · [`XCOPY`](#xcopy)

### BACKUP

**Back up files** — Backs up files from one disk onto another.

```text
BACKUP source drive: [/S] [/M] [/A] [/F[:size]] [/D:date] [/T:time]
       [/L[:logfile]]
```

BACKUP needs a source file specification and a target drive. It copies file by file rather than making a disk image, and it never backs up the two hidden system files.

/S includes subdirectories, /M limits the run to files whose archive attribute is set, and /A adds to the files already on the target instead of replacing them.

/D and /T restrict the run to files changed on or after the given date and time. Diskette targets are formatted by default; /F chooses a capacity such as 360, 720, 1200, or 1440 kilobytes. /L writes a log file.

**See also:** [`FORMAT`](#format) · [`RESTORE`](#restore) · [`XCOPY`](#xcopy)

### BREAK

**Extended Ctrl+C checking** — Controls extended Ctrl+C checking.

```text
BREAK [ON|OFF]
```

With no operand, BREAK reports the current state. ON and OFF change the shell's extended Ctrl+C checking setting.

**See also:** [`CTTY`](#ctty) · [`VERIFY`](#verify)

### CALL

**Call a command or batch file** — Calls a batch or executable file and returns.

```text
CALL [drive:][path]file [arguments]
```

CALL searches the current directory and PATH for a batch file, passes its arguments, and resumes the caller when the batch finishes.

If the target is not a batch file, CALL executes the supplied command line and returns after it completes.

**See also:** [`GOTO`](#goto) · [`IF`](#if) · [`SHIFT`](#shift)

### CD

**Current directory** — Displays or changes the current directory.

- **Spellings:** `CD`, `CHDIR`

```text
CD [drive:][path]
```

Without a path, CD displays the current drive and directory. A drive-qualified directory updates that drive; changing drives remains a separate drive-letter command.

**See also:** [`DIR`](#dir) · [`MD`](#md) · [`RD`](#rd) · [`TRUENAME`](#truename)

### CHCP

**Active code page** — Displays or changes the active code page.

```text
CHCP [nnn]
```

Without a number, CHCP reports the active code page. Supplying a prepared three-digit code-page identifier switches the active console and device code page when NLSFUNC is loaded.

**See also:** [`CTTY`](#ctty) · [`VER`](#ver)

### CHKDSK

**Check a disk** — Checks a disk and reports its file and memory status.

```text
CHKDSK [drive:][[path]file] [/F] [/V]
```

CHKDSK reports allocation, directory, and free-space totals for the selected drive together with the memory available to the session. One optional operand names a drive or a file specification to check.

/F writes corrections for the allocation faults it finds; without it the report changes nothing. /V names every file as the scan reaches it.

**See also:** [`FORMAT`](#format) · [`MEM`](#mem) · [`RECOVER`](#recover)

### CLS

**Clear the screen** — Clears the screen.

```text
CLS
```

CLS clears the current text console using its configured blank cell and returns the cursor to the home position.

**See also:** [`ECHO`](#echo) · [`PROMPT`](#prompt)

### COMMAND

**Command processor** — Starts a secondary DOS command processor.

```text
COMMAND [drive:][path] [/P] [/F] [/D] [/E:nnnnn] [/MSG] [/C string]
```

COMMAND starts a second copy of the command processor. The optional path tells that copy where to reload itself from. /P makes it permanent, so EXIT will not end it.

/C runs the rest of the line as a single command and then exits, so it has to come last; it also cancels /P. /E sets the environment size in bytes and accepts 160 through 32768.

/D suppresses the startup date and time prompt, /F answers every critical-error request with failure, and /MSG keeps the extended message block resident and is accepted only together with /P.

**See also:** [`EXIT`](#exit) · [`PATH`](#path) · [`SET`](#set)

### COMP

**Compare file contents** — Compares the contents of two files.

```text
COMP [drive:][path]file1 [drive:][path]file2
```

COMP compares two files byte for byte and prints the offset and both values at each difference. It abandons a pair after ten mismatches.

Either operand may be left out; COMP then prompts for the missing name. Files of unequal size are reported without being compared, and after each pair it asks whether to compare more files.

**See also:** [`DISKCOMP`](#diskcomp) · [`FC`](#fc)

### COPY

**Copy or combine files** — Copies or combines files.

```text
COPY source [+ source ...] [destination] [/A] [/B] [/V]
```

COPY accepts one or more sources joined by plus signs. The final operand may name the destination; otherwise the current directory and source name are used.

/A selects text handling, /B selects binary handling, and /V requests write verification.

**See also:** [`DEL`](#del) · [`DIR`](#dir) · [`REN`](#ren) · [`TYPE`](#type)

### CTTY

**Controlling terminal** — Changes the controlling terminal device.

```text
CTTY device
```

CTTY redirects standard console input and output for the command processor to a character device such as AUX, COM1, or CON. CTTY CON restores the local keyboard and screen.

**See also:** [`CHCP`](#chcp) · [`ECHO`](#echo)

### DATE

**System date** — Displays or sets the system date.

```text
DATE [mm-dd-yy]
```

With no operand, DATE displays the current date and prompts for a replacement. Enter accepts the current value.

Accepted dates use month, day, and year fields and must fall from 1980 through 2099.

**See also:** [`TIME`](#time) · [`VER`](#ver)

### DEBUG

**Program debugger** — Loads a program for testing with the DOS debugger.

```text
DEBUG [[drive:][path]file [arguments]]
```

DEBUG loads the named file together with any following arguments and then presents its own prompt. Without a file it starts with an empty workspace.

Single-letter commands assemble, compare, dump, enter, fill, go, add hexadecimal values, read ports, load, move, name, write ports, proceed, quit, show registers, search, trace, unassemble, and write.

The X commands manage expanded memory: XA allocates pages, XD releases a handle, XM maps a page, and XS reports handle status.

**See also:** [`EXE2BIN`](#exe2bin) · [`MEM`](#mem)

### DEL

**Delete files** — Deletes files; /P prompts before each deletion.

- **Spellings:** `DEL`, `ERASE`

```text
DEL [drive:][path]file [/P]
```

DEL accepts DOS wildcards and removes matching files. Directories are not removed.

/P asks for confirmation before each matching file is deleted.

**See also:** [`COPY`](#copy) · [`DIR`](#dir) · [`REN`](#ren)

### DIR

**Directory listing** — Displays a directory; /P pauses and /W uses wide format.

```text
DIR [drive:][path][file] [/P] [/W]
```

DIR lists matching files and directories, including DOS attributes, sizes, and free-space totals for the selected drive.

/P pauses after each screen and /W uses a compact wide listing.

**See also:** [`CD`](#cd) · [`MD`](#md) · [`TYPE`](#type)

### DISKCOMP

**Compare diskettes** — Compares the contents of two diskettes.

```text
DISKCOMP [drive1: [drive2:]] [/1] [/8]
```

DISKCOMP compares two diskettes track by track and names each track and side that differs. Given fewer than two drives it prompts for each diskette in turn.

/1 compares the first side only. /8 compares only the first eight sectors of each track.

**See also:** [`COMP`](#comp) · [`DISKCOPY`](#diskcopy) · [`FORMAT`](#format)

### DISKCOPY

**Copy diskettes** — Copies the whole contents of one diskette to another.

```text
DISKCOPY [drive1: [drive2:]] [/1]
```

DISKCOPY duplicates an entire diskette and formats the target when its layout does not already match the source. Given fewer than two drives it prompts for the diskettes to swap.

/1 copies the first side only. Whatever the target diskette held before is replaced.

**See also:** [`DISKCOMP`](#diskcomp) · [`FORMAT`](#format) · [`XCOPY`](#xcopy)

### DOSKEY

**Command history and macros** — Edits command lines, recalls commands, and creates macros.

```text
DOSKEY [/REINSTALL] [/LISTSIZE=size] [/MACROS[:ALL|exename]]
       [/HISTORY] [/INSERT|/OVERSTRIKE] [/EXENAME=exename]
       [/MACROFILE=filename] [macroname=[text]]
```

Running DOSKEY enables the resident command-line editor. /REINSTALL clears history; /LISTSIZE, /INSERT, and /OVERSTRIKE adjust editor behavior.

/HISTORY displays history. /MACROS and /EXENAME select macro namespaces; /MACROFILE loads name=value definitions. Macro text supports arguments and $T command separators.

**See also:** [`HELP`](#help) · [`PROMPT`](#prompt)

### ECHO

**Display text and command echo** — Displays text or controls command echoing.

```text
ECHO [ON|OFF|text]
```

ECHO ON and ECHO OFF control batch command display. With no operand, ECHO reports that state; any other operand is printed as text.

**See also:** [`PROMPT`](#prompt) · [`TYPE`](#type)

### EDIT

**CK Edit full-screen text editor (EDIT.COM)** — Starts CK Edit (EDIT.COM) to create and edit ASCII text files.

```text
EDIT [[drive:][path]file] [/B] [/G] [/H] [/NOHI]
```

CK Edit (shipped as EDIT.COM for DOS compatibility) opens an ASCII text file in a full-screen editor with pull-down menus, dialogs, mouse support, clipboard commands, search, and replace.

/B selects monochrome attributes, /G enables fast CGA updates, /H uses 43/50 EGA/VGA rows, and /NOHI disables bright background colors. F1 opens built-in editor help.

Copyright (c) 2026 C. Klukas. Developed clean-room in 16-bit x86 assembly under the MIT License, with zero Microsoft MS-DOS 5 EDIT or QBasic source code or help data.

**See also:** [`EDLIN`](#edlin) · [`HELP`](#help) · [`QBASIC`](#qbasic) · [`TYPE`](#type)

### EDLIN

**Line editor** — Edits a text file one numbered line at a time.

```text
EDLIN [drive:][path]file [/B]
```

EDLIN requires a file name. An existing file is loaded for editing and a new name starts an empty one. Its own prompt accepts commands that act on line numbers.

/B reads the whole file instead of stopping at the first end-of-file character, which matters for files that carry one in the middle.

**See also:** [`EDIT`](#edit) · [`TYPE`](#type)

### EXE2BIN

**Convert EXE to binary** — Converts an executable file to binary format.

```text
EXE2BIN [drive:][path]input [[drive:][path]output]
```

EXE2BIN turns a relocatable executable into a plain memory image. The input name is required; without an output name the input name is reused with a BIN extension.

A file that still needs segment fix-ups cannot be converted until a base segment is supplied, and EXE2BIN prompts for that value in hexadecimal. Files it cannot convert are reported and left alone.

**See also:** [`DEBUG`](#debug) · [`MEM`](#mem)

### EXIT

**Exit the command prompt** — Exits a secondary command processor.

```text
EXIT
```

EXIT ends a secondary COMMAND.COM shell and returns to the parent program or command processor. It has no effect in a permanent top-level shell started with /P.

**See also:** [`CALL`](#call) · [`HELP`](#help)

### EXPAND

**Expand compressed files** — Decompresses one or more compressed distribution files.

```text
EXPAND [drive:][path]source [[drive:][path]destination]
```

EXPAND unpacks distribution files stored in compressed format and writes the expanded output to the named destination file or directory.

Already-uncompressed input files are copied unchanged so setup scripts can process mixed media uniformly.

**See also:** [`COPY`](#copy) · [`REPLACE`](#replace) · [`XCOPY`](#xcopy)

### FC

**Compare two files** — Compares two files and lists the differences.

```text
FC [/A] [/C] [/L] [/LB:n] [/N] [/T] [/W] [/nnnn] file1 file2
FC /B file1 file2
```

FC needs exactly two file operands. /B forces a byte comparison; otherwise files whose extension is EXE, OBJ, LIB, COM, BIN, or SYS are compared as bytes and all others as lines of text.

/A prints only the first and last line of each differing block, /C ignores letter case, /W ignores runs of white space, /N numbers the lines, and /T keeps tab characters instead of expanding them.

/nnnn sets how many consecutive equal lines end a difference, and /LB:n sets the internal line buffer. A byte comparison rejects /L and /N.

**See also:** [`COMP`](#comp) · [`FIND`](#find)

### FIND

**Search files for text** — Searches files for a text string.

```text
FIND [/V] [/C] [/N] "string" [[drive:][path]file ...]
```

FIND prints every line of the named files that contains the quoted string, and repeats the file name as a heading. With no file operand it reads the standard input, so it can end a pipe.

The comparison keeps letter case. /V prints the lines that do not contain the string, /C prints only the count of matching lines, and /N puts the line number in front of each line.

**See also:** [`MORE`](#more) · [`SORT`](#sort) · [`TYPE`](#type)

### FOR

**Repeat a command** — Runs a command for each item in a set.

```text
FOR %variable IN (set) DO command
```

FOR substitutes one percent variable into the command for each item in the parenthesized set.

Items containing DOS wildcards expand to matching directory entries before execution.

**See also:** [`IF`](#if) · [`SET`](#set)

### FORMAT

**Format a disk** — Formats a disk for use with DOS.

```text
FORMAT drive: [/V[:label]] [/S] [/B] [/1] [/4] [/8] [/T:tracks]
       [/N:sectors] [/F:size] [/SELECT] [/BACKUP] [/AUTOTEST]
```

FORMAT prepares the disk in the named drive and takes exactly one drive operand. /V records a volume label, /S copies the system files afterwards, and /B reserves room for them without copying.

Layout comes from /T for tracks, from 1 through 1024, /N for sectors per track, from 1 through 64, and /F for a named capacity such as 360, 720, 1200, or 1440 kilobytes. /1 formats one side; /4 and /8 select reduced layouts.

/SELECT, /BACKUP, and /AUTOTEST are the unattended entry points used by installation programs. This build ships those twelve switches and no quick or unconditional format switch.

**See also:** [`CHKDSK`](#chkdsk) · [`DISKCOPY`](#diskcopy) · [`LABEL`](#label) · [`SYS`](#sys)

### GOTO

**Branch within a batch file** — Branches to a label in the current batch file.

```text
GOTO label
```

GOTO searches labels in the current batch file and continues after the matching label. It is rejected outside batch execution.

**See also:** [`CALL`](#call) · [`IF`](#if)

### HELP

**CK Help command reference (HELP.COM)** — Starts CK Help (HELP.COM) to browse CK-DOS 5 command topics.

```text
HELP [topic]
```

CK Help (shipped as HELP.COM) opens the full-screen CK-DOS 5 command reference. Supplying a command name opens that topic directly; typing in Search filters topics interactively.

Alternate command spellings resolve to one entry (for example HELP CHDIR and HELP CD). Esc clears an active search filter or exits back to DOS.

Copyright (c) 2026 C. Klukas. Independently authored clean-room software and command reference under the MIT License; zero Microsoft DOSHELP.HLP code or text is used.

**See also:** [`EDIT`](#edit) · [`QBASIC`](#qbasic) · [`VER`](#ver)

### IF

**Conditional command execution** — Conditionally runs a batch command.

```text
IF [NOT] condition command
```

IF supports ERRORLEVEL n, EXIST path, and string1==string2 conditions. NOT may invert a condition.

The trailing command runs only when the resulting condition is true.

**See also:** [`FOR`](#for) · [`GOTO`](#goto)

### JOIN

**Join a drive to a path** — Joins a disk drive to a directory on another drive.

```text
JOIN [drive1: [drive2:]path] [/D]
```

With no operands JOIN lists the joins in effect. Given a drive and a path, the whole of that drive appears below the named directory, which has to exist and be empty.

/D cancels the join for the named drive. A drive cannot be joined to itself, and a joined drive is no longer reached through its own letter.

**See also:** [`SUBST`](#subst) · [`TREE`](#tree)

### LABEL

**Volume label** — Creates, changes, or deletes a disk volume label.

```text
LABEL [drive:][label]
```

LABEL shows the current label and serial number of the selected drive and then writes the new one. A label holds up to eleven characters and follows the file-name character rules.

With no label on the command line LABEL prompts for one; an empty answer offers to delete the existing label. Substituted, assigned, and network drives are refused.

**See also:** [`FORMAT`](#format) · [`VOL`](#vol)

### LH

**Load a program into upper memory** — Loads a program into upper memory blocks (UMBs) when available.

- **Spellings:** `LH`, `LOADHIGH`

```text
LOADHIGH [drive:][path]filename [parameters]
LH [drive:][path]filename [parameters]
```

LH (or LOADHIGH) links upper memory blocks into the DOS allocation chain and attempts to load a terminate-and-stay-resident utility or program above 640 KB.

If upper memory is unavailable or no UMB is large enough, DOS automatically loads the program into conventional memory instead.

**See also:** [`LOADFIX`](#loadfix) · [`MEM`](#mem)

### LOADFIX

**Load above the first 64 KB** — Ensures a program is loaded above the first 64 KB of conventional memory.

```text
LOADFIX [drive:][path]filename [parameters]
```

Some older packed executables fail with a packed-file error when DOS frees low conventional memory and loads them below segment 1000h (64 KB).

LOADFIX reserves any free memory below 64 KB before launching the target program and releases that reservation when the program terminates.

**See also:** [`LH`](#lh) · [`MEM`](#mem) · [`SETVER`](#setver)

### MD

**Create a directory** — Creates a directory.

- **Spellings:** `MD`, `MKDIR`

```text
MD [drive:]path
```

MD creates one directory at the resolved DOS path. Its parent directory must already exist.

**See also:** [`CD`](#cd) · [`DIR`](#dir) · [`RD`](#rd)

### MEM

**Memory report** — Displays used and free memory.

```text
MEM [/PROGRAM|/DEBUG]
```

MEM reports total and available conventional memory, the largest program that will still fit, and the expanded and extended memory the session can see. It takes no file operands.

/PROGRAM lists the programs held in memory with their addresses, names, and sizes. /DEBUG adds the system data, device drivers, and expanded-memory handles to that listing.

**See also:** [`CHKDSK`](#chkdsk) · [`DEBUG`](#debug) · [`LH`](#lh) · [`LOADFIX`](#loadfix)

### MORE

**Page output** — Displays output one screen at a time.

```text
MORE < [drive:][path]file
command | MORE
```

MORE reads the standard input and writes it one screen at a time, waiting for a key at the foot of each screen. It accepts no command-line operands, so its input has to be redirected or piped.

The page height follows the current text mode and falls back to twenty-five lines when that mode cannot be read.

**See also:** [`FIND`](#find) · [`SORT`](#sort) · [`TYPE`](#type)

### PATH

**Executable search path** — Displays or sets the executable search path.

```text
PATH [[drive:]path[;...]]
```

Without an operand, PATH displays the executable search path. PATH ; clears it; any other value replaces it.

Unqualified batch and executable names are searched through the semicolon-separated directories.

**See also:** [`CD`](#cd) · [`SET`](#set) · [`TRUENAME`](#truename)

### PAUSE

**Pause a batch file** — Suspends batch processing until a key is pressed.

```text
PAUSE
```

PAUSE prints a continuation message and suspends the active batch flow until the session receives a key.

**See also:** [`ECHO`](#echo) · [`REM`](#rem)

### PROMPT

**Command prompt format** — Changes the command prompt.

```text
PROMPT [text]
```

PROMPT stores the supplied pattern in the PROMPT environment variable. With no operand, it restores the native default.

Dollar codes insert values such as drive, path, time, date, version, punctuation, escape, backspace, and a literal dollar sign.

**See also:** [`ECHO`](#echo) · [`SET`](#set)

### QBASIC

**CK Basic environment & interpreter (QBASIC.EXE)** — Starts CK Basic (QBASIC.EXE) to edit and run BASIC programs.

```text
QBASIC [/B] [/EDITOR] [/G] [/H] [/MBF] [/NOHI] [[/RUN] [drive:][path]file]
```

CK Basic (shipped under the compatible executable name QBASIC.EXE) edits and runs structured BASIC programs (.BAS) with SUB and FUNCTION procedures, menus, stepping, and an Immediate window.

/RUN loads and executes the named program directly; /EDITOR chains to CK Edit (EDIT.COM); /MBF selects Microsoft Binary Format for MKS$/MKD$/CVS/CVD.

Copyright (c) 2026 C. Klukas. Developed clean-room from public specifications and behavioral testing under the MIT License, with zero Microsoft QBasic source code or help data.

**See also:** [`EDIT`](#edit) · [`HELP`](#help)

### RD

**Remove a directory** — Removes an empty directory.

- **Spellings:** `RD`, `RMDIR`

```text
RD [drive:]path
```

RD removes one empty directory. It refuses the current directory and any directory that is not empty.

**See also:** [`CD`](#cd) · [`DIR`](#dir) · [`MD`](#md)

### RECOVER

**Recover a damaged file** — Recovers a file or a disk with damaged sectors.

```text
RECOVER [drive:][path]file
RECOVER drive:
```

Given a file, RECOVER reads it sector by sector and rebuilds it from the sectors that still read. The file keeps its name and loses whatever the damaged sectors held.

Given only a drive, RECOVER rebuilds the whole root directory and names what it finds FILE0001.REC upwards, so every file has to be identified by hand. Substituted, assigned, and network drives are refused.

**See also:** [`CHKDSK`](#chkdsk) · [`DISKCOPY`](#diskcopy)

### REM

**Batch-file comment** — Adds a comment to a batch file.

```text
REM [comment]
```

REM ignores the remainder of its line. It is commonly used to annotate batch files without changing execution.

**See also:** [`CALL`](#call) · [`GOTO`](#goto)

### REN

**Rename files or directories** — Renames a file or directory.

- **Spellings:** `REN`, `RENAME`

```text
REN [drive:][path]source destination
```

REN changes a file or directory name without moving it to another drive or directory.

A wildcard source may rename multiple entries when the destination is an unqualified DOS wildcard pattern.

**See also:** [`COPY`](#copy) · [`DEL`](#del) · [`DIR`](#dir)

### REPLACE

**Replace files** — Replaces or adds files from one directory to another.

```text
REPLACE source [drive:][path] [/A] [/P] [/R] [/S] [/U] [/W]
```

REPLACE copies files from the source specification over files of the same name in the target directory. Without a target operand the current directory is used.

/A adds only the files that are missing from the target and cannot be combined with /S or /U. /S searches every directory below the target for names to replace, and /U replaces only files older than their source.

/P asks before each file, /R allows read-only files to be overwritten, and /W waits for a key so that a diskette can be swapped before the search starts.

**See also:** [`COPY`](#copy) · [`XCOPY`](#xcopy)

### RESTORE

**Restore backed-up files** — Restores files that were saved by BACKUP.

```text
RESTORE drive: [drive:][path]file [/S] [/P] [/M] [/N]
        [/B:date] [/A:date] [/E:time] [/L:time]
```

RESTORE reads a backup set from the first drive and writes the selected files back. Both the source drive and a target file specification are required.

/S restores subdirectories, /P asks before replacing hidden or read-only files, /M restores only files changed since the backup, and /N restores only files that no longer exist on the target.

/B and /A restore files last changed on or before, and on or after, the given date. /E and /L restore files last changed at or earlier than, and at or later than, the given time.

**See also:** [`BACKUP`](#backup) · [`COPY`](#copy) · [`XCOPY`](#xcopy)

### SET

**Environment variables** — Displays, sets, or removes environment variables.

```text
SET [name=[value]]
```

Without an assignment, SET lists environment variables. NAME=value creates or replaces a value; NAME= removes it.

Variable names are matched without regard to case and are stored in canonical uppercase.

**See also:** [`PATH`](#path) · [`PROMPT`](#prompt)

### SETVER

**Version table manager** — Displays or modifies the DOS version number reported to specific programs.

```text
SETVER [drive:path] [filename n.nn]
SETVER [drive:path] filename /DELETE [/QUIET]
```

With no program operand, SETVER lists the active version-table entries. Supplying an executable file name and version (such as 4.00 or 3.30) records the version DOS reports to that program via INT 21h function 30h.

/DELETE removes an entry from the table, and /QUIET suppresses confirmation messages during deletion. Load DEVICE=SETVER.EXE in CONFIG.SYS when persistent boot-time overrides are required.

**See also:** [`LOADFIX`](#loadfix) · [`MEM`](#mem) · [`VER`](#ver)

### SHIFT

**Shift batch parameters** — Shifts batch parameters %1 through %9.

```text
SHIFT
```

SHIFT discards the current %1 batch argument and moves the remaining positional arguments down one number.

**See also:** [`CALL`](#call) · [`GOTO`](#goto)

### SORT

**Sort text** — Sorts text and writes the result.

```text
SORT [/R] [/+n]
SORT [/R] [/+n] < input > output
```

SORT reads the standard input, orders the lines it finds, and writes them to the standard output. It accepts no file operands, so its input and output have to be redirected or piped.

/R reverses the order. /+n starts each comparison at column n instead of the first column.

**See also:** [`FIND`](#find) · [`MORE`](#more)

### SUBST

**Substitute a drive for a path** — Substitutes a drive letter for a path.

```text
SUBST [drive1: [drive2:]path] [/D]
```

With no operands SUBST lists the substitutions in effect. Given a free drive letter and a path, that letter then stands for the path in every command that follows.

/D cancels the substitution for the named drive. A drive cannot be substituted for a path that lies on itself.

**See also:** [`JOIN`](#join) · [`PATH`](#path) · [`TRUENAME`](#truename)

### SYS

**Transfer the system** — Copies the DOS system files to a disk.

```text
SYS [source-drive:] target-drive:
```

SYS copies IO.SYS and MSDOS.SYS to the target disk so that it can start DOS. A target drive is required and may not be the current drive; a source drive may be given in front of it.

The target has to have room for the system files where the loader expects them, which normally means a disk formatted with that space reserved. Network, substituted, and assigned drives are refused.

**See also:** [`FORMAT`](#format) · [`VER`](#ver)

### TIME

**System time** — Displays or sets the system time.

```text
TIME [hh:mm[:ss]]
```

With no operand, TIME displays the current time and prompts for a replacement. Enter accepts the current value.

Hours use 0 through 23; minutes and seconds use 0 through 59. A fractional suffix is accepted and ignored.

**See also:** [`DATE`](#date) · [`VER`](#ver)

### TREE

**Directory tree** — Displays the directory structure of a drive.

```text
TREE [drive:][path] [/F] [/A]
```

TREE walks the directories below the named path, or below the current directory when no path is given, and draws each level with the line-drawing characters of the active code page.

/F also lists the files in every directory it reaches. /A draws the tree with plain text characters instead, for printers and code pages that cannot show the line-drawing set.

**See also:** [`CD`](#cd) · [`DIR`](#dir) · [`JOIN`](#join)

### TRUENAME

**Resolve a DOS path** — Displays the fully resolved path.

```text
TRUENAME [drive:][path]
```

TRUENAME resolves the supplied DOS path against the current drive and directory and prints its canonical drive-qualified form.

**See also:** [`CD`](#cd) · [`PATH`](#path)

### TYPE

**Display a text file** — Displays a text file.

```text
TYPE [drive:][path]file
```

TYPE writes the selected file bytes to the current command output. It does not edit or convert the file.

**See also:** [`DIR`](#dir) · [`ECHO`](#echo)

### VER

**CK-DOS 5 version** — Displays the CK-DOS 5 operating-system version.

```text
VER
```

VER prints the active CK-DOS 5 version banner on standard output. Programs that query INT 21h function 30h receive the active DOS version or any per-program override configured with SETVER.

**See also:** [`HELP`](#help) · [`SETVER`](#setver) · [`VOL`](#vol)

### VERIFY

**Write verification** — Controls write verification.

```text
VERIFY [ON|OFF]
```

With no operand, VERIFY reports the current state. ON and OFF change the shell's write-verification setting.

**See also:** [`BREAK`](#break) · [`COPY`](#copy)

### VOL

**Disk volume identity** — Displays a disk volume label.

```text
VOL [drive:]
```

VOL prints the volume label and serial number for the selected drive, or for the current drive when none is supplied.

**See also:** [`DIR`](#dir) · [`VER`](#ver)

### XCOPY

**Copy directory trees** — Copies files and directory trees.

```text
XCOPY source [destination] [/A|/M] [/D:date] [/E] [/P] [/S] [/V] [/W]
```

XCOPY copies the files that match the source specification, using the current directory when no destination is given. It reads as many files as memory allows before writing them, so it crosses a tree faster than a plain copy.

/S copies the directories below the source that contain files, and /E also creates the empty ones. /A copies files whose archive attribute is set and leaves it alone; /M copies the same files and clears it.

/D copies only files dated on or after the given date, written in the format of the active country setting. /P asks before each file, /V verifies each write, and /W waits for a key before the copy starts.

**See also:** [`COPY`](#copy) · [`REPLACE`](#replace) · [`TREE`](#tree)

