; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; CK-DOS HELP browser.  MASM 5.10 / 8086 source for a tiny-model COM program:
;
;   MASM HELP.ASM,HELP.OBJ,NUL,NUL;
;   LINK HELP.OBJ+DOSUI.OBJ;
;   EXE2BIN HELP.EXE HELP.COM
;
; The authored topics are assembled in from HELPDATA.INC, which is generated
; from CMD/HELP/topics.json.  Nothing here carries prose of its own.
;
; With an INT 33h driver present (CK-DOS's built-in pointer service or a
; guest driver on the PS/2 profile), the menu-bar headers and their items,
; the search field, the topic list, the rule between the panes as a scroll
; rail, the See also links, and the About dialog answer left clicks; without
; one, behavior is exactly the keyboard-only browser.

.8086
DOSUI_TEXT SEGMENT PUBLIC 'CODE'
ASSUME CS:DOSUI_TEXT, DS:DOSUI_TEXT, ES:DOSUI_TEXT
ORG 100H

INCLUDE DOSUI.INC

; ---------------------------------------------------------------------------
; Screen layout.  Mode 3, 80x25.
; ---------------------------------------------------------------------------
MENU_ROW        EQU 0
SEARCH_ROW      EQU 1
FRAME_ROW       EQU 2
LIST_TOP        EQU 3
LIST_BOTTOM     EQU 22
LIST_ROWS       EQU LIST_BOTTOM - LIST_TOP + 1
LIST_LEFT       EQU 1
LIST_WIDTH      EQU 20
TEXT_LEFT       EQU 23
TEXT_WIDTH      EQU 56
STATUS_ROW      EQU 24
; The row between the panes and the status line carries nothing.  It still has
; to be written by somebody now that no full-screen clear runs.
GAP_ROW         EQU LIST_BOTTOM + 1

; Mode-3 text RAM, addressed directly by WRAP_EMIT_RUN.
TEXT_SEGMENT    EQU 0B800H
ROW_BYTES       EQU DOSUI_SCREEN_COLUMNS * 2

ATTR_NORMAL     EQU 07H
ATTR_FRAME      EQU 07H
ATTR_BAR        EQU 70H
ATTR_SELECTED   EQU 70H
ATTR_HEADING    EQU 0FH
ATTR_STATUS     EQU 70H

SEARCH_MAX      EQU 16

; The longest authored topic identifier is eight bytes, so a command tail
; operand longer than this cannot name a topic and is kept only to be reported.
ARG_MAX         EQU 16

KEY_ESC         EQU 27
KEY_TAB         EQU 9
KEY_BACKSPACE   EQU 8
KEY_ENTER       EQU 13
SCAN_UP         EQU 48H
SCAN_DOWN       EQU 50H
SCAN_PGUP       EQU 49H
SCAN_PGDN       EQU 51H
SCAN_HOME       EQU 47H
SCAN_END        EQU 4FH
SCAN_LEFT       EQU 4BH
SCAN_RIGHT      EQU 4DH
SCAN_ALT_F      EQU 21H
SCAN_ALT_H      EQU 23H
SCAN_F10        EQU 44H

; The two drop-downs sit under their headers in MENU_BAR_TEXT.
MENU_DROP_ROW   EQU 2
FILE_MENU_LEFT  EQU 2
HELP_MENU_LEFT  EQU 8
MENU_DROP_WIDTH EQU 10
ATTR_MENU_FRAME EQU 70H
ATTR_MENU_FILL  EQU 70H
ABOUT_TOP       EQU 8
ABOUT_LEFT      EQU 20

START:
        JMP     BEGIN

; The generated corpus sits immediately after the entry jump so that every
; HELP_* equate is defined before the data below needs it. A DUP count has to
; be known in pass one; an immediate does not.
INCLUDE HELPDATA.INC

BEGIN:
        PUSH    CS
        POP     DS
        PUSH    CS
        POP     ES
        CLD
        ; Read the command tail before the screen is taken over: the PSP shares
        ; offset 80h with the default DTA, so the operand is only reliably
        ; there while nothing else has run.
        CALL    PARSE_ARGS
        CALL    DOSUI_INIT
        CALL    DOSUI_MOUSE_INIT
        CALL    BUILD_FILTER
        CALL    APPLY_ARG
        CALL    DRAW_SCREEN
MAIN_LOOP:
        CALL    DOSUI_READ_EVENT
        JNC     SHORT MAIN_HAVE_KEY
        JMP     HELP_MOUSE
MAIN_HAVE_KEY:
        OR      AL,AL
        JNZ     SHORT MAIN_J01
        JMP     MAIN_EXTENDED
MAIN_J01:

        CMP     AL,KEY_ESC
        JNE     SHORT MAIN_NOT_ESC
        ; Escape backs out of a search before it leaves the browser.
        CMP     BYTE PTR SEARCH_LEN,0
        JNE     SHORT MAIN_J02
        JMP     MAIN_QUIT
MAIN_J02:
        MOV     BYTE PTR SEARCH_LEN,0
        MOV     BYTE PTR SEARCH_TEXT,0
        CALL    BUILD_FILTER
        JMP     MAIN_REDRAW
MAIN_NOT_ESC:
        CMP     AL,KEY_TAB
        JNE     SHORT MAIN_NOT_TAB
        MOV     AL,FOCUS
        INC     AL
        CMP     AL,3
        JB      SHORT MAIN_TAB_STORE
        XOR     AL,AL
MAIN_TAB_STORE:
        MOV     FOCUS,AL
        JMP     MAIN_REDRAW
MAIN_NOT_TAB:
        CMP     AL,KEY_ENTER
        JNE     SHORT MAIN_NOT_ENTER
        CMP     BYTE PTR FOCUS,2
        JNE     SHORT MAIN_ENTER_LIST
        CALL    ACTIVATE_LINK
        JMP     MAIN_REDRAW
MAIN_ENTER_LIST:
        MOV     BYTE PTR FOCUS,0
        JMP     MAIN_REDRAW
MAIN_NOT_ENTER:
        CMP     BYTE PTR FOCUS,1
        JE      SHORT MAIN_J03
        JMP     MAIN_LOOP
MAIN_J03:
        CMP     AL,KEY_BACKSPACE
        JNE     SHORT MAIN_NOT_BACKSPACE
        CALL    SEARCH_BACKSPACE
        CALL    BUILD_FILTER
        JMP     MAIN_REDRAW
MAIN_NOT_BACKSPACE:
        CMP     AL,' '
        JAE     SHORT MAIN_J04
        JMP     MAIN_LOOP
MAIN_J04:
        CMP     AL,7EH
        JBE     SHORT MAIN_J05
        JMP     MAIN_LOOP
MAIN_J05:
        CALL    SEARCH_APPEND
        CALL    BUILD_FILTER
        JMP     MAIN_REDRAW

MAIN_EXTENDED:
        CMP     AH,SCAN_ALT_F
        JNE     SHORT MAIN_NOT_ALT_F
        MOV     AL,1
        JMP     SHORT MAIN_OPEN_MENU
MAIN_NOT_ALT_F:
        CMP     AH,SCAN_F10
        JNE     SHORT MAIN_NOT_F10
        MOV     AL,1
        JMP     SHORT MAIN_OPEN_MENU
MAIN_NOT_F10:
        CMP     AH,SCAN_ALT_H
        JNE     SHORT MAIN_NOT_ALT_H
        MOV     AL,2
MAIN_OPEN_MENU:
        CALL    SHOW_MENU
        CMP     AL,1
        JNE     SHORT MAIN_MENU_ABOUT
        JMP     MAIN_QUIT
MAIN_MENU_ABOUT:
        CMP     AL,2
        JNE     SHORT MAIN_MENU_DONE
        CALL    SHOW_ABOUT
MAIN_MENU_DONE:
        JMP     MAIN_REDRAW
MAIN_NOT_ALT_H:
        CMP     AH,SCAN_LEFT
        JNE     SHORT MAIN_NOT_LEFT
        CMP     BYTE PTR FOCUS,2
        JE      SHORT MAIN_J06
        JMP     MAIN_LOOP
MAIN_J06:
        CMP     BYTE PTR LINK_INDEX,0
        JNE     SHORT MAIN_J07
        JMP     MAIN_LOOP
MAIN_J07:
        DEC     BYTE PTR LINK_INDEX
        JMP     MAIN_REDRAW
MAIN_NOT_LEFT:
        CMP     AH,SCAN_RIGHT
        JNE     SHORT MAIN_NOT_RIGHT
        CMP     BYTE PTR FOCUS,2
        JE      SHORT MAIN_J08
        JMP     MAIN_LOOP
MAIN_J08:
        MOV     AL,LINK_INDEX
        INC     AL
        CMP     AL,COUNT_SEE
        JB      SHORT MAIN_J09
        JMP     MAIN_LOOP
MAIN_J09:
        MOV     LINK_INDEX,AL
        JMP     MAIN_REDRAW
MAIN_NOT_RIGHT:
        CMP     AH,SCAN_UP
        JNE     SHORT MAIN_NOT_UP
        CALL    SELECT_PREVIOUS
        JMP     MAIN_REDRAW
MAIN_NOT_UP:
        CMP     AH,SCAN_DOWN
        JNE     SHORT MAIN_NOT_DOWN
        CALL    SELECT_NEXT
        JMP     MAIN_REDRAW
MAIN_NOT_DOWN:
        CMP     AH,SCAN_PGUP
        JNE     SHORT MAIN_NOT_PGUP
        CALL    SELECT_PAGE_UP
        JMP     MAIN_REDRAW
MAIN_NOT_PGUP:
        CMP     AH,SCAN_PGDN
        JNE     SHORT MAIN_NOT_PGDN
        CALL    SELECT_PAGE_DOWN
        JMP     MAIN_REDRAW
MAIN_NOT_PGDN:
        CMP     AH,SCAN_HOME
        JNE     SHORT MAIN_NOT_HOME
        MOV     WORD PTR SELECTED_POS,0
        JMP     MAIN_REDRAW
MAIN_NOT_HOME:
        CMP     AH,SCAN_END
        JE      SHORT MAIN_J10
        JMP     MAIN_LOOP
MAIN_J10:
        CALL    SELECT_LAST
MAIN_REDRAW:
        ; The startup notice reports how the browser was invoked. The first key
        ; that changes anything has acknowledged it, so the status line goes
        ; back to carrying the key hints.
        MOV     BYTE PTR NOTICE_ACTIVE,0
        CALL    ENSURE_VISIBLE
        CALL    DRAW_SCREEN
        JMP     MAIN_LOOP
MAIN_QUIT:
        CALL    DOSUI_MOUSE_SHUTDOWN
        CALL    DOSUI_SHUTDOWN
        MOV     AX,4C00H
        INT     21H

; ---------------------------------------------------------------------------
; Route one main-loop click (DH = cell row, DL = cell column) to the same
; handlers the keyboard reaches: the menu-bar headers, the search field, the
; topic list, the rule between the panes as a scroll rail, and the See also
; links.  A click that lands on nothing is ignored, exactly like an unmapped
; key.
; ---------------------------------------------------------------------------
HELP_MOUSE:
        OR      DH,DH
        JNZ     SHORT HELP_MOUSE_NOT_MENU_BAR
        CALL    MENU_FROM_BAR_COLUMN
        CMP     AL,0FFH
        JE      SHORT HELP_MOUSE_MISS
        JMP     MAIN_OPEN_MENU
HELP_MOUSE_NOT_MENU_BAR:
        CMP     DH,SEARCH_ROW
        JNE     SHORT HELP_MOUSE_NOT_SEARCH
        ; The label and the field: everything DRAW_SEARCH_ROW writes.
        CMP     DL,LIST_LEFT
        JB      SHORT HELP_MOUSE_MISS
        CMP     DL,LIST_LEFT + 8 + SEARCH_MAX
        JA      SHORT HELP_MOUSE_MISS
        MOV     BYTE PTR FOCUS,1
        JMP     MAIN_REDRAW
HELP_MOUSE_MISS:
        JMP     MAIN_LOOP
HELP_MOUSE_NOT_SEARCH:
        CMP     DH,LIST_TOP
        JB      SHORT HELP_MOUSE_MISS
        CMP     DH,LIST_BOTTOM
        JA      SHORT HELP_MOUSE_MISS
        CMP     DL,TEXT_LEFT - 1
        JE      SHORT HELP_MOUSE_RAIL
        JB      SHORT HELP_MOUSE_LIST
        ; The reading pane: only the See also row answers clicks, and only
        ; on a link.  TEXT_SEE_ROW is 0FFh when no See also line is on
        ; screen, which no click row can equal.
        CMP     DH,TEXT_SEE_ROW
        JNE     SHORT HELP_MOUSE_MISS
        CALL    LINK_FROM_COLUMN
        CMP     AL,0FFH
        JE      SHORT HELP_MOUSE_MISS
        MOV     LINK_INDEX,AL
        MOV     BYTE PTR FOCUS,2
        CALL    ACTIVATE_LINK
        JMP     MAIN_REDRAW
HELP_MOUSE_RAIL:
        ; The rule between the panes doubles as the list's scroll rail, the
        ; way EDIT's right rail does: the end cells move by one entry, the
        ; stretch between them by a page toward the clicked half.
        CMP     DH,LIST_TOP
        JNE     SHORT HELP_MOUSE_RAIL_NOT_TOP
        CALL    SELECT_PREVIOUS
        JMP     MAIN_REDRAW
HELP_MOUSE_RAIL_NOT_TOP:
        CMP     DH,LIST_BOTTOM
        JNE     SHORT HELP_MOUSE_RAIL_BETWEEN
        CALL    SELECT_NEXT
        JMP     MAIN_REDRAW
HELP_MOUSE_RAIL_BETWEEN:
        CMP     DH,(LIST_TOP + LIST_BOTTOM) / 2
        JA      SHORT HELP_MOUSE_RAIL_DOWN
        CALL    SELECT_PAGE_UP
        JMP     MAIN_REDRAW
HELP_MOUSE_RAIL_DOWN:
        CALL    SELECT_PAGE_DOWN
        JMP     MAIN_REDRAW
HELP_MOUSE_LIST:
        ; A list cell names the entry on its row; a vacated row names none.
        MOV     AL,DH
        SUB     AL,LIST_TOP
        XOR     AH,AH
        ADD     AX,LIST_FIRST
        CMP     AX,FILTER_COUNT
        JAE     SHORT HELP_MOUSE_LIST_MISS
        MOV     SELECTED_POS,AX
        MOV     BYTE PTR FOCUS,0
        JMP     MAIN_REDRAW
HELP_MOUSE_LIST_MISS:
        JMP     MAIN_LOOP

; ---------------------------------------------------------------------------
; Selection.
; ---------------------------------------------------------------------------
SELECT_NEXT PROC NEAR
        MOV     AX,SELECTED_POS
        INC     AX
        CMP     AX,FILTER_COUNT
        JB      SHORT SELECT_NEXT_STORE
        MOV     AX,FILTER_COUNT
        OR      AX,AX
        JZ      SHORT SELECT_NEXT_ZERO
        DEC     AX
SELECT_NEXT_ZERO:
SELECT_NEXT_STORE:
        MOV     SELECTED_POS,AX
        RET
SELECT_NEXT ENDP

SELECT_PREVIOUS PROC NEAR
        MOV     AX,SELECTED_POS
        OR      AX,AX
        JZ      SHORT SELECT_PREVIOUS_DONE
        DEC     AX
        MOV     SELECTED_POS,AX
SELECT_PREVIOUS_DONE:
        RET
SELECT_PREVIOUS ENDP

SELECT_PAGE_DOWN PROC NEAR
        MOV     AX,SELECTED_POS
        ADD     AX,LIST_ROWS
        CMP     AX,FILTER_COUNT
        JB      SHORT SELECT_PAGE_DOWN_STORE
        MOV     AX,FILTER_COUNT
        OR      AX,AX
        JZ      SHORT SELECT_PAGE_DOWN_STORE
        DEC     AX
SELECT_PAGE_DOWN_STORE:
        MOV     SELECTED_POS,AX
        RET
SELECT_PAGE_DOWN ENDP

SELECT_PAGE_UP PROC NEAR
        MOV     AX,SELECTED_POS
        CMP     AX,LIST_ROWS
        JAE     SHORT SELECT_PAGE_UP_SUB
        XOR     AX,AX
        JMP     SHORT SELECT_PAGE_UP_STORE
SELECT_PAGE_UP_SUB:
        SUB     AX,LIST_ROWS
SELECT_PAGE_UP_STORE:
        MOV     SELECTED_POS,AX
        RET
SELECT_PAGE_UP ENDP

SELECT_LAST PROC NEAR
        MOV     AX,FILTER_COUNT
        OR      AX,AX
        JZ      SHORT SELECT_LAST_STORE
        DEC     AX
SELECT_LAST_STORE:
        MOV     SELECTED_POS,AX
        RET
SELECT_LAST ENDP

; Scroll the list so the selection is on screen.
ENSURE_VISIBLE PROC NEAR
        PUSH    AX
        PUSH    BX
        MOV     AX,SELECTED_POS
        MOV     BX,LIST_FIRST
        CMP     AX,BX
        JAE     SHORT ENSURE_VISIBLE_BOTTOM
        MOV     LIST_FIRST,AX
        JMP     SHORT ENSURE_VISIBLE_DONE
ENSURE_VISIBLE_BOTTOM:
        SUB     AX,BX
        CMP     AX,LIST_ROWS
        JB      SHORT ENSURE_VISIBLE_DONE
        MOV     AX,SELECTED_POS
        SUB     AX,LIST_ROWS - 1
        MOV     LIST_FIRST,AX
ENSURE_VISIBLE_DONE:
        POP     BX
        POP     AX
        RET
ENSURE_VISIBLE ENDP

; Out: AX = the absolute topic index the selection names.
CURRENT_TOPIC PROC NEAR
        PUSH    BX
        MOV     BX,SELECTED_POS
        SHL     BX,1
        MOV     AX,WORD PTR FILTER_LIST[BX]
        POP     BX
        RET
CURRENT_TOPIC ENDP

; In: AX = an absolute topic index.  Selects it if the filter carries it.
SELECT_TOPIC PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     DX,AX
        XOR     CX,CX
SELECT_TOPIC_NEXT:
        CMP     CX,FILTER_COUNT
        JAE     SHORT SELECT_TOPIC_DONE
        MOV     BX,CX
        SHL     BX,1
        MOV     AX,WORD PTR FILTER_LIST[BX]
        CMP     AX,DX
        JNE     SHORT SELECT_TOPIC_SKIP
        MOV     SELECTED_POS,CX
        JMP     SHORT SELECT_TOPIC_DONE
SELECT_TOPIC_SKIP:
        INC     CX
        JMP     SHORT SELECT_TOPIC_NEXT
SELECT_TOPIC_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
SELECT_TOPIC ENDP

; ---------------------------------------------------------------------------
; Command tail.  The authored HELP topic promises that "HELP followed by a
; command name opens that topic", so the operand is resolved through the same
; generated alias table the see-also links use and lands on the same entry:
; HELP CHDIR and HELP CD open one topic, matching the host prompt.
; ---------------------------------------------------------------------------

; In: AL = one command-tail byte.  Out: AL = 1 when it ends an operand.
; Every control byte counts, which covers the 0Dh DOS writes after the tail.
ARG_IS_BREAK PROC NEAR
        CMP     AL,' '
        JBE     SHORT ARG_IS_BREAK_YES
        CMP     AL,','
        JE      SHORT ARG_IS_BREAK_YES
        CMP     AL,';'
        JE      SHORT ARG_IS_BREAK_YES
        CMP     AL,'='
        JE      SHORT ARG_IS_BREAK_YES
        CMP     AL,'+'
        JE      SHORT ARG_IS_BREAK_YES
        XOR     AL,AL
        RET
ARG_IS_BREAK_YES:
        MOV     AL,1
        RET
ARG_IS_BREAK ENDP

; Copy the first command-tail operand into ARG_TEXT, upper-cased so it can be
; compared against the corpus spellings without folding case.  A COM image
; enters with DS = CS = the PSP, so the length byte at 80h and the text at 81h
; are directly addressable.  Out: ARG_LEN = the stored length, zero when the
; tail names nothing.  Trailing operands are ignored; the browser opens one
; topic, and refusing a second word would only turn a usable request away.
PARSE_ARGS PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    SI
        MOV     BYTE PTR ARG_LEN,0
        MOV     BYTE PTR ARG_TEXT,0
        XOR     BX,BX                   ; BX = bytes stored
        MOV     SI,0080H
        MOV     CL,[SI]
        XOR     CH,CH
        INC     SI                      ; SI = 0081h, the tail text
        JCXZ    PARSE_ARGS_STORE
PARSE_ARGS_SKIP:
        MOV     AL,[SI]
        CALL    ARG_IS_BREAK
        OR      AL,AL
        JZ      SHORT PARSE_ARGS_WORD
        INC     SI
        LOOP    PARSE_ARGS_SKIP
        JMP     SHORT PARSE_ARGS_STORE
PARSE_ARGS_WORD:
        ; A leading switch is not a topic name, so HELP /? still opens the
        ; index rather than reporting "/?" as a missing topic.
        CMP     BYTE PTR [SI],'/'
        JE      SHORT PARSE_ARGS_STORE
PARSE_ARGS_TAKE:
        MOV     AL,[SI]
        CALL    ARG_IS_BREAK
        OR      AL,AL
        JNZ     SHORT PARSE_ARGS_STORE
        CMP     BX,ARG_MAX
        JAE     SHORT PARSE_ARGS_STORE
        MOV     AL,[SI]
        CALL    UPPER_AL
        MOV     BYTE PTR ARG_TEXT[BX],AL
        INC     BX
        INC     SI
        LOOP    PARSE_ARGS_TAKE
PARSE_ARGS_STORE:
        MOV     BYTE PTR ARG_TEXT[BX],0
        MOV     AL,BL
        MOV     ARG_LEN,AL
        POP     SI
        POP     CX
        POP     BX
        POP     AX
        RET
PARSE_ARGS ENDP

; Open the topic the command tail named.  Call after BUILD_FILTER so the whole
; index is in FILTER_LIST and the selection can reach any entry; moving the
; selection is what makes the list, the text pane and the status line agree,
; because every one of them reads SELECTED_POS.
;
; A name that resolves to nothing raises a status-line notice and leaves the
; browser on the index.  Refusing to start would strand the user in the shell
; holding a name they cannot look up, and the browser they were denied is
; exactly the tool that finds it; ignoring the name silently would let HELP
; contradict its own topic without saying so.
APPLY_ARG PROC NEAR
        PUSH    AX
        PUSH    SI
        CMP     BYTE PTR ARG_LEN,0
        JE      SHORT APPLY_ARG_DONE
        MOV     SI,OFFSET ARG_TEXT
        CALL    RESOLVE_ALIAS
        CMP     AX,0FFFFH
        JE      SHORT APPLY_ARG_MISSING
        CALL    SELECT_TOPIC
        CALL    ENSURE_VISIBLE
        JMP     SHORT APPLY_ARG_DONE
APPLY_ARG_MISSING:
        MOV     BYTE PTR NOTICE_ACTIVE,1
APPLY_ARG_DONE:
        POP     SI
        POP     AX
        RET
APPLY_ARG ENDP

; ---------------------------------------------------------------------------
; Menus.  The frame, shadow and item rendering are DOSUI's, shared with EDIT.
; What lives here is this program's policy: which menus exist and what they do.
; Both items are always available, so the default DOSUI_MENU_MASK_VECTOR stands
; and HELP needs no enable routine of its own.
; ---------------------------------------------------------------------------

; Map a menu-bar cell column in DL to 1 for File, 2 for Help, or 0FFh
; outside both headers.  The spans are the header glyphs MENU_BAR_TEXT puts
; at those cells.
MENU_FROM_BAR_COLUMN PROC NEAR
        MOV     AL,1
        CMP     DL,FILE_MENU_LEFT
        JB      SHORT MENU_FROM_BAR_NONE
        CMP     DL,FILE_MENU_LEFT + 3
        JBE     SHORT MENU_FROM_BAR_DONE
        MOV     AL,2
        CMP     DL,HELP_MENU_LEFT
        JB      SHORT MENU_FROM_BAR_NONE
        CMP     DL,HELP_MENU_LEFT + 3
        JBE     SHORT MENU_FROM_BAR_DONE
MENU_FROM_BAR_NONE:
        MOV     AL,0FFH
MENU_FROM_BAR_DONE:
        RET
MENU_FROM_BAR_COLUMN ENDP

; In: AL = 1 for File, 2 for Help.  Out: AL = 0 dismissed, 1 Exit, 2 About.
; Each menu carries exactly one item, so the chosen command is the menu itself.
SHOW_MENU PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     MENU_OPEN,AL
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
SHOW_MENU_REDRAW:
        CALL    DRAW_SCREEN
        CALL    DRAW_OPEN_MENU
SHOW_MENU_KEY:
        CALL    DOSUI_READ_EVENT
        JNC     SHORT SHOW_MENU_HAVE_KEY
        JMP     SHOW_MENU_MOUSE
SHOW_MENU_HAVE_KEY:
        OR      AL,AL
        JZ      SHORT SHOW_MENU_EXTENDED
        CMP     AL,KEY_ESC
        JNE     SHORT SHOW_MENU_NOT_ESC
        XOR     AL,AL
        JMP     SHORT SHOW_MENU_DONE
SHOW_MENU_NOT_ESC:
        CMP     AL,KEY_ENTER
        JNE     SHORT SHOW_MENU_KEY
        MOV     AL,MENU_OPEN
        JMP     SHORT SHOW_MENU_DONE
SHOW_MENU_EXTENDED:
        CMP     AH,SCAN_ALT_F
        JNE     SHORT SHOW_MENU_TRY_H
        MOV     BYTE PTR MENU_OPEN,1
        JMP     SHORT SHOW_MENU_REDRAW
SHOW_MENU_TRY_H:
        CMP     AH,SCAN_ALT_H
        JNE     SHORT SHOW_MENU_TRY_MOVE
        MOV     BYTE PTR MENU_OPEN,2
        JMP     SHORT SHOW_MENU_REDRAW
SHOW_MENU_TRY_MOVE:
        CMP     AH,SCAN_LEFT
        JE      SHORT SHOW_MENU_OTHER
        CMP     AH,SCAN_RIGHT
        JNE     SHORT SHOW_MENU_KEY
SHOW_MENU_OTHER:
        ; With two menus, Left and Right both mean "the other one".
        CMP     BYTE PTR MENU_OPEN,1
        JNE     SHORT SHOW_MENU_OTHER_FILE
        MOV     BYTE PTR MENU_OPEN,2
        JMP     SHORT SHOW_MENU_REDRAW
SHOW_MENU_OTHER_FILE:
        MOV     BYTE PTR MENU_OPEN,1
        JMP     SHORT SHOW_MENU_REDRAW
SHOW_MENU_DONE:
        MOV     BYTE PTR MENU_OPEN,0
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        RET

; A click while a drop-down is open, EDIT's menu policy: another header
; switches to its menu, the open header or any cell outside the drop-down
; dismisses the scope, and the single item row executes exactly as Enter
; would.  Clicks on the frame itself are ignored, like a separator row.
SHOW_MENU_MOUSE:
        OR      DH,DH
        JNZ     SHORT SHOW_MENU_MOUSE_NOT_BAR
        CALL    MENU_FROM_BAR_COLUMN
        CMP     AL,0FFH
        JNE     SHORT SHOW_MENU_MOUSE_HEADER
        JMP     SHOW_MENU_KEY
SHOW_MENU_MOUSE_HEADER:
        CMP     AL,MENU_OPEN
        JE      SHORT SHOW_MENU_MOUSE_CLOSE
        MOV     MENU_OPEN,AL
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_MOUSE_NOT_BAR:
        ; The interior of the open drop-down is its single item row.
        MOV     AL,FILE_MENU_LEFT
        CMP     BYTE PTR MENU_OPEN,1
        JE      SHORT SHOW_MENU_MOUSE_HAVE_LEFT
        MOV     AL,HELP_MENU_LEFT
SHOW_MENU_MOUSE_HAVE_LEFT:
        CMP     DH,MENU_DROP_ROW
        JNE     SHORT SHOW_MENU_MOUSE_CLOSE
        CMP     DL,AL
        JBE     SHORT SHOW_MENU_MOUSE_CLOSE
        ADD     AL,MENU_DROP_WIDTH
        CMP     DL,AL
        JA      SHORT SHOW_MENU_MOUSE_CLOSE
        MOV     AL,MENU_OPEN
        JMP     SHOW_MENU_DONE
SHOW_MENU_MOUSE_CLOSE:
        XOR     AL,AL
        JMP     SHOW_MENU_DONE
SHOW_MENU ENDP

DRAW_OPEN_MENU PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     BYTE PTR DOSUI_MENU_ITEM_WIDTH,MENU_DROP_WIDTH
        CMP     BYTE PTR MENU_OPEN,1
        JNE     SHORT DRAW_OPEN_HELP
        MOV     SI,OFFSET FILE_MENU_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET FILE_EXIT_TEXT
        MOV     DH,MENU_DROP_ROW
        MOV     DL,FILE_MENU_LEFT + 1
        XOR     AX,AX
        CALL    DOSUI_MENU_ITEM
        JMP     SHORT DRAW_OPEN_DONE
DRAW_OPEN_HELP:
        CMP     BYTE PTR MENU_OPEN,2
        JNE     SHORT DRAW_OPEN_DONE
        MOV     SI,OFFSET HELP_MENU_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET HELP_ABOUT_TEXT
        MOV     DH,MENU_DROP_ROW
        MOV     DL,HELP_MENU_LEFT + 1
        XOR     AX,AX
        CALL    DOSUI_MENU_ITEM
DRAW_OPEN_DONE:
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
DRAW_OPEN_MENU ENDP

SHOW_ABOUT PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    DX
        PUSH    SI
        CALL    DOSUI_HIDE_CURSOR
        MOV     SI,OFFSET ABOUT_RECT
        CALL    DOSUI_DRAW_MODAL
        MOV     BL,ATTR_HEADING
        MOV     SI,OFFSET ABOUT_LINE1
        MOV     DH,ABOUT_TOP + 2
        MOV     DL,ABOUT_LEFT + 3
        CALL    DOSUI_WRITE_Z
        MOV     BL,ATTR_MENU_FILL
        MOV     SI,OFFSET ABOUT_LINE2
        MOV     DH,ABOUT_TOP + 4
        MOV     DL,ABOUT_LEFT + 3
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET ABOUT_LINE3
        MOV     DH,ABOUT_TOP + 6
        MOV     DL,ABOUT_LEFT + 3
        CALL    DOSUI_WRITE_Z
        ; Any key or any click dismisses the dialog.
        CALL    DOSUI_READ_EVENT
        ; The dialog covered rows 8..16 of the reading pane, so what DRAW_TEXT
        ; believes is on screen there is gone.  Without this the redraw that
        ; dismisses the dialog would skip the body and leave the frame behind.
        MOV     WORD PTR TEXT_TOPIC,0FFFEH
        POP     SI
        POP     DX
        POP     BX
        POP     AX
        RET
SHOW_ABOUT ENDP

; ---------------------------------------------------------------------------
; Search.  SEARCH_TEXT is stored already upper-cased, so matching never has to
; fold case: every authored topic identifier is upper-case too.
; ---------------------------------------------------------------------------
UPPER_AL PROC NEAR
        CMP     AL,'a'
        JB      SHORT UPPER_AL_DONE
        CMP     AL,'z'
        JA      SHORT UPPER_AL_DONE
        SUB     AL,20H
UPPER_AL_DONE:
        RET
UPPER_AL ENDP

; In: SI = a topic identifier.  Out: AL = 1 when SEARCH_TEXT occurs in it.
MATCH_TOPIC PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        MOV     CL,SEARCH_LEN
        OR      CL,CL
        JZ      SHORT MATCH_TOPIC_YES
MATCH_TOPIC_START:
        CMP     BYTE PTR [SI],0
        JE      SHORT MATCH_TOPIC_NO
        MOV     DI,OFFSET SEARCH_TEXT
        MOV     BX,SI
        XOR     CH,CH
MATCH_TOPIC_COMPARE:
        MOV     AL,[BX]
        OR      AL,AL
        JZ      SHORT MATCH_TOPIC_ADVANCE
        MOV     AH,[DI]
        CMP     AL,AH
        JNE     SHORT MATCH_TOPIC_ADVANCE
        INC     BX
        INC     DI
        INC     CH
        CMP     CH,CL
        JB      SHORT MATCH_TOPIC_COMPARE
        JMP     SHORT MATCH_TOPIC_YES
MATCH_TOPIC_ADVANCE:
        INC     SI
        JMP     SHORT MATCH_TOPIC_START
MATCH_TOPIC_NO:
        XOR     AL,AL
        JMP     SHORT MATCH_TOPIC_DONE
MATCH_TOPIC_YES:
        MOV     AL,1
MATCH_TOPIC_DONE:
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        RET
MATCH_TOPIC ENDP

; Rebuild FILTER_LIST from SEARCH_TEXT and keep the selection inside it.
BUILD_FILTER PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        XOR     DI,DI
        XOR     CX,CX
BUILD_FILTER_NEXT:
        CMP     CX,HELP_TOPIC_COUNT
        JAE     SHORT BUILD_FILTER_DONE
        MOV     AX,CX
        CALL    TOPIC_RECORD
        MOV     AX,WORD PTR HELP_TOPICS[BX]
        CALL    POOL_STRING
        CALL    MATCH_TOPIC
        OR      AL,AL
        JZ      SHORT BUILD_FILTER_SKIP
        MOV     BX,DI
        SHL     BX,1
        MOV     WORD PTR FILTER_LIST[BX],CX
        INC     DI
BUILD_FILTER_SKIP:
        INC     CX
        JMP     SHORT BUILD_FILTER_NEXT
BUILD_FILTER_DONE:
        MOV     FILTER_COUNT,DI
        MOV     AX,SELECTED_POS
        CMP     AX,DI
        JB      SHORT BUILD_FILTER_KEEP
        MOV     AX,DI
        OR      AX,AX
        JZ      SHORT BUILD_FILTER_ZERO
        DEC     AX
BUILD_FILTER_ZERO:
        MOV     SELECTED_POS,AX
BUILD_FILTER_KEEP:
        MOV     BYTE PTR LINK_INDEX,0
        CALL    ENSURE_VISIBLE
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        POP     AX
        RET
BUILD_FILTER ENDP

; Compare two zero-terminated strings.  Out: AL = 0 equal, 1 SI<DI, 2 SI>DI.
COMPARE_Z PROC NEAR
        PUSH    BX
        PUSH    SI
        PUSH    DI
COMPARE_Z_NEXT:
        MOV     AL,[SI]
        MOV     BL,[DI]
        CMP     AL,BL
        JB      SHORT COMPARE_Z_LESS
        JA      SHORT COMPARE_Z_MORE
        OR      AL,AL
        JZ      SHORT COMPARE_Z_EQUAL
        INC     SI
        INC     DI
        JMP     SHORT COMPARE_Z_NEXT
COMPARE_Z_EQUAL:
        XOR     AL,AL
        JMP     SHORT COMPARE_Z_DONE
COMPARE_Z_LESS:
        MOV     AL,1
        JMP     SHORT COMPARE_Z_DONE
COMPARE_Z_MORE:
        MOV     AL,2
COMPARE_Z_DONE:
        POP     DI
        POP     SI
        POP     BX
        RET
COMPARE_Z ENDP

; In: SI = a command spelling.  Out: AX = its topic index, or 0FFFFh.
; The generated alias table is sorted, which the repository gate proves, so one
; binary search resolves any of the 71 spellings.
RESOLVE_ALIAS PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        MOV     TARGET_PTR,SI
        XOR     CX,CX
        MOV     DX,HELP_ALIAS_COUNT - 1
RESOLVE_LOOP:
        CMP     CX,DX
        JBE     SHORT RESOLVE_STEP
        JMP     RESOLVE_NONE
RESOLVE_STEP:
        MOV     AX,CX
        ADD     AX,DX
        SHR     AX,1
        MOV     MID_INDEX,AX
        MOV     BX,AX
        SHL     AX,1
        ADD     AX,BX
        MOV     BX,AX
        MOV     AX,WORD PTR HELP_ALIASES[BX]
        CALL    POOL_STRING
        MOV     DI,TARGET_PTR
        CALL    COMPARE_Z
        OR      AL,AL
        JZ      SHORT RESOLVE_FOUND
        CMP     AL,1
        JNE     SHORT RESOLVE_HIGH
        MOV     AX,MID_INDEX
        INC     AX
        MOV     CX,AX
        JMP     RESOLVE_LOOP
RESOLVE_HIGH:
        MOV     AX,MID_INDEX
        OR      AX,AX
        JZ      SHORT RESOLVE_NONE
        DEC     AX
        MOV     DX,AX
        JMP     RESOLVE_LOOP
RESOLVE_FOUND:
        MOV     BX,MID_INDEX
        MOV     AX,BX
        SHL     AX,1
        ADD     AX,BX
        MOV     BX,AX
        XOR     AX,AX
        MOV     AL,BYTE PTR HELP_ALIASES[BX+2]
        JMP     SHORT RESOLVE_DONE
RESOLVE_NONE:
        MOV     AX,0FFFFH
RESOLVE_DONE:
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        RET
RESOLVE_ALIAS ENDP

; Open the see-also entry the link cursor names.  Clearing the search first
; guarantees the target is in the list rather than filtered out of it.
ACTIVATE_LINK PROC NEAR
        PUSH    AX
        PUSH    CX
        PUSH    SI
        MOV     CL,COUNT_SEE
        OR      CL,CL
        JZ      SHORT ACTIVATE_DONE
        MOV     AX,FIELD_SEE
        CALL    POOL_STRING
        MOV     CL,LINK_INDEX
        XOR     CH,CH
ACTIVATE_WALK:
        JCXZ    ACTIVATE_RESOLVE
        CALL    POOL_NEXT
        DEC     CX
        JMP     SHORT ACTIVATE_WALK
ACTIVATE_RESOLVE:
        CALL    RESOLVE_ALIAS
        CMP     AX,0FFFFH
        JE      SHORT ACTIVATE_DONE
        PUSH    AX
        MOV     BYTE PTR SEARCH_LEN,0
        MOV     BYTE PTR SEARCH_TEXT,0
        CALL    BUILD_FILTER
        POP     AX
        CALL    SELECT_TOPIC
        CALL    ENSURE_VISIBLE
        MOV     BYTE PTR FOCUS,0
        MOV     BYTE PTR LINK_INDEX,0
ACTIVATE_DONE:
        POP     SI
        POP     CX
        POP     AX
        RET
ACTIVATE_LINK ENDP

; Map a click column in DL on the See also row to a link index in AL, or
; 0FFh in a gap or past the last link.  The walk advances through the same
; strings at the same columns DRAW_TEXT_SEE writes, so the hit test and the
; drawn highlight can never disagree.
LINK_FROM_COLUMN PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    SI
        MOV     CL,COUNT_SEE
        XOR     CH,CH
        JCXZ    LINK_FROM_COLUMN_NONE
        MOV     AX,FIELD_SEE
        CALL    POOL_STRING
        MOV     BL,TEXT_LEFT + 10       ; BL = this link's first column
        XOR     BH,BH                   ; BH = this link's index
LINK_FROM_COLUMN_NEXT:
        CMP     DL,BL
        JB      SHORT LINK_FROM_COLUMN_NONE
        CALL    STRING_LENGTH
        ADD     AL,BL
        CMP     DL,AL
        JB      SHORT LINK_FROM_COLUMN_HIT
        ADD     AL,2                    ; the two-space gap after each link
        MOV     BL,AL
        CALL    POOL_NEXT
        INC     BH
        LOOP    LINK_FROM_COLUMN_NEXT
LINK_FROM_COLUMN_NONE:
        MOV     AL,0FFH
        JMP     SHORT LINK_FROM_COLUMN_DONE
LINK_FROM_COLUMN_HIT:
        MOV     AL,BH
LINK_FROM_COLUMN_DONE:
        POP     SI
        POP     CX
        POP     BX
        RET
LINK_FROM_COLUMN ENDP

; Append one typed character to the search, upper-cased.
SEARCH_APPEND PROC NEAR
        PUSH    BX
        MOV     BL,SEARCH_LEN
        CMP     BL,SEARCH_MAX
        JAE     SHORT SEARCH_APPEND_DONE
        CALL    UPPER_AL
        MOV     BL,SEARCH_LEN
        XOR     BH,BH
        MOV     BYTE PTR SEARCH_TEXT[BX],AL
        INC     BX
        MOV     BYTE PTR SEARCH_TEXT[BX],0
        MOV     AL,BL
        MOV     SEARCH_LEN,AL
SEARCH_APPEND_DONE:
        POP     BX
        RET
SEARCH_APPEND ENDP

SEARCH_BACKSPACE PROC NEAR
        PUSH    BX
        MOV     BL,SEARCH_LEN
        OR      BL,BL
        JZ      SHORT SEARCH_BACKSPACE_DONE
        DEC     BL
        MOV     SEARCH_LEN,BL
        XOR     BH,BH
        MOV     BYTE PTR SEARCH_TEXT[BX],0
SEARCH_BACKSPACE_DONE:
        POP     BX
        RET
SEARCH_BACKSPACE ENDP

; ---------------------------------------------------------------------------
; Topic record access.  In: AX = topic index.  Out: BX = record offset.
; ---------------------------------------------------------------------------
TOPIC_RECORD PROC NEAR
        PUSH    DX
        MOV     DX,HELP_TOPIC_SIZE
        MUL     DX
        MOV     BX,AX
        POP     DX
        RET
TOPIC_RECORD ENDP

; In: AX = pool offset.  Out: SI = address of the zero-terminated string.
POOL_STRING PROC NEAR
        MOV     SI,OFFSET HELP_POOL
        ADD     SI,AX
        RET
POOL_STRING ENDP

; In: SI = a zero-terminated string.  Out: SI = the string after it.
POOL_NEXT PROC NEAR
POOL_NEXT_SCAN:
        CMP     BYTE PTR [SI],0
        JE      SHORT POOL_NEXT_DONE
        INC     SI
        JMP     SHORT POOL_NEXT_SCAN
POOL_NEXT_DONE:
        INC     SI
        RET
POOL_NEXT ENDP

; ---------------------------------------------------------------------------
; Drawing.
;
; There is no full-screen clear here.  Wiping all 2,000 cells and then refilling
; them is what made a redraw visibly blank the page, and the refill is slow
; enough on an emulated 8086 that the blank is on screen for a quarter of a
; second.  Instead every cell of the 80x25 screen belongs to exactly one
; painter, which is responsible for writing it on every redraw:
;
;   row 0                     DRAW_MENU_BAR   full width
;   row 1                     DRAW_SEARCH_ROW full width
;   row 2                     DRAW_FRAME      full width
;   rows 3..22 cols 0..21     DRAW_LIST       every row, entry or not
;   rows 3..22 col 22         DRAW_FRAME      the vertical rule
;   rows 3..22 cols 23..79    DRAW_TEXT       via CLEAR_TEXT_PANE
;   row 23                    DRAW_FRAME      full width
;   row 24                    DRAW_STATUS     full width
;
; Leave a gap in that table and a glyph from a longer topic, a wider list or a
; dismissed dialog survives the redraw that should have replaced it.
; ---------------------------------------------------------------------------
DRAW_SCREEN PROC NEAR
        CALL    DOSUI_HIDE_CURSOR
        CALL    DRAW_MENU_BAR
        CALL    DRAW_SEARCH_ROW
        CALL    DRAW_FRAME
        CALL    DRAW_LIST
        CALL    DRAW_TEXT
        CALL    DRAW_STATUS
        RET
DRAW_SCREEN ENDP

DRAW_MENU_BAR PROC NEAR
        MOV     AL,' '
        MOV     BL,ATTR_BAR
        MOV     DH,MENU_ROW
        XOR     DL,DL
        MOV     CX,DOSUI_SCREEN_COLUMNS
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET MENU_BAR_TEXT
        MOV     BL,ATTR_BAR
        MOV     DH,MENU_ROW
        MOV     DL,2
        CALL    DOSUI_WRITE_Z
        ; Reverse the header whose drop-down is open.
        CMP     BYTE PTR MENU_OPEN,1
        JNE     SHORT DRAW_MENU_BAR_HELP
        MOV     SI,OFFSET MENU_FILE_TEXT
        MOV     BL,ATTR_NORMAL
        MOV     DL,FILE_MENU_LEFT
        CALL    DOSUI_WRITE_Z
        RET
DRAW_MENU_BAR_HELP:
        CMP     BYTE PTR MENU_OPEN,2
        JNE     SHORT DRAW_MENU_BAR_DONE
        MOV     SI,OFFSET MENU_HELP_TEXT
        MOV     BL,ATTR_NORMAL
        MOV     DL,HELP_MENU_LEFT
        CALL    DOSUI_WRITE_Z
DRAW_MENU_BAR_DONE:
        RET
DRAW_MENU_BAR ENDP

DRAW_SEARCH_ROW PROC NEAR
        ; This row is this painter's whole width, not just the label and the
        ; field: an open drop-down covers it, and nothing clears it first.
        MOV     AL,' '
        MOV     BL,ATTR_NORMAL
        MOV     DH,SEARCH_ROW
        XOR     DL,DL
        MOV     CX,DOSUI_SCREEN_COLUMNS
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET SEARCH_LABEL
        MOV     BL,ATTR_NORMAL
        MOV     DH,SEARCH_ROW
        MOV     DL,LIST_LEFT
        CALL    DOSUI_WRITE_Z
        ; The field itself is highlighted while it has focus.
        MOV     BL,ATTR_NORMAL
        CMP     BYTE PTR FOCUS,1
        JNE     SHORT DRAW_SEARCH_ATTR
        MOV     BL,ATTR_SELECTED
DRAW_SEARCH_ATTR:
        PUSH    BX
        MOV     AL,' '
        MOV     DL,LIST_LEFT + 8
        MOV     CX,SEARCH_MAX + 1
        CALL    DOSUI_FILL_ROW
        POP     BX
        MOV     SI,OFFSET SEARCH_TEXT
        MOV     DL,LIST_LEFT + 8
        CALL    DOSUI_WRITE_Z
        RET
DRAW_SEARCH_ROW ENDP

DRAW_FRAME PROC NEAR
        MOV     AL,0C4H
        MOV     BL,ATTR_FRAME
        MOV     DH,FRAME_ROW
        XOR     DL,DL
        MOV     CX,DOSUI_SCREEN_COLUMNS
        CALL    DOSUI_FILL_ROW
        MOV     DH,LIST_TOP
DRAW_FRAME_COLUMN:
        CMP     DH,LIST_BOTTOM
        JA      SHORT DRAW_FRAME_DONE
        MOV     AL,0B3H
        MOV     BL,ATTR_FRAME
        MOV     DL,TEXT_LEFT - 1
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        INC     DH
        JMP     SHORT DRAW_FRAME_COLUMN
DRAW_FRAME_DONE:
        ; The blank row under both panes belongs to no content painter, so the
        ; chrome owns it.  Without this the last row of a long topic would stay
        ; on screen after a shorter one replaced it.
        MOV     AL,' '
        MOV     BL,ATTR_NORMAL
        MOV     DH,GAP_ROW
        XOR     DL,DL
        MOV     CX,DOSUI_SCREEN_COLUMNS
        CALL    DOSUI_FILL_ROW
        RET
DRAW_FRAME ENDP

DRAW_LIST PROC NEAR
        PUSH    BP
        MOV     BP,LIST_FIRST
        MOV     DH,LIST_TOP
DRAW_LIST_ROW:
        CMP     DH,LIST_BOTTOM
        JA      SHORT DRAW_LIST_DONE
        ; Columns 0..LIST_WIDTH+1 belong to this painter on every row it covers,
        ; whether the filter still has an entry for that row or not.  A search
        ; that narrows the list vacates rows, and nothing else will blank them.
        MOV     AL,' '
        MOV     BL,ATTR_NORMAL
        XOR     DL,DL
        MOV     CX,LIST_WIDTH + 2
        CALL    DOSUI_FILL_ROW
        MOV     AX,BP
        CMP     AX,FILTER_COUNT
        JAE     SHORT DRAW_LIST_NEXT
        CMP     AX,SELECTED_POS
        JNE     SHORT DRAW_LIST_ATTR_READY
        ; The highlight covers the entry columns only, which is exactly the span
        ; the blanking fill above just left at ATTR_NORMAL.
        MOV     AL,' '
        MOV     BL,ATTR_SELECTED
        MOV     DL,LIST_LEFT
        MOV     CX,LIST_WIDTH
        CALL    DOSUI_FILL_ROW
DRAW_LIST_ATTR_READY:
        MOV     BX,BP
        SHL     BX,1
        MOV     AX,WORD PTR FILTER_LIST[BX]
        CALL    TOPIC_RECORD
        MOV     AX,WORD PTR HELP_TOPICS[BX]
        CALL    POOL_STRING
        MOV     BL,ATTR_NORMAL
        MOV     AX,BP
        CMP     AX,SELECTED_POS
        JNE     SHORT DRAW_LIST_TEXT_ATTR
        MOV     BL,ATTR_SELECTED
DRAW_LIST_TEXT_ATTR:
        MOV     DL,LIST_LEFT + 1
        CALL    DOSUI_WRITE_Z
DRAW_LIST_NEXT:
        INC     BP
        INC     DH
        JMP     SHORT DRAW_LIST_ROW
DRAW_LIST_DONE:
        POP     BP
        RET
DRAW_LIST ENDP

; Blank the text pane: rows LIST_TOP..LIST_BOTTOM, columns TEXT_LEFT..79.
; Re-laying the body is what makes a redraw expensive, so the pane is cleared
; once here rather than padded line by line; the alternative needs every branch
; of DRAW_TEXT to account for the rows it stepped over, and one that forgets
; leaves a line of the previous topic behind.
CLEAR_TEXT_PANE PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     DH,LIST_TOP
CLEAR_TEXT_PANE_ROW:
        CMP     DH,LIST_BOTTOM
        JA      SHORT CLEAR_TEXT_PANE_DONE
        MOV     AL,' '
        MOV     BL,ATTR_NORMAL
        MOV     DL,TEXT_LEFT
        MOV     CX,DOSUI_SCREEN_COLUMNS - TEXT_LEFT
        CALL    DOSUI_FILL_ROW
        INC     DH
        JMP     SHORT CLEAR_TEXT_PANE_ROW
CLEAR_TEXT_PANE_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
CLEAR_TEXT_PANE ENDP

; Repaint the reading pane.  Laying out a topic body is 80% of a redraw, and
; most keys do not change which topic is open: Tab, the link cursor, Enter on
; the list, and any arrow that the selection was already clamped against all
; leave the same body on screen.  So the body is drawn only when the topic
; actually changed, and the See also row -- whose highlight follows the link
; cursor -- is repainted on its own the rest of the time.
;
; TEXT_TOPIC identifies what is on screen: a topic index, 0FFFFh for the empty
; filter, or 0FFFEh for "nothing is trusted here".  Anything that paints over
; the pane must store 0FFFEh, or the next redraw will leave it there.
DRAW_TEXT PROC NEAR
        MOV     AX,0FFFFH
        CMP     WORD PTR FILTER_COUNT,0
        JE      SHORT DRAW_TEXT_KEY
        CALL    CURRENT_TOPIC
DRAW_TEXT_KEY:
        CMP     AX,TEXT_TOPIC
        JNE     SHORT DRAW_TEXT_BODY
        JMP     DRAW_TEXT_LINKS
DRAW_TEXT_BODY:
        MOV     TEXT_TOPIC,AX
        MOV     BYTE PTR TEXT_SEE_ROW,0FFH
        CALL    CLEAR_TEXT_PANE
        CMP     AX,0FFFFH
        JNE     SHORT DRAW_TEXT_HAVE
        MOV     SI,OFFSET NO_MATCH_TEXT
        MOV     BL,ATTR_NORMAL
        MOV     DH,LIST_TOP
        MOV     DL,TEXT_LEFT
        CALL    DOSUI_WRITE_Z
        MOV     BYTE PTR COUNT_SEE,0
        RET
DRAW_TEXT_HAVE:
        ; Cache the selected record once. HELP_TOPICS is a byte label, so every
        ; field needs an explicit size, and BP-relative indexing would reach
        ; through SS rather than DS.  AX still holds the topic index.
        CALL    TOPIC_RECORD
        MOV     AX,WORD PTR HELP_TOPICS[BX]
        MOV     FIELD_ID,AX
        MOV     AX,WORD PTR HELP_TOPICS[BX+2]
        MOV     FIELD_TITLE,AX
        MOV     AX,WORD PTR HELP_TOPICS[BX+4]
        MOV     FIELD_SUMMARY,AX
        MOV     AX,WORD PTR HELP_TOPICS[BX+6]
        MOV     FIELD_SYNTAX,AX
        MOV     AX,WORD PTR HELP_TOPICS[BX+8]
        MOV     FIELD_DETAILS,AX
        MOV     AX,WORD PTR HELP_TOPICS[BX+10]
        MOV     FIELD_SEE,AX
        MOV     AL,BYTE PTR HELP_TOPICS[BX+12]
        MOV     COUNT_SYNTAX,AL
        MOV     AL,BYTE PTR HELP_TOPICS[BX+13]
        MOV     COUNT_DETAILS,AL
        MOV     AL,BYTE PTR HELP_TOPICS[BX+14]
        MOV     COUNT_SEE,AL

        MOV     DH,LIST_TOP
        MOV     AX,FIELD_ID
        CALL    POOL_STRING
        MOV     BL,ATTR_HEADING
        MOV     DL,TEXT_LEFT
        CALL    DOSUI_WRITE_Z
        MOV     AX,FIELD_TITLE
        CALL    POOL_STRING
        MOV     BL,ATTR_HEADING
        MOV     DL,TEXT_LEFT + 10
        CALL    DOSUI_WRITE_Z
        INC     DH
        INC     DH

        MOV     AX,FIELD_SUMMARY
        CALL    POOL_STRING
        MOV     CL,TEXT_WIDTH
        CALL    WRAP_WRITE
        INC     DH

        MOV     AX,FIELD_SYNTAX
        CALL    POOL_STRING
        MOV     CL,COUNT_SYNTAX
        XOR     CH,CH
DRAW_TEXT_SYNTAX:
        JCXZ    DRAW_TEXT_SYNTAX_DONE
        CMP     DH,LIST_BOTTOM
        JA      SHORT DRAW_TEXT_SYNTAX_DONE
        PUSH    CX
        MOV     BL,ATTR_NORMAL
        MOV     DL,TEXT_LEFT
        CALL    DOSUI_WRITE_Z
        POP     CX
        CALL    POOL_NEXT
        INC     DH
        DEC     CX
        JMP     SHORT DRAW_TEXT_SYNTAX
DRAW_TEXT_SYNTAX_DONE:
        INC     DH

        MOV     AX,FIELD_DETAILS
        CALL    POOL_STRING
        MOV     CL,COUNT_DETAILS
        XOR     CH,CH
DRAW_TEXT_DETAIL:
        JCXZ    DRAW_TEXT_DETAIL_DONE
        CMP     DH,LIST_BOTTOM
        JA      SHORT DRAW_TEXT_DETAIL_DONE
        PUSH    CX
        MOV     CL,TEXT_WIDTH
        CALL    WRAP_WRITE
        POP     CX
        CALL    POOL_NEXT
        INC     DH
        DEC     CX
        JMP     SHORT DRAW_TEXT_DETAIL
DRAW_TEXT_DETAIL_DONE:

        CMP     DH,LIST_BOTTOM
        JA      SHORT DRAW_TEXT_DONE
        MOV     TEXT_SEE_ROW,DH
DRAW_TEXT_LINKS:
        ; Reached either straight from the body above or as the whole of a
        ; redraw whose topic did not change.  Both cases write the same strings
        ; at the same columns -- they are a function of the topic alone -- so
        ; repainting covers exactly the cells the body pass wrote, and only the
        ; cursor's attribute differs.
        MOV     DH,TEXT_SEE_ROW
        CMP     DH,0FFH
        JE      SHORT DRAW_TEXT_DONE
        MOV     SI,OFFSET SEE_ALSO_LABEL
        MOV     BL,ATTR_NORMAL
        MOV     DL,TEXT_LEFT
        CALL    DOSUI_WRITE_Z
        MOV     DL,TEXT_LEFT + 10
        MOV     AX,FIELD_SEE
        CALL    POOL_STRING
        MOV     CL,COUNT_SEE
        XOR     CH,CH
DRAW_TEXT_SEE:
        JCXZ    DRAW_TEXT_DONE
        PUSH    CX
        MOV     BL,ATTR_NORMAL
        CMP     BYTE PTR FOCUS,2
        JNE     SHORT DRAW_TEXT_SEE_ATTR
        MOV     AL,COUNT_SEE
        SUB     AL,CL
        CMP     AL,LINK_INDEX
        JNE     SHORT DRAW_TEXT_SEE_ATTR
        MOV     BL,ATTR_SELECTED
DRAW_TEXT_SEE_ATTR:
        CALL    DOSUI_WRITE_Z
        CALL    STRING_LENGTH
        ADD     DL,AL
        ADD     DL,2
        POP     CX
        CALL    POOL_NEXT
        DEC     CX
        JMP     SHORT DRAW_TEXT_SEE
DRAW_TEXT_DONE:
        RET
DRAW_TEXT ENDP

DRAW_STATUS PROC NEAR
        MOV     AL,' '
        MOV     BL,ATTR_STATUS
        MOV     DH,STATUS_ROW
        XOR     DL,DL
        MOV     CX,DOSUI_SCREEN_COLUMNS
        CALL    DOSUI_FILL_ROW
        MOV     BL,ATTR_STATUS
        MOV     DH,STATUS_ROW
        MOV     DL,1
        CMP     BYTE PTR NOTICE_ACTIVE,0
        JNE     SHORT DRAW_STATUS_NOTICE
        MOV     SI,OFFSET STATUS_TEXT
        CALL    DOSUI_WRITE_Z
        RET
DRAW_STATUS_NOTICE:
        ; The name is reported where the key hints normally sit, so a request
        ; that could not be honoured is visible rather than silently dropped.
        MOV     SI,OFFSET NOTICE_PREFIX
        CALL    DOSUI_WRITE_Z
        CALL    STRING_LENGTH
        ADD     DL,AL
        MOV     SI,OFFSET ARG_TEXT
        CALL    DOSUI_WRITE_Z
        CALL    STRING_LENGTH
        ADD     DL,AL
        MOV     SI,OFFSET NOTICE_SUFFIX
        CALL    DOSUI_WRITE_Z
        RET
DRAW_STATUS ENDP

; In: SI = zero-terminated string.  Out: AL = its length, capped at 255.
STRING_LENGTH PROC NEAR
        PUSH    SI
        XOR     AL,AL
STRING_LENGTH_SCAN:
        CMP     BYTE PTR [SI],0
        JE      SHORT STRING_LENGTH_DONE
        INC     AL
        JZ      SHORT STRING_LENGTH_DONE
        INC     SI
        JMP     SHORT STRING_LENGTH_SCAN
STRING_LENGTH_DONE:
        POP     SI
        RET
STRING_LENGTH ENDP

; Write one paragraph, breaking at spaces.  In: SI = text, CL = width,
; DH = first row.  Out: DH = the row after the last one written.  Stops at
; LIST_BOTTOM so a long topic cannot run into the status line.
WRAP_WRITE PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        MOV     CH,CL                   ; CH = wrap width
WRAP_LINE:
        CMP     BYTE PTR [SI],0
        JE      SHORT WRAP_DONE
        CMP     DH,LIST_BOTTOM
        JA      SHORT WRAP_DONE
        ; Find how many bytes fit on this line.
        XOR     CL,CL                   ; CL = bytes taken
        XOR     BL,BL                   ; BL = last space seen, 0 = none
        MOV     DI,SI
WRAP_MEASURE:
        MOV     AL,[DI]
        OR      AL,AL
        JZ      SHORT WRAP_EMIT
        CMP     CL,CH
        JAE     SHORT WRAP_BREAK
        CMP     AL,' '
        JNE     SHORT WRAP_MEASURE_NEXT
        MOV     BL,CL
WRAP_MEASURE_NEXT:
        INC     DI
        INC     CL
        JMP     SHORT WRAP_MEASURE
WRAP_BREAK:
        ; Full line: break at the last space when there is one.
        OR      BL,BL
        JZ      SHORT WRAP_EMIT
        MOV     CL,BL
WRAP_EMIT:
        CALL    WRAP_EMIT_RUN
        ; Advance past what was written, then past one space if we broke there.
        XOR     AH,AH
        MOV     AL,CL
        ADD     SI,AX
        CMP     BYTE PTR [SI],' '
        JNE     SHORT WRAP_EMIT_NEXT
        INC     SI
WRAP_EMIT_NEXT:
        INC     DH
        JMP     SHORT WRAP_LINE
WRAP_DONE:
        POP     DI
        POP     SI
        MOV     AX,DX                   ; keep the row we reached
        POP     DX
        MOV     DH,AH
        POP     CX
        POP     BX
        POP     AX
        RET
WRAP_WRITE ENDP

; Write CL bytes from DS:SI at row DH, column TEXT_LEFT.
;
; The cells go straight into the mode-3 plane rather than through one
; DOSUI_FILL_ROW call per byte.  That call re-validates the row, re-derives the
; cell address and saves and restores nine registers to write a single
; character, which cost about fifty instructions each; over the six hundred odd
; characters of a wrapped topic body it was the great majority of everything
; HELP executed between two keystrokes.  The row and column clipping the shared
; primitive applied is kept, so out-of-range requests are still ignored rather
; than writing outside the row.
WRAP_EMIT_RUN PROC NEAR
        PUSHF
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        XOR     CH,CH
        JCXZ    WRAP_EMIT_RUN_DONE
        CMP     DH,DOSUI_SCREEN_ROWS
        JAE     SHORT WRAP_EMIT_RUN_DONE
        MOV     BX,DOSUI_SCREEN_COLUMNS - TEXT_LEFT
        CMP     CX,BX
        JBE     SHORT WRAP_EMIT_RUN_FITS
        MOV     CX,BX
WRAP_EMIT_RUN_FITS:
        XOR     AX,AX
        MOV     AL,DH
        MOV     BL,ROW_BYTES
        MUL     BL
        MOV     DI,AX
        ADD     DI,TEXT_LEFT * 2
        MOV     AX,TEXT_SEGMENT
        MOV     ES,AX
        ; LODSB loads AL and leaves AH alone, so the attribute is set once and
        ; each cell costs a load, a store and the loop.
        MOV     AH,ATTR_NORMAL
        CLD
WRAP_EMIT_RUN_NEXT:
        LODSB
        STOSW
        LOOP    WRAP_EMIT_RUN_NEXT
WRAP_EMIT_RUN_DONE:
        POP     ES
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        POPF
        RET
WRAP_EMIT_RUN ENDP

; ---------------------------------------------------------------------------
; State and strings.  Every user-visible word about a command comes from the
; corpus; only the chrome below is written here.
; ---------------------------------------------------------------------------
SELECTED_POS    DW 0
LIST_FIRST      DW 0
FOCUS           DB 0                    ; 0 list, 1 search, 2 links
LINK_INDEX      DB 0
SEARCH_LEN      DB 0
SEARCH_TEXT     DB (SEARCH_MAX + 1) DUP (0)
ARG_LEN         DB 0                    ; the command-tail operand, upper-cased
ARG_TEXT        DB (ARG_MAX + 1) DUP (0)
NOTICE_ACTIVE   DB 0                    ; the operand named no topic
TARGET_PTR      DW 0
MID_INDEX       DW 0
FILTER_COUNT    DW 0
FILTER_LIST     DW HELP_TOPIC_COUNT DUP (0)

FIELD_ID        DW 0
FIELD_TITLE     DW 0
FIELD_SUMMARY   DW 0
FIELD_SYNTAX    DW 0
FIELD_DETAILS   DW 0
FIELD_SEE       DW 0
COUNT_SYNTAX    DB 0
COUNT_DETAILS   DB 0
COUNT_SEE       DB 0

; What the reading pane currently shows.  0FFFFh is the empty-filter state and
; 0FFFEh means nothing there can be trusted; neither is a reachable topic index
; because there are only HELP_TOPIC_COUNT topics.  The initial 0FFFEh is what
; makes the first redraw lay out a body.
TEXT_TOPIC      DW 0FFFEH
TEXT_SEE_ROW    DB 0FFH                 ; row of the See also line, 0FFh none

MENU_OPEN       DB 0                    ; 0 none, 1 File, 2 Help

; TOP, LEFT, BOTTOM, RIGHT, frame attribute, fill attribute.
FILE_MENU_RECT  DB 1,FILE_MENU_LEFT,3,FILE_MENU_LEFT + MENU_DROP_WIDTH + 1
                DB ATTR_MENU_FRAME,ATTR_MENU_FILL
HELP_MENU_RECT  DB 1,HELP_MENU_LEFT,3,HELP_MENU_LEFT + MENU_DROP_WIDTH + 1
                DB ATTR_MENU_FRAME,ATTR_MENU_FILL
ABOUT_RECT      DB ABOUT_TOP,ABOUT_LEFT,ABOUT_TOP + 8,ABOUT_LEFT + 39
                DB ATTR_MENU_FRAME,ATTR_MENU_FILL

FILE_EXIT_TEXT  DB 'Exit',0
HELP_ABOUT_TEXT DB 'About',0
ABOUT_LINE1     DB 'CK-DOS Help',0
ABOUT_LINE2     DB 'Command topics for this build.',0
ABOUT_LINE3     DB 'Press any key to continue.',0

MENU_BAR_TEXT   DB 'File  Help',0
MENU_FILE_TEXT  DB 'File',0
MENU_HELP_TEXT  DB 'Help',0
SEARCH_LABEL    DB 'Search:',0
SEE_ALSO_LABEL  DB 'See also:',0
STATUS_TEXT     DB 'Alt+F/Alt+H menus  Tab moves  Enter opens  Esc clears or exits',0
NO_MATCH_TEXT   DB 'No topic matches that search.',0
NOTICE_PREFIX   DB 'No help topic named ',0
NOTICE_SUFFIX   DB '.  Tab to search, Esc exits.',0

DOSUI_TEXT ENDS
END START
