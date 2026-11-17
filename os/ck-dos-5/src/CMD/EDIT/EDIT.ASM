; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; CK-DOS EDIT — a clean-room, native 16-bit DOS text editor.
;
; This first DOSUI-1 application is deliberately an ordinary COM program. It
; uses DOSUI-0 for screen, key, and mouse ownership and DOS INT 21h for all
; file I/O.  With an INT 33h driver present (CK-DOS's built-in pointer
; service or a guest driver on the PS/2 profile), every menu, drop-down
; item, dialog button, status-bar shortcut, scroll rail, and the text
; viewport answer left clicks; without one, behavior is exactly the
; keyboard-only editor.

.8086
DOSUI_TEXT SEGMENT PUBLIC 'CODE'
ASSUME CS:DOSUI_TEXT, DS:DOSUI_TEXT, ES:DOSUI_TEXT
ORG 100H

INCLUDE DOSUI.INC
EXTRN EDITHLP_START:BYTE                ; EDITHLP.ASM, linked after DOSUI
EXTRN EDITHLP_END:BYTE

LINE_COLUMN_MAX EQU 255
OPEN_FILE_MAX   EQU 120
OPEN_DIR_MAX    EQU 64
LEFT_COLUMN_MAX EQU 178
INI_HELP_PATH_MAX EQU 128
HELP_PATH_FIELD_WIDTH EQU 49
INI_IMAGE_MAX   EQU 47+INI_HELP_PATH_MAX
; The view of the text (see "The document's memory") is a 64 KB DOS memory
; block of its own, which is also the stack's segment: SS addresses it, so
; BUFFER[BX] reads the view with an SS override while DS stays on the
; program's data, and the few string moves through the view (VIEW_SEGS)
; point DS and ES at SS around the move. The stack lives above the view.
; The text itself and the clipboard live in one DOS memory block as large
; as DOS has (ARENA_SEG). START shrinks the program to its image (code,
; data and the help topics, which stay where EDITHLP.ASM put them), takes
; the view's block and moves the stack there, keeps the screen EDIT started
; on in a block of its own (DOSUI_SAVE_SEGMENT, left out when memory is
; short), then takes the largest block left for the text, and fails with
; "Insufficient memory" when that is under MIN_ARENA_PARAS.
BUFFER_BASE     EQU 0
BUFFER          EQU BYTE PTR SS:[BUFFER_BASE]
STACK_FLOOR     EQU 0FE00H
VIEW_PARAS      EQU 1000H           ; the view's block: 64 KB, the stack's top
BUFFER_MAX      EQU STACK_FLOOR-BUFFER_BASE     ; the view's size
VIEW_RESERVE    EQU 2048            ; kept free in the view between commands
LINE_LIMIT      EQU 32768           ; the longest line a file may bring, its CR included

; DS = ES = the view's segment around a string move through the view;
; CODE_SEGS puts both back.
VIEW_SEGS MACRO
        PUSH    DS
        PUSH    ES
        PUSH    SS
        POP     DS
        PUSH    SS
        POP     ES
ENDM
CODE_SEGS MACRO
        POP     ES
        POP     DS
ENDM
MIN_ARENA_PARAS EQU 800H            ; 32 KB at least for the text
TEXT_FIRST_ROW  EQU 2
ERROR_DISK_FULL EQU 0FF01H              ; FILE_ERROR codes of the editor's own
ERROR_BAD_NAME  EQU 0FF02H
ERROR_MEMORY    EQU 0FF03H
HELP_TOPIC_STARTED  EQU 1
HELP_TOPIC_MENUS    EQU 3
HELP_TOPIC_DIALOGS  EQU 4
HELP_TOPIC_KEYBOARD EQU 5
HELP_TOPIC_COUNT    EQU 9
HELP_LINK_BACK      EQU 0FEH
HELP_LINK_MAX       EQU 12
HELP_HISTORY_MAX    EQU 16
HELP_EXIT_REDRAW    EQU 0           ; HELP_VIEW's answers to EDIT_LOOP
HELP_EXIT_MOUSE     EQU 1
HELP_EXIT_KEY       EQU 2
HELP_STATE_BYTES    EQU 8+HELP_LINK_MAX*4   ; HELP_TOPIC .. HELP_LINKS
DIALOG_HELP_ROWS_MAX EQU 14

MENU_FILE       EQU 0
MENU_EDIT       EQU 1
MENU_SEARCH     EQU 2
MENU_OPTIONS    EQU 3
MENU_HELP       EQU 4

COMMAND_NONE        EQU 0
COMMAND_SAVE        EQU 1
COMMAND_EXIT        EQU 2
COMMAND_MARK        EQU 3
COMMAND_COPY        EQU 4
COMMAND_CUT         EQU 5
COMMAND_PASTE       EQU 6
COMMAND_FIND        EQU 7
COMMAND_FIND_NEXT   EQU 8
COMMAND_REPLACE     EQU 9
COMMAND_DISPLAY     EQU 10
COMMAND_HELP_PATH   EQU 11
COMMAND_GETTING_STARTED EQU 12
COMMAND_KEYBOARD    EQU 13
COMMAND_ABOUT       EQU 14
COMMAND_SAVE_AS     EQU 15
COMMAND_CLEAR       EQU 16
COMMAND_NEW         EQU 17
COMMAND_OPEN        EQU 18
COMMAND_PRINT       EQU 19

START:
        PUSH    CS
        POP     DS
        PUSH    CS
        POP     ES
        CLD
        MOV     BX,OFFSET EDITHLP_END   ; the image: code, data, help topics
        ADD     BX,15
        MOV     CL,4
        SHR     BX,CL
        MOV     AX,BX                   ; and room below the loader's stack
        INC     AX
        SHL     AX,CL
        CMP     SP,AX
        JB      SHORT START_NO_MEMORY
        MOV     AH,4AH
        INT     21H
        JC      SHORT START_NO_MEMORY
        MOV     BX,VIEW_PARAS           ; the view's block, the stack on top
        MOV     AH,48H
        INT     21H
        JC      SHORT START_NO_MEMORY
        CLI
        MOV     SS,AX
        XOR     SP,SP
        STI
        MOV     AX,OFFSET EDITHLP_START ; the help topics stay in the image,
        MOV     CL,4                    ; on a paragraph of their own
        SHR     AX,CL                   ; (EDITHLP.ASM)
        MOV     BX,CS
        ADD     AX,BX
        MOV     HELP_SEGMENT,AX
        MOV     BX,DOSUI_SAVE_PARAGRAPHS ; caller screen + offscreen backbuffer
        MOV     AH,48H
        INT     21H
        JNC     SHORT START_HAVE_SAVE_SEG
        MOV     BYTE PTR DOSUI_HAS_BACKBUFFER,0
        MOV     BX,DOSUI_TEXT_SAVE_PARAGRAPHS
        MOV     AH,48H
        INT     21H
        JC      SHORT START_ARENA
START_HAVE_SAVE_SEG:
        MOV     DOSUI_SAVE_SEGMENT,AX
START_ARENA:
        MOV     BX,0FFFFH               ; the largest block left: the text's
        MOV     AH,48H
        INT     21H
        CMP     BX,MIN_ARENA_PARAS
        JB      SHORT START_NO_MEMORY
        MOV     AH,48H
        INT     21H
        JC      SHORT START_NO_MEMORY
        MOV     ARENA_SEG,AX
        MOV     AX,BX
        MOV     CL,4
        SHL     AX,CL
        MOV     ARENA_SIZE,AX
        MOV     CL,12
        SHR     BX,CL
        MOV     ARENA_SIZE+2,BX
        JMP     SHORT START_MEMORY_OK
START_NO_MEMORY:
        MOV     DX,OFFSET NO_MEMORY_TEXT
        MOV     AH,09H
        INT     21H
        MOV     AX,4C08H
        INT     21H
START_MEMORY_OK:
        MOV     DX,OFFSET CRITICAL_ERROR_HANDLER
        MOV     AX,2524H                ; DOS puts it back at exit
        INT     21H
        MOV     WORD PTR DOSUI_MENU_MASK_VECTOR,OFFSET GET_ACTIVE_MENU_MASK
        CALL    PARSE_COMMAND_LINE
        JNC     EDIT_START
        MOV     DX,OFFSET HELP_DOS
        MOV     AH,09H
        INT     21H
        MOV     AX,4C00H
        INT     21H

EDIT_START:
        CALL    COMMIT_DISPLAY_NAME
        CALL    SETUP_DISPLAY_MODE
        CALL    INI_LOAD
        CALL    LOAD_FILE
        CMP     BYTE PTR STARTUP_ERROR,0
        JE      SHORT EDIT_START_LOADED
        JMP     STARTUP_ABORT
EDIT_START_LOADED:
        CALL    DOSUI_INIT
        CALL    SETUP_ROWS
        CALL    DOSUI_MOUSE_INIT
        CALL    SETUP_MOUSE_ROWS
        MOV     BYTE PTR DOSUI_ALT_TAP,1        ; "Press ALT to activate menus"
        CALL    DRAW_SCREEN
        CMP     BYTE PTR FILE_NAME,0
        JNE     EDIT_LOOP
        CALL    SHOW_WELCOME
        CALL    DRAW_SCREEN

EDIT_LOOP:
        CALL    NORMALIZE_VIEW          ; the view ready for the next command
        CMP     BYTE PTR HELP_ACTIVE,0  ; Help the active window: its keys
        JE      SHORT EDIT_LOOP_EDITOR
        MOV     BYTE PTR MOUSE_REPEAT,0
        CALL    HELP_VIEW
        CMP     AL,HELP_EXIT_MOUSE
        JNE     SHORT EDIT_LOOP_HELP_KEY
        JMP     EDIT_MOUSE
EDIT_LOOP_HELP_KEY:
        CMP     AL,HELP_EXIT_KEY
        JNE     SHORT EDIT_LOOP_HELP_DONE
        MOV     AX,HELP_KEY_STASH
        JMP     SHORT EDIT_LOOP_KEY
EDIT_LOOP_HELP_DONE:
        JMP     EDIT_REDRAW
EDIT_LOOP_EDITOR:
        CMP     BYTE PTR MOUSE_REPEAT,0 ; a scroll bar press held down
        JE      SHORT EDIT_LOOP_READ
        CALL    MOUSE_REPEAT_WAIT
        JNC     SHORT EDIT_LOOP_READ
        MOV     BYTE PTR MOUSE_REPEATING,1
        JMP     EDIT_MOUSE
EDIT_LOOP_READ:
        XOR     AL,AL                   ; the editor's own rows
        CALL    SET_ROW_OFFSET
        CALL    DOSUI_READ_EVENT
        JNC     SHORT EDIT_LOOP_KEY
        JMP     EDIT_MOUSE
EDIT_LOOP_KEY:
        PUSH    AX
        MOV     AH,02H                  ; modifier snapshot for this BIOS key
        INT     16H
        MOV     MODIFIER_STATE,AL
        POP     AX
        CMP     AL,0
        JNE     SHORT EDIT_ASCII
        JMP     EDIT_EXTENDED
EDIT_ASCII:
        CMP     AL,32                   ; control keys: the DOS 5 (WordStar) set
        JAE     SHORT EDIT_PRINTABLE
        XOR     BX,BX
        MOV     BL,AL
        SHL     BX,1
        JMP     WORD PTR CONTROL_KEY_TABLE[BX]
EDIT_PRINTABLE:
        CALL    TYPE_CHARACTER
        JMP     EDIT_REDRAW

; Insert or overwrite one typed character AL at the cursor.
TYPE_CHARACTER PROC NEAR
        CALL    DELETE_SELECTION        ; typed text replaces a selection
        PUSH    CX
        CALL    VIRTUAL_GAP
        INC     CX
        CALL    ROOM_FOR                ; "Out of memory": nothing typed
        POP     CX
        JC      TYPE_CHARACTER_DONE
        CALL    MATERIALIZE_VIRTUAL_COLUMN
        CALL    INSERT_BYTE
        CALL    RESET_NAVIGATION_COLUMNS
TYPE_CHARACTER_DONE:
        RET
TYPE_CHARACTER ENDP

; CX = the blanks MATERIALIZE_VIRTUAL_COLUMN would insert.
VIRTUAL_GAP PROC NEAR
        XOR     CX,CX
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      VIRTUAL_GAP_DONE
        PUSH    AX
        PUSH    SI
        CALL    CALCULATE_POSITION
        MOV     CX,VIRTUAL_COLUMN
        SUB     CX,CURRENT_COLUMN
        JNB     VIRTUAL_GAP_KNOWN
        XOR     CX,CX
VIRTUAL_GAP_KNOWN:
        POP     SI
        POP     AX
VIRTUAL_GAP_DONE:
        RET
VIRTUAL_GAP ENDP

; The key after a Ctrl+K or Ctrl+Q prefix, as an upper-case letter or digit
; in AL (Ctrl+letter counts as the letter). Carry: a click or another key.
READ_PREFIX_KEY PROC NEAR
        CALL    DOSUI_READ_EVENT
        JC      SHORT READ_PREFIX_KEY_DONE
        OR      AL,AL
        JZ      SHORT READ_PREFIX_KEY_NONE
        CMP     AL,26
        JA      SHORT READ_PREFIX_KEY_CHARACTER
        ADD     AL,'@'
READ_PREFIX_KEY_CHARACTER:
        CMP     AL,'a'
        JB      SHORT READ_PREFIX_KEY_OK
        CMP     AL,'z'
        JA      SHORT READ_PREFIX_KEY_OK
        SUB     AL,20H
READ_PREFIX_KEY_OK:
        CLC
        RET
READ_PREFIX_KEY_NONE:
        STC
READ_PREFIX_KEY_DONE:
        RET
READ_PREFIX_KEY ENDP

; F2, Shift+F2, Ctrl+F2 and Alt+X do nothing in DOS 5 EDIT (QEMU capture,
; 2026-11-11), so they fall through to the ignored keys here.
EDIT_EXTENDED:
        CMP     AH,38H                  ; a lone Alt tap (DOSUI_ALT_TAP)
        JNE     SHORT EDIT_NOT_MENU_BAR
        MOV     BYTE PTR MENU_BAR_ONLY,1
        MOV     AL,MENU_FILE
        JMP     EDIT_MENU_SYSTEM
EDIT_NOT_MENU_BAR:
        CMP     AH,21H                  ; Alt+F
        JNE     SHORT EDIT_NOT_FILE_MENU
        MOV     AL,MENU_FILE
        JMP     EDIT_MENU_SYSTEM
EDIT_NOT_FILE_MENU:
        CMP     AH,12H                  ; Alt+E
        JNE     SHORT EDIT_NOT_EDIT_MENU
        MOV     AL,MENU_EDIT
        JMP     EDIT_MENU_SYSTEM
EDIT_NOT_EDIT_MENU:
        CMP     AH,1FH                  ; Alt+S
        JNE     SHORT EDIT_NOT_SEARCH_MENU
        MOV     AL,MENU_SEARCH
        JMP     EDIT_MENU_SYSTEM
EDIT_NOT_SEARCH_MENU:
        CMP     AH,18H                  ; Alt+O
        JNE     SHORT EDIT_NOT_OPTIONS_MENU
        MOV     AL,MENU_OPTIONS
        JMP     EDIT_MENU_SYSTEM
EDIT_NOT_OPTIONS_MENU:
        CMP     AH,23H                  ; Alt+H
        JNE     SHORT EDIT_NOT_HELP_MENU
        MOV     AL,MENU_HELP
        JMP     EDIT_MENU_SYSTEM
EDIT_NOT_HELP_MENU:
        CMP     AH,3BH                  ; F1
        JNE     SHORT EDIT_NOT_HELP
        JMP     EDIT_HELP
EDIT_NOT_HELP:
        CMP     AH,40H                  ; F6, Shift+F6: Help's window
        JE      SHORT EDIT_TO_HELP_KEY
        CMP     AH,59H
        JE      SHORT EDIT_TO_HELP_KEY
        CMP     AH,4EH                  ; Alt+Plus, Alt+Minus: the editor's
        JE      SHORT EDIT_TALLER_KEY   ; window a row taller, shorter
        CMP     AH,4AH
        JNE     SHORT EDIT_NOT_WINDOW_KEY
        JMP     EDIT_WINDOW_SHORTER
EDIT_TALLER_KEY:
        JMP     EDIT_WINDOW_TALLER
EDIT_TO_HELP_KEY:
        JMP     EDIT_TO_HELP
EDIT_NOT_WINDOW_KEY:
        CMP     AH,3DH                  ; F3: find next
        JNE     SHORT EDIT_NOT_F3
        JMP     EDIT_FIND_NEXT
EDIT_NOT_F3:
        CMP     AH,77H                  ; Ctrl+Home: beginning of file
        JNE     SHORT EDIT_NOT_FILE_HOME
        JMP     EDIT_FILE_HOME
EDIT_NOT_FILE_HOME:
        CMP     AH,75H                  ; Ctrl+End: end of file
        JNE     SHORT EDIT_NOT_FILE_END
        JMP     EDIT_FILE_END
EDIT_NOT_FILE_END:
        CMP     AH,92H                  ; Ctrl+Insert
        JNE     SHORT EDIT_NOT_CTRL_INSERT
        JMP     EDIT_COPY_BLOCK
EDIT_NOT_CTRL_INSERT:
        CMP     AH,76H                  ; Ctrl+PgDn: a window to the right
        JNE     SHORT EDIT_NOT_PAGE_RIGHT
        JMP     EDIT_PAGE_RIGHT
EDIT_NOT_PAGE_RIGHT:
        CMP     AH,84H                  ; Ctrl+PgUp: a window to the left
        JNE     SHORT EDIT_NOT_PAGE_LEFT
        JMP     EDIT_PAGE_LEFT
EDIT_NOT_PAGE_LEFT:
        CMP     AH,8DH                  ; Ctrl+Up: scroll the view up
        JNE     SHORT EDIT_NOT_SCROLL_UP
        JMP     EDIT_SCROLL_UP
EDIT_NOT_SCROLL_UP:
        CMP     AH,91H                  ; Ctrl+Down: scroll the view down
        JNE     SHORT EDIT_NOT_SCROLL_DOWN
        JMP     EDIT_SCROLL_DOWN
EDIT_NOT_SCROLL_DOWN:
        CMP     AH,0FH                  ; Shift+Tab: outdent
        JNE     SHORT EDIT_NOT_BACK_TAB
        JMP     EDIT_BACK_TAB
EDIT_NOT_BACK_TAB:
        CMP     AH,73H                  ; Ctrl+Left: previous word
        JNE     SHORT EDIT_NOT_WORD_LEFT
        JMP     EDIT_WORD_LEFT
EDIT_NOT_WORD_LEFT:
        CMP     AH,74H                  ; Ctrl+Right: next word
        JNE     SHORT EDIT_NOT_WORD_RIGHT
        JMP     EDIT_WORD_RIGHT
EDIT_NOT_WORD_RIGHT:
        CMP     AH,4BH                  ; Left
        JNE     SHORT EDIT_NOT_LEFT
        JMP     EDIT_LEFT
EDIT_NOT_LEFT:
        CMP     AH,4DH                  ; Right
        JNE     SHORT EDIT_NOT_RIGHT
        JMP     EDIT_RIGHT
EDIT_NOT_RIGHT:
        CMP     AH,48H                  ; Up
        JNE     SHORT EDIT_NOT_UP
        JMP     EDIT_UP
EDIT_NOT_UP:
        CMP     AH,50H                  ; Down
        JNE     SHORT EDIT_NOT_DOWN
        JMP     EDIT_DOWN
EDIT_NOT_DOWN:
        CMP     AH,49H                  ; Page Up
        JNE     SHORT EDIT_NOT_PAGE_UP
        JMP     EDIT_PAGE_UP
EDIT_NOT_PAGE_UP:
        CMP     AH,51H                  ; Page Down
        JNE     SHORT EDIT_NOT_PAGE_DOWN
        JMP     EDIT_PAGE_DOWN
EDIT_NOT_PAGE_DOWN:
        CMP     AH,47H                  ; Home
        JNE     SHORT EDIT_NOT_HOME
        JMP     EDIT_HOME
EDIT_NOT_HOME:
        CMP     AH,4FH                  ; End
        JNE     SHORT EDIT_NOT_END
        JMP     EDIT_END
EDIT_NOT_END:
        CMP     AH,52H                  ; Insert
        JNE     SHORT EDIT_NOT_INSERT_KEY
        TEST    BYTE PTR MODIFIER_STATE,03H
        JZ      SHORT EDIT_INSERT_UNSHIFTED
        JMP     EDIT_PASTE_BLOCK
EDIT_INSERT_UNSHIFTED:
        JMP     EDIT_INSERT_MODE
EDIT_NOT_INSERT_KEY:
        CMP     AH,53H                  ; Delete
        JNE     SHORT EDIT_EXTENDED_UNKNOWN
        TEST    BYTE PTR MODIFIER_STATE,03H
        JZ      SHORT EDIT_DELETE_UNSHIFTED
        JMP     EDIT_CUT_BLOCK
EDIT_DELETE_UNSHIFTED:
        CALL    HAS_SELECTION           ; Del clears a selection (Edit/Clear)
        JNC     SHORT EDIT_DELETE_ONE
        JMP     EDIT_CLEAR_BLOCK
EDIT_DELETE_ONE:
        JMP     EDIT_DELETE
EDIT_EXTENDED_UNKNOWN:
        JMP     EDIT_LOOP

EDIT_LEFT:
        MOV     BYTE PTR STEP_LEFT,1    ; the view follows a single step
        CALL    PREPARE_SHIFT_SELECTION
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT EDIT_LEFT_PHYSICAL
        CALL    CALCULATE_POSITION
        MOV     AX,VIRTUAL_COLUMN
        DEC     AX
        MOV     VIRTUAL_COLUMN,AX
        CMP     AX,CURRENT_COLUMN
        JA      SHORT EDIT_LEFT_REDRAW
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,0
EDIT_LEFT_REDRAW:
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,0
        JMP     EDIT_CURSOR_REDRAW
EDIT_LEFT_PHYSICAL:
        CMP     WORD PTR CURSOR_OFFSET,0
        JNE     SHORT EDIT_LEFT_ADVANCE
        JMP     EDIT_LOOP
EDIT_LEFT_ADVANCE:
        DEC     WORD PTR CURSOR_OFFSET
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,0
        JMP     EDIT_CURSOR_REDRAW

EDIT_RIGHT:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    CURSOR_COLUMN
        CMP     AX,LINE_COLUMN_MAX      ; lines end at column 255
        JB      SHORT EDIT_RIGHT_ROOM
        JMP     EDIT_LOOP
EDIT_RIGHT_ROOM:
        CALL    CALCULATE_POSITION
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JNE     SHORT EDIT_RIGHT_VIRTUAL_ADVANCE
        MOV     AX,CURSOR_OFFSET
        CMP     AX,BUFFER_LENGTH
        JAE     SHORT EDIT_RIGHT_START_VIRTUAL
        MOV     BX,AX
        MOV     AL,BUFFER[BX]
        CMP     AL,13
        JE      SHORT EDIT_RIGHT_START_VIRTUAL
        CMP     AL,10
        JE      SHORT EDIT_RIGHT_START_VIRTUAL
EDIT_RIGHT_ADVANCE:
        INC     WORD PTR CURSOR_OFFSET
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,0
        JMP     EDIT_CURSOR_REDRAW
EDIT_RIGHT_START_VIRTUAL:
        MOV     AX,CURRENT_COLUMN
        INC     AX
        MOV     VIRTUAL_COLUMN,AX
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,1
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,0
        JMP     EDIT_CURSOR_REDRAW
EDIT_RIGHT_VIRTUAL_ADVANCE:
        INC     WORD PTR VIRTUAL_COLUMN
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,0
        JMP     EDIT_CURSOR_REDRAW

EDIT_UP:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    MOVE_UP
        JMP     EDIT_CURSOR_REDRAW

EDIT_DOWN:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    MOVE_DOWN
        JMP     EDIT_CURSOR_REDRAW

EDIT_PAGE_UP:
        CALL    PAGE_UP_VIEW
        CALL    PREPARE_SHIFT_SELECTION
        CALL    MOVE_PAGE_UP
        JMP     EDIT_REDRAW

EDIT_PAGE_DOWN:
        CALL    PAGE_DOWN_VIEW
        CALL    PREPARE_SHIFT_SELECTION
        CALL    MOVE_PAGE_DOWN
        JMP     EDIT_REDRAW

EDIT_HOME:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     BX,CURSOR_OFFSET
EDIT_HOME_NEXT:
        OR      BX,BX
        JZ      EDIT_HOME_DONE
        CMP     BYTE PTR BUFFER[BX-1],10
        JE      EDIT_HOME_DONE
        DEC     BX
        JMP     EDIT_HOME_NEXT
EDIT_HOME_DONE:                         ; DOS 5 Home: first non-blank,
        MOV     DX,BX                   ; or column 0 on a blank line
EDIT_HOME_SKIP:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT EDIT_HOME_BLANK
        MOV     AL,BUFFER[BX]
        CMP     AL,13
        JE      SHORT EDIT_HOME_BLANK
        CMP     AL,10
        JE      SHORT EDIT_HOME_BLANK
        CMP     AL,' '
        JNE     SHORT EDIT_HOME_SET
        INC     BX
        JMP     EDIT_HOME_SKIP
EDIT_HOME_BLANK:
        MOV     BX,DX
EDIT_HOME_SET:
        MOV     CURSOR_OFFSET,BX
        JMP     EDIT_CURSOR_REDRAW

EDIT_END:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     BX,CURSOR_OFFSET
EDIT_END_NEXT:
        CMP     BX,BUFFER_LENGTH
        JAE     EDIT_END_DONE
        MOV     AL,BUFFER[BX]
        CMP     AL,13
        JE      EDIT_END_DONE
        CMP     AL,10
        JE      EDIT_END_DONE
        INC     BX
        JMP     EDIT_END_NEXT
EDIT_END_DONE:
        MOV     CURSOR_OFFSET,BX
        JMP     EDIT_CURSOR_REDRAW

EDIT_WORD_LEFT:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        CALL    MOVE_WORD_LEFT
        JMP     EDIT_CURSOR_REDRAW

EDIT_WORD_RIGHT:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        TEST    BYTE PTR MODIFIER_STATE,03H
        JNZ     SHORT EDIT_WORD_RIGHT_SELECT
        CALL    MOVE_WORD_RIGHT
        JMP     EDIT_CURSOR_REDRAW
EDIT_WORD_RIGHT_SELECT:                 ; Shift+Ctrl+Right selects to a word end
        CALL    MOVE_WORD_END
        JMP     EDIT_REDRAW

; Tab inserts blanks up to the next tab stop (Options Display Tab Stops, 8
; by default), as DOS 5 EDIT does - in overwrite mode too.
EDIT_TAB:
        CALL    MARK_IS_OUTSIDE         ; a selection beyond the view
        JNC     EDIT_TAB_LOCAL
        CALL    GLOBAL_INDENT
        JMP     EDIT_REDRAW
EDIT_TAB_LOCAL:
        CALL    SELECTED_LINES          ; a selection over lines indents them
        JNC     SHORT EDIT_TAB_PLAIN
        CALL    INDENT_LINES
        JMP     EDIT_REDRAW
EDIT_TAB_PLAIN:
        CALL    COLLAPSE_SELECTION
        CALL    VIRTUAL_GAP
        ADD     CX,99                   ; the widest Tab Stops
        CALL    ROOM_FOR
        JNC     EDIT_TAB_ROOM
        JMP     EDIT_REDRAW
EDIT_TAB_ROOM:
        CALL    MATERIALIZE_VIRTUAL_COLUMN
        CALL    CALCULATE_POSITION
        MOV     AX,CURRENT_COLUMN       ; to the next tab stop
        XOR     DX,DX
        XOR     CX,CX
        MOV     CL,TAB_STOPS
        DIV     CX
        SUB     CX,DX
        MOV     AL,INSERT_MODE
        PUSH    AX
        MOV     BYTE PTR INSERT_MODE,1
        MOV     AL,' '
EDIT_TAB_NEXT:
        CALL    INSERT_BYTE
        LOOP    EDIT_TAB_NEXT
        POP     AX
        MOV     INSERT_MODE,AL
        CALL    RESET_NAVIGATION_COLUMNS
        JMP     EDIT_REDRAW

EDIT_FILE_HOME:                         ; first line, first non-blank
        CALL    PREPARE_SHIFT_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        XOR     AX,AX                   ; the view at the text's start
        XOR     DX,DX
        CALL    SET_CURSOR_GLOBAL
        MOV     WORD PTR TOP_LINE,0
        XOR     BX,BX
        JMP     EDIT_HOME_DONE

EDIT_FILE_END:
        CALL    PREPARE_SHIFT_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        CALL    DOC_LENGTH
        CALL    SET_CURSOR_GLOBAL
        JMP     EDIT_REDRAW

EDIT_INSERT_MODE:
        XOR     BYTE PTR INSERT_MODE,1
        JMP     EDIT_REDRAW

; Del at a line end joins the next line and, as in DOS 5 EDIT, drops that
; line's indentation. Past the end of a line the gap is filled first.
EDIT_DELETE:
        CALL    MATERIALIZE_VIRTUAL_COLUMN
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     BX,CURSOR_OFFSET
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT EDIT_DELETE_DONE
        MOV     AL,BUFFER[BX]
        PUSH    AX
        CALL    DELETE_AT_CURSOR
        POP     AX
        CMP     AL,13
        JE      SHORT EDIT_DELETE_JOINED
        CMP     AL,10
        JNE     SHORT EDIT_DELETE_DONE
EDIT_DELETE_JOINED:
        MOV     BX,CURSOR_OFFSET
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT EDIT_DELETE_DONE
        CMP     BYTE PTR BUFFER[BX],' '
        JNE     SHORT EDIT_DELETE_DONE
        CALL    DELETE_AT_CURSOR
        JMP     EDIT_DELETE_JOINED
EDIT_DELETE_DONE:
        JMP     EDIT_REDRAW

EDIT_BACKSPACE:
        CALL    DELETE_SELECTION        ; Backspace deletes a selection
        JC      SHORT EDIT_BACKSPACE_DONE
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT EDIT_BACKSPACE_TEXT
        CALL    CALCULATE_POSITION      ; past the line end it only moves
        DEC     WORD PTR VIRTUAL_COLUMN
        MOV     AX,VIRTUAL_COLUMN
        CMP     AX,CURRENT_COLUMN
        JA      SHORT EDIT_BACKSPACE_MOVED
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,0
EDIT_BACKSPACE_MOVED:
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,0
        JMP     EDIT_REDRAW
EDIT_BACKSPACE_TEXT:
        CALL    RESET_NAVIGATION_COLUMNS
        CALL    OUTDENT_AT_CURSOR       ; inside the indentation: outdent
        JC      SHORT EDIT_BACKSPACE_DONE
        CALL    DELETE_BEFORE_CURSOR
EDIT_BACKSPACE_DONE:
        JMP     EDIT_REDRAW

; Shift+Tab outdents the selected lines, or else the cursor's line.
EDIT_BACK_TAB:
        CALL    MARK_IS_OUTSIDE         ; a selection beyond the view
        JNC     EDIT_BACK_TAB_LOCAL
        CALL    GLOBAL_OUTDENT
        JMP     EDIT_REDRAW
EDIT_BACK_TAB_LOCAL:
        CALL    SELECTED_LINES
        JNC     SHORT EDIT_BACK_TAB_LINE
        CALL    OUTDENT_LINES
        JMP     EDIT_REDRAW
EDIT_BACK_TAB_LINE:
        CALL    COLLAPSE_SELECTION
        CALL    OUTDENT_CURSOR_LINE
        JMP     EDIT_REDRAW

; With Help open: F6 makes it the active window; Alt+Plus and Alt+Minus
; give the editor's window a row more or less; Esc closes Help.
EDIT_TO_HELP:
        CMP     BYTE PTR HELP_OPEN,0
        JNE     SHORT EDIT_TO_HELP_OPEN
        JMP     EDIT_LOOP
EDIT_TO_HELP_OPEN:
        MOV     BYTE PTR HELP_ACTIVE,1
        JMP     EDIT_REDRAW
EDIT_WINDOW_TALLER:
        MOV     AL,0FFH
        JMP     SHORT EDIT_WINDOW_RESIZE
EDIT_WINDOW_SHORTER:
        MOV     AL,1
EDIT_WINDOW_RESIZE:
        CMP     BYTE PTR HELP_OPEN,0
        JNE     SHORT EDIT_WINDOW_RESIZE_OPEN
        JMP     EDIT_LOOP
EDIT_WINDOW_RESIZE_OPEN:
        CALL    HELP_RESIZE
        JMP     EDIT_REDRAW

EDIT_ESCAPE:
        CMP     BYTE PTR HELP_OPEN,0
        JE      SHORT EDIT_ESCAPE_EDITOR
        CALL    HELP_CLOSE
        JMP     EDIT_REDRAW
EDIT_ESCAPE_EDITOR:
        CMP     BYTE PTR SHIFT_SELECTION,0
        JNE     SHORT EDIT_ESCAPE_SELECTION
        JMP     EDIT_LOOP
EDIT_ESCAPE_SELECTION:
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
        JMP     EDIT_REDRAW

; Ctrl+Y removes the cursor's whole line, line end included, into the
; clipboard (Shift+Ins brings it back) and leaves the cursor on the
; following line's first non-blank.
EDIT_CUT_LINE:
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
        CALL    CALCULATE_POSITION
        MOV     BX,CURSOR_OFFSET
        SUB     BX,CURRENT_COLUMN
EDIT_CUT_LINE_START:                    ; a lone CR before the LF is not text
        OR      BX,BX
        JZ      SHORT EDIT_CUT_LINE_HAVE_START
        CMP     BYTE PTR BUFFER[BX-1],10
        JE      SHORT EDIT_CUT_LINE_HAVE_START
        DEC     BX
        JMP     EDIT_CUT_LINE_START
EDIT_CUT_LINE_HAVE_START:
        MOV     DX,BX
EDIT_CUT_LINE_END:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT EDIT_CUT_LINE_HAVE_END
        INC     BX
        CMP     BYTE PTR BUFFER[BX-1],10
        JNE     EDIT_CUT_LINE_END
EDIT_CUT_LINE_HAVE_END:
        CMP     BX,DX
        JNE     SHORT EDIT_CUT_LINE_SPAN
        JMP     EDIT_LOOP                ; the empty last line: nothing to cut
EDIT_CUT_LINE_SPAN:
        MOV     BLOCK_MARK,DX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     CURSOR_OFFSET,BX
        CALL    CUT_BLOCK
        JNC     SHORT EDIT_CUT_LINE_DONE
        JMP     EDIT_MEMORY_ERROR
EDIT_CUT_LINE_DONE:
        MOV     BX,CURSOR_OFFSET
EDIT_CUT_LINE_INDENT:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT EDIT_CUT_LINE_PLACED
        CMP     BYTE PTR BUFFER[BX],' '
        JNE     SHORT EDIT_CUT_LINE_PLACED
        INC     BX
        JMP     EDIT_CUT_LINE_INDENT
EDIT_CUT_LINE_PLACED:
        MOV     CURSOR_OFFSET,BX
        JMP     EDIT_REDRAW

; Enter keeps the indentation of the line it splits: the new line starts at
; the column of that line's first non-blank. An otherwise empty new line holds
; the cursor there without storing blanks; text carried down is indented.
EDIT_ENTER:
        CALL    NEW_LINE
        JMP     EDIT_REDRAW

; Ctrl+N splits the line like Enter, but the cursor stays where it was.
EDIT_SPLIT_LINE:
        PUSH    WORD PTR CURSOR_OFFSET
        CALL    NEW_LINE
        POP     WORD PTR CURSOR_OFFSET
        CALL    RESET_NAVIGATION_COLUMNS
        JMP     EDIT_REDRAW

NEW_LINE PROC NEAR
        CALL    LINE_INDENT
        MOV     CX,AX                   ; CR, LF and the indentation
        ADD     CX,2
        CALL    ROOM_FOR
        JNC     NEW_LINE_ROOM
        RET
NEW_LINE_ROOM:
        PUSH    AX
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,0
        MOV     AL,13
        CALL    INSERT_BYTE
        MOV     AL,10
        CALL    INSERT_BYTE
        CALL    RESET_NAVIGATION_COLUMNS
        POP     CX
        JCXZ    EDIT_ENTER_DONE
        MOV     BX,CURSOR_OFFSET
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT EDIT_ENTER_VIRTUAL
        CMP     BYTE PTR BUFFER[BX],13
        JE      SHORT EDIT_ENTER_VIRTUAL
        CMP     BYTE PTR BUFFER[BX],10
        JE      SHORT EDIT_ENTER_VIRTUAL
        MOV     AL,INSERT_MODE
        PUSH    AX
        MOV     BYTE PTR INSERT_MODE,1
        MOV     AL,' '
EDIT_ENTER_INDENT:
        CALL    INSERT_BYTE
        LOOP    EDIT_ENTER_INDENT
        POP     AX
        MOV     INSERT_MODE,AL
        RET
EDIT_ENTER_VIRTUAL:
        MOV     VIRTUAL_COLUMN,CX
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,1
EDIT_ENTER_DONE:
        RET
NEW_LINE ENDP

; ---- DOS 5 (WordStar) control keys ------------------------------------
; Ctrl+K n sets bookmark n (0-3), Ctrl+Q n goes to it; Ctrl+Q with S, D,
; R, C, E, X moves to line start, line end, file start, file end, window
; top, window bottom; Ctrl+Q F/A find/change; Ctrl+Q Y deletes to the end
; of the line. Ctrl+P puts the next key in as a character.
EDIT_PREFIX_Q:
        CALL    READ_PREFIX_KEY
        JC      SHORT EDIT_PREFIX_Q_DONE
        CMP     AL,'S'
        JNE     SHORT EDIT_Q_NOT_S
        JMP     EDIT_HOME
EDIT_Q_NOT_S:
        CMP     AL,'D'
        JNE     SHORT EDIT_Q_NOT_D
        JMP     EDIT_END
EDIT_Q_NOT_D:
        CMP     AL,'R'
        JNE     SHORT EDIT_Q_NOT_R
        JMP     EDIT_FILE_HOME
EDIT_Q_NOT_R:
        CMP     AL,'C'
        JNE     SHORT EDIT_Q_NOT_C
        JMP     EDIT_FILE_END
EDIT_Q_NOT_C:
        CMP     AL,'E'
        JNE     SHORT EDIT_Q_NOT_E
        JMP     EDIT_WINDOW_TOP
EDIT_Q_NOT_E:
        CMP     AL,'X'
        JNE     SHORT EDIT_Q_NOT_X
        JMP     EDIT_WINDOW_BOTTOM
EDIT_Q_NOT_X:
        CMP     AL,'F'
        JNE     SHORT EDIT_Q_NOT_F
        JMP     EDIT_FIND
EDIT_Q_NOT_F:
        CMP     AL,'A'
        JNE     SHORT EDIT_Q_NOT_A
        JMP     EDIT_REPLACE
EDIT_Q_NOT_A:
        CMP     AL,'Y'
        JNE     SHORT EDIT_Q_NOT_Y
        JMP     EDIT_DELETE_LINE_END
EDIT_Q_NOT_Y:
        SUB     AL,'0'
        CMP     AL,3
        JA      SHORT EDIT_PREFIX_Q_DONE
        CALL    BOOKMARK_INDEX
        CMP     WORD PTR BOOKMARKS[BX+2],0FFFFH
        JE      SHORT EDIT_PREFIX_Q_DONE
        CALL    COLLAPSE_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     AX,BOOKMARKS[BX]
        MOV     DX,BOOKMARKS[BX+2]
        CALL    VIEW_TO_LINE
        MOV     DX,BOOKMARKS[BX+4]
        CALL    MOVE_TO_LINE_COLUMN
        JMP     EDIT_REDRAW
EDIT_PREFIX_Q_DONE:
        JMP     EDIT_LOOP

EDIT_PREFIX_K:
        CALL    READ_PREFIX_KEY
        JC      SHORT EDIT_PREFIX_K_DONE
        SUB     AL,'0'
        CMP     AL,3
        JA      SHORT EDIT_PREFIX_K_DONE
        CALL    BOOKMARK_INDEX
        CALL    CALCULATE_POSITION
        MOV     AX,CURRENT_LINE         ; the line in the whole text
        XOR     DX,DX
        ADD     AX,VIEW_LINE
        ADC     DX,VIEW_LINE+2
        MOV     BOOKMARKS[BX],AX
        MOV     BOOKMARKS[BX+2],DX
        MOV     AX,CURRENT_COLUMN
        MOV     BOOKMARKS[BX+4],AX
EDIT_PREFIX_K_DONE:
        JMP     EDIT_LOOP

; AL = a bookmark (0-3): BX = its offset in BOOKMARKS (six bytes each).
BOOKMARK_INDEX PROC NEAR
        XOR     BX,BX
        MOV     BL,AL
        SHL     BX,1
        ADD     BL,AL
        SHL     BX,1
        RET
BOOKMARK_INDEX ENDP

EDIT_LITERAL:
        CALL    DOSUI_READ_EVENT
        JC      SHORT EDIT_LITERAL_DONE
        OR      AL,AL
        JZ      SHORT EDIT_LITERAL_DONE
        CMP     AL,13                   ; line ends stay line ends
        JE      SHORT EDIT_LITERAL_DONE
        CMP     AL,10
        JE      SHORT EDIT_LITERAL_DONE
        CALL    TYPE_CHARACTER
        JMP     EDIT_REDRAW
EDIT_LITERAL_DONE:
        JMP     EDIT_LOOP

EDIT_WINDOW_TOP:
        MOV     AX,TOP_LINE
        JMP     SHORT EDIT_WINDOW_LINE
EDIT_WINDOW_BOTTOM:
        CALL    FIND_LAST_LINE
        MOV     BX,TOP_LINE
        ADD     BX,TEXT_ROW_COUNT
        DEC     BX
        CMP     BX,AX
        JAE     SHORT EDIT_WINDOW_LINE
        MOV     AX,BX
EDIT_WINDOW_LINE:
        PUSH    AX
        CALL    COLLAPSE_SELECTION
        CALL    PREPARE_VERTICAL_COLUMN
        POP     AX
        CALL    VERTICAL_MOVE_TO
        JMP     EDIT_REDRAW

; Ctrl+PgDn/Ctrl+PgUp move the view and the cursor a window width (78
; columns) right or left; the cursor keeps its screen column. Lines end at
; column 255 and the view at column 178, as in DOS 5.
EDIT_PAGE_RIGHT:
        CALL    COLLAPSE_SELECTION
        CALL    CURSOR_COLUMN
        ADD     AX,78
        CMP     AX,LINE_COLUMN_MAX
        JA      SHORT EDIT_PAGE_SIDEWAYS_NONE
        PUSH    AX
        MOV     AX,LEFT_COLUMN
        ADD     AX,78
        CMP     AX,LEFT_COLUMN_MAX
        JBE     SHORT EDIT_PAGE_RIGHT_LEFT
        MOV     AX,LEFT_COLUMN_MAX
EDIT_PAGE_RIGHT_LEFT:
        MOV     LEFT_COLUMN,AX
        POP     AX
        JMP     SHORT EDIT_PAGE_SIDEWAYS
EDIT_PAGE_LEFT:
        CMP     WORD PTR LEFT_COLUMN,0
        JE      SHORT EDIT_PAGE_SIDEWAYS_NONE
        CALL    COLLAPSE_SELECTION
        MOV     AX,LEFT_COLUMN
        SUB     AX,78
        JNC     SHORT EDIT_PAGE_LEFT_LEFT
        XOR     AX,AX
EDIT_PAGE_LEFT_LEFT:
        MOV     LEFT_COLUMN,AX
        CALL    CURSOR_COLUMN
        SUB     AX,78
        JNC     SHORT EDIT_PAGE_SIDEWAYS
        XOR     AX,AX
EDIT_PAGE_SIDEWAYS:
        CALL    SET_CURSOR_COLUMN
        JMP     EDIT_REDRAW
EDIT_PAGE_SIDEWAYS_NONE:
        JMP     EDIT_LOOP

; Ctrl+W/Ctrl+Up and Ctrl+Z/Ctrl+Down scroll the view one line; the cursor
; keeps its line unless that would leave the window. The view does not
; scroll down once the last line is in it.
EDIT_SCROLL_UP:
        CMP     WORD PTR TOP_LINE,0
        JE      SHORT EDIT_SCROLL_NONE
        DEC     WORD PTR TOP_LINE
        CALL    CALCULATE_POSITION
        MOV     AX,TOP_LINE
        ADD     AX,TEXT_ROW_COUNT
        CMP     CURRENT_LINE,AX
        JB      SHORT EDIT_SCROLL_DRAWN
        CALL    MOVE_UP
        JMP     SHORT EDIT_SCROLL_DRAWN
EDIT_SCROLL_DOWN:
        CALL    FIND_LAST_LINE
        MOV     BX,TOP_LINE
        ADD     BX,TEXT_ROW_COUNT
        CMP     BX,AX
        JA      SHORT EDIT_SCROLL_NONE
        INC     WORD PTR TOP_LINE
        CALL    CALCULATE_POSITION
        MOV     AX,CURRENT_LINE
        CMP     AX,TOP_LINE
        JAE     SHORT EDIT_SCROLL_DRAWN
        CALL    MOVE_DOWN
EDIT_SCROLL_DRAWN:
        JMP     EDIT_REDRAW
EDIT_SCROLL_NONE:
        JMP     EDIT_LOOP

; Ctrl+T as real DOS 5 EDIT does it (QEMU captures, 2026-11-11, plans T,
; T2-T4): from the cursor it deletes the rest of a run of word characters
; (letters, digits, . ! # $ % & and the characters from 80h) or of blanks
; (spaces and tabs), never past the line end; any other character goes
; alone. At or past the line end it pads the line to the
; cursor and joins the next line without its leading blanks; on the last
; line (and on the empty line after it) it does nothing.
EDIT_DELETE_WORD:
        CALL    COLLAPSE_SELECTION
        CALL    CALCULATE_POSITION
        MOV     BX,CURSOR_OFFSET
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JNE     SHORT EDIT_DELETE_WORD_JOIN
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT EDIT_DELETE_WORD_NONE
        MOV     AL,BUFFER[BX]
        CMP     AL,13
        JE      SHORT EDIT_DELETE_WORD_JOIN
        CMP     AL,10
        JE      SHORT EDIT_DELETE_WORD_JOIN
        CALL    RESET_NAVIGATION_COLUMNS
        CALL    DELETE_WORD_CLASS
        MOV     DL,AL
        MOV     SI,BX
        CMP     AL,2                    ; any other character goes alone
        JNE     SHORT EDIT_DELETE_WORD_NEXT
        INC     SI
        JMP     SHORT EDIT_DELETE_SPAN_READY
EDIT_DELETE_WORD_NEXT:
        CMP     SI,BUFFER_LENGTH
        JAE     SHORT EDIT_DELETE_SPAN_READY
        MOV     AL,BUFFER[SI]
        CMP     AL,13
        JE      SHORT EDIT_DELETE_SPAN_READY
        CMP     AL,10
        JE      SHORT EDIT_DELETE_SPAN_READY
        CALL    DELETE_WORD_CLASS
        CMP     AL,DL
        JNE     SHORT EDIT_DELETE_SPAN_READY
        INC     SI
        JMP     EDIT_DELETE_WORD_NEXT
EDIT_DELETE_WORD_NONE:
        JMP     EDIT_REDRAW
EDIT_DELETE_WORD_JOIN:                  ; BX = the line end (CR or LF)
        CALL    NEXT_LINE_START         ; from the cursor's line end
        CMP     BX,BUFFER_LENGTH        ; no line after this one
        JAE     SHORT EDIT_DELETE_WORD_NONE
        CALL    MATERIALIZE_VIRTUAL_COLUMN
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     BX,CURSOR_OFFSET
        MOV     SI,BX
        CMP     BYTE PTR BUFFER[SI],13
        JNE     SHORT EDIT_DELETE_WORD_LF
        INC     SI
EDIT_DELETE_WORD_LF:
        INC     SI                      ; past the LF
EDIT_DELETE_WORD_BLANKS:
        CMP     SI,BUFFER_LENGTH
        JAE     SHORT EDIT_DELETE_SPAN_READY
        CMP     BYTE PTR BUFFER[SI],' '
        JNE     SHORT EDIT_DELETE_SPAN_READY
        INC     SI
        JMP     EDIT_DELETE_WORD_BLANKS
; Ctrl+Q Y deletes to the end of the line.
EDIT_DELETE_LINE_END:
        CALL    COLLAPSE_SELECTION
        CALL    MATERIALIZE_VIRTUAL_COLUMN
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     BX,CURSOR_OFFSET
        MOV     SI,BX
EDIT_DELETE_LINE_END_NEXT:
        CMP     SI,BUFFER_LENGTH
        JAE     SHORT EDIT_DELETE_SPAN_READY
        CMP     BYTE PTR BUFFER[SI],13
        JE      SHORT EDIT_DELETE_SPAN_READY
        CMP     BYTE PTR BUFFER[SI],10
        JE      SHORT EDIT_DELETE_SPAN_READY
        INC     SI
        JMP     EDIT_DELETE_LINE_END_NEXT
EDIT_DELETE_SPAN_READY:
        MOV     CX,SI
        SUB     CX,BX
        CALL    DELETE_SPAN_AT
        JMP     EDIT_REDRAW

; ---- File New / Open --------------------------------------------------
; A modified file is offered for saving first (Cancel keeps it).
EDIT_NEW:
        CALL    OFFER_SAVE
        JC      SHORT EDIT_FILE_KEPT
        CALL    CLEAR_DOCUMENT
        JMP     EDIT_REDRAW
EDIT_FILE_KEPT:
        JMP     EDIT_REDRAW

EDIT_OPEN:
        CALL    OFFER_SAVE
        JC      SHORT EDIT_FILE_KEPT
        CALL    SHOW_OPEN_DIALOG
        JC      SHORT EDIT_FILE_KEPT
        CALL    CLEAR_DOCUMENT
        MOV     SI,OFFSET OPEN_NAME
        MOV     DI,OFFSET FILE_NAME
        MOV     CX,64
        REP     MOVSB
        CALL    COMMIT_DISPLAY_NAME
        CALL    LOAD_FILE
        JMP     EDIT_REDRAW

; ---- File Print and Options Display -----------------------------------
; Print sends the selected text or the whole document through DOS to PRN
; (handle 4) with CR LF line ends and a closing form feed.
EDIT_PRINT:
        MOV     BYTE PTR PRINT_CHOICE,1
        CALL    HAS_SELECTION
        JNC     SHORT EDIT_PRINT_ASK
        MOV     BYTE PTR PRINT_CHOICE,0 ; a selection: Selected Text Only
EDIT_PRINT_ASK:
        CALL    DRAW_SCREEN
        MOV     BYTE PTR MENU_ACTIVE,MENU_FILE
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET PRINT_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BL,70H
        MOV     SI,OFFSET PRINT_TITLE_TEXT
        MOV     DH,7
        MOV     DL,33
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET PRINT_SELECTED_TEXT
        MOV     DH,9
        MOV     DL,27
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET PRINT_DOCUMENT_TEXT
        MOV     DH,10
        CALL    DOSUI_WRITE_Z
        MOV     DH,13
        MOV     DL,20
        MOV     CX,34
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET PRINT_BUTTONS_TEXT
        MOV     DH,14
        MOV     DL,23
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET PRINT_CONTROLS
        MOV     CX,0104H
        XOR     AL,AL
        CALL    RUN_DIALOG
        CMP     AL,DLG_HELP
        JNE     SHORT EDIT_PRINT_ANSWER
        CALL    SHOW_KEYBOARD_HELP
        JMP     EDIT_PRINT_ASK
EDIT_PRINT_ANSWER:
        OR      AL,AL
        JZ      SHORT EDIT_PRINT_DONE
        CALL    PRINT_DOCUMENT          ; DOS 5 reports no printer error
EDIT_PRINT_DONE:
        JMP     EDIT_REDRAW

EDIT_DISPLAY:
        MOV     AL,TEXT_ATTR
        MOV     DISPLAY_ATTR,AL         ; what the sample shows until a list moves
        AND     AX,000FH
        CALL    DISPLAY_COLOUR_INDEX
        MOV     DISPLAY_FOREGROUND,AX
        MOV     AL,TEXT_ATTR
        MOV     CL,4
        SHR     AL,CL
        AND     AX,000FH
        TEST    BYTE PTR EDIT_SWITCHES,1
        JNZ     SHORT EDIT_DISPLAY_BACKGROUND   ; /B: 8 and 15 are colours
        AND     AX,0007H
EDIT_DISPLAY_BACKGROUND:
        CALL    DISPLAY_COLOUR_INDEX
        MOV     DISPLAY_BACKGROUND,AX
        MOV     WORD PTR DISPLAY_FOREGROUND_TOP,0
        MOV     WORD PTR DISPLAY_BACKGROUND_TOP,0
        CMP     WORD PTR DISPLAY_FOREGROUND_COUNT,8
        JBE     SHORT EDIT_DISPLAY_SHOWN
        CMP     WORD PTR DISPLAY_FOREGROUND,8
        JB      SHORT EDIT_DISPLAY_SHOWN
        MOV     WORD PTR DISPLAY_FOREGROUND_TOP,8
EDIT_DISPLAY_SHOWN:
        MOV     AL,SCROLL_BARS
        MOV     DISPLAY_SCROLL_BARS,AL
        MOV     AL,TAB_STOPS
        DB      0D4H,0AH                ; AAM
        XCHG    AL,AH
        ADD     AX,3030H
        MOV     WORD PTR DISPLAY_TAB_TEXT,AX
        MOV     BYTE PTR DISPLAY_TAB_TEXT+2,0
        CMP     AL,'0'
        JNE     SHORT EDIT_DISPLAY_ASK
        MOV     DISPLAY_TAB_TEXT,AH     ; one digit
        MOV     BYTE PTR DISPLAY_TAB_TEXT+1,0
EDIT_DISPLAY_ASK:
        CALL    DRAW_SCREEN
        MOV     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET DISPLAY_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET DISPLAY_TEXTS
EDIT_DISPLAY_TEXT_NEXT:
        LODSW
        OR      AX,AX
        JZ      SHORT EDIT_DISPLAY_BOXES
        XCHG    AH,AL                   ; row, column
        MOV     DX,AX
        MOV     BL,70H
        CALL    DOSUI_WRITE_Z
EDIT_DISPLAY_TEXT_SKIP:
        LODSB
        OR      AL,AL
        JNZ     EDIT_DISPLAY_TEXT_SKIP
        JMP     EDIT_DISPLAY_TEXT_NEXT
EDIT_DISPLAY_BOXES:
        MOV     SI,OFFSET DISPLAY_COLORS_BOX
        CALL    DRAW_LIST_BOX
        MOV     SI,OFFSET DISPLAY_OPTIONS_BOX
        CALL    DRAW_LIST_BOX
        MOV     SI,OFFSET DISPLAY_FOREGROUND_BOX
        CALL    DRAW_LIST_BOX
        MOV     SI,OFFSET DISPLAY_BACKGROUND_BOX
        CALL    DRAW_LIST_BOX
        CALL    DISPLAY_PREVIEW
        MOV     SI,OFFSET DISPLAY_TITLES
EDIT_DISPLAY_TITLE_NEXT:
        LODSW
        OR      AX,AX
        JZ      SHORT EDIT_DISPLAY_RUN
        XCHG    AH,AL                   ; row, column
        MOV     DX,AX
        MOV     BL,70H
        CALL    DOSUI_WRITE_Z
EDIT_DISPLAY_TITLE_SKIP:
        LODSB
        OR      AL,AL
        JNZ     EDIT_DISPLAY_TITLE_SKIP
        JMP     EDIT_DISPLAY_TITLE_NEXT
EDIT_DISPLAY_RUN:
        MOV     DH,20
        MOV     DL,10
        MOV     CX,60
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET DISPLAY_BUTTONS_TEXT
        MOV     DH,21
        MOV     DL,20
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET DISPLAY_CONTROLS
        MOV     CX,0407H                ; OK is the default
        XOR     AL,AL
        CALL    RUN_DIALOG
        CMP     AL,DLG_HELP
        JNE     SHORT EDIT_DISPLAY_ANSWER
        CALL    SHOW_KEYBOARD_HELP
        JMP     EDIT_DISPLAY_ASK
EDIT_DISPLAY_ANSWER:
        OR      AL,AL
        JZ      SHORT EDIT_DISPLAY_DONE
        MOV     BYTE PTR INI_DIRTY,1    ; QBASIC.INI is written at exit
        MOV     AL,DISPLAY_ATTR         ; normal text, and the selection as
        CALL    SET_TEXT_ATTRIBUTE      ; its inverse, as in DOS 5
        MOV     AL,DISPLAY_SCROLL_BARS
        CALL    SET_SCROLL_BARS
        MOV     AL,DISPLAY_TAB_TEXT     ; Tab Stops 1-99
        SUB     AL,'0'
        CMP     AL,9
        JA      SHORT EDIT_DISPLAY_DONE
        MOV     AH,DISPLAY_TAB_TEXT+1
        OR      AH,AH
        JZ      SHORT EDIT_DISPLAY_TAB_READY
        SUB     AH,'0'
        CMP     AH,9
        JA      SHORT EDIT_DISPLAY_DONE
        MOV     DL,10
        MUL     DL
        ADD     AL,DISPLAY_TAB_TEXT+1
        SUB     AL,'0'
EDIT_DISPLAY_TAB_READY:
        OR      AL,AL
        JZ      SHORT EDIT_DISPLAY_DONE
        MOV     TAB_STOPS,AL
EDIT_DISPLAY_DONE:
        JMP     EDIT_REDRAW

EDIT_SAVE:
        CALL    SAVE_FILE
        JNC     SHORT EDIT_SAVE_OK
        CMP     BYTE PTR SAVE_CANCELLED,0
        JE      SHORT EDIT_SAVE_REAL_ERROR
        JMP     EDIT_REDRAW
EDIT_SAVE_REAL_ERROR:
        JMP     EDIT_SAVE_ERROR
EDIT_SAVE_OK:
        CALL    SHOW_SAVED
        JMP     EDIT_REDRAW
EDIT_SAVE_ERROR:
        CMP     BYTE PTR SAVE_CANCELLED,0
        JNE     SHORT EDIT_SAVE_ERROR_DONE
EDIT_SHOW_FILE_ERROR:
        CALL    SHOW_FILE_ERROR
EDIT_SAVE_ERROR_DONE:
        JMP     EDIT_REDRAW
EDIT_MEMORY_ERROR:
        MOV     WORD PTR FILE_ERROR,ERROR_MEMORY
        JMP     EDIT_SHOW_FILE_ERROR

EDIT_HELP:
        XOR     AL,AL                   ; the first topic
        CALL    HELP_OPEN_AT
        JMP     EDIT_REDRAW

EDIT_MENU_SYSTEM:
        CALL    SHOW_MENU_SYSTEM
        CALL    MENU_COMMAND_FOCUS
        CMP     AL,COMMAND_SAVE
        JNE     SHORT EDIT_MENU_NOT_SAVE
        JMP     EDIT_SAVE
EDIT_MENU_NOT_SAVE:
        CMP     AL,COMMAND_NEW
        JNE     SHORT EDIT_MENU_NOT_NEW
        JMP     EDIT_NEW
EDIT_MENU_NOT_NEW:
        CMP     AL,COMMAND_OPEN
        JNE     SHORT EDIT_MENU_NOT_OPEN
        JMP     EDIT_OPEN
EDIT_MENU_NOT_OPEN:
        CMP     AL,COMMAND_PRINT
        JNE     SHORT EDIT_MENU_NOT_PRINT
        JMP     EDIT_PRINT
EDIT_MENU_NOT_PRINT:
        CMP     AL,COMMAND_SAVE_AS
        JNE     SHORT EDIT_MENU_NOT_SAVE_AS
        MOV     BYTE PTR SAVE_AS_REQUESTED,1
        JMP     EDIT_SAVE
EDIT_MENU_NOT_SAVE_AS:
        CMP     AL,COMMAND_EXIT
        JNE     SHORT EDIT_MENU_NOT_EXIT
        JMP     EDIT_EXIT_REQUEST
EDIT_MENU_NOT_EXIT:
        CMP     AL,COMMAND_MARK
        JNE     SHORT EDIT_MENU_NOT_MARK
        JMP     EDIT_MARK_BLOCK
EDIT_MENU_NOT_MARK:
        CMP     AL,COMMAND_COPY
        JNE     SHORT EDIT_MENU_NOT_COPY
        JMP     EDIT_COPY_BLOCK
EDIT_MENU_NOT_COPY:
        CMP     AL,COMMAND_CUT
        JNE     SHORT EDIT_MENU_NOT_CUT
        JMP     EDIT_CUT_BLOCK
EDIT_MENU_NOT_CUT:
        CMP     AL,COMMAND_PASTE
        JNE     SHORT EDIT_MENU_NOT_PASTE
        JMP     EDIT_PASTE_BLOCK
EDIT_MENU_NOT_PASTE:
        CMP     AL,COMMAND_CLEAR
        JNE     SHORT EDIT_MENU_NOT_CLEAR
        JMP     EDIT_CLEAR_BLOCK
EDIT_MENU_NOT_CLEAR:
        CMP     AL,COMMAND_FIND
        JNE     SHORT EDIT_MENU_NOT_FIND
        JMP     EDIT_FIND
EDIT_MENU_NOT_FIND:
        CMP     AL,COMMAND_FIND_NEXT
        JNE     SHORT EDIT_MENU_NOT_FIND_NEXT
        JMP     EDIT_FIND_NEXT
EDIT_MENU_NOT_FIND_NEXT:
        CMP     AL,COMMAND_REPLACE
        JNE     SHORT EDIT_MENU_NOT_REPLACE
        JMP     EDIT_REPLACE
EDIT_MENU_NOT_REPLACE:
        CMP     AL,COMMAND_DISPLAY
        JNE     SHORT EDIT_MENU_NOT_DISPLAY
        JMP     EDIT_DISPLAY
EDIT_MENU_NOT_DISPLAY:
        CMP     AL,COMMAND_HELP_PATH
        JNE     SHORT EDIT_MENU_NOT_HELP_PATH
        CALL    SHOW_HELP_PATH
        JMP     EDIT_REDRAW
EDIT_MENU_NOT_HELP_PATH:
        CMP     AL,COMMAND_GETTING_STARTED
        JNE     SHORT EDIT_MENU_NOT_GETTING_STARTED
        MOV     AL,HELP_TOPIC_STARTED
        CALL    HELP_OPEN_AT
        JMP     EDIT_REDRAW
EDIT_MENU_NOT_GETTING_STARTED:
        CMP     AL,COMMAND_KEYBOARD
        JNE     SHORT EDIT_MENU_NOT_KEYBOARD
        MOV     AL,HELP_TOPIC_KEYBOARD
        CALL    HELP_OPEN_AT
        JMP     EDIT_REDRAW
EDIT_MENU_NOT_KEYBOARD:
        CMP     AL,COMMAND_ABOUT
        JNE     SHORT EDIT_MENU_NOT_ABOUT
        CALL    SHOW_ABOUT
EDIT_MENU_NOT_ABOUT:
        JMP     EDIT_REDRAW

EDIT_FIND:
        CALL    DRAW_SCREEN
        CALL    PROMPT_FIND
        OR      AL,AL
        JNZ     SHORT EDIT_FIND_SEARCH
        JMP     EDIT_REDRAW
EDIT_FIND_SEARCH:                       ; DOS 5 searches from after the cursor
        JMP     SHORT EDIT_FIND_NEXT_READY
EDIT_FIND_MISSING:
        CALL    SHOW_NOT_FOUND
        JMP     EDIT_REDRAW

EDIT_FIND_NEXT:
        CMP     BYTE PTR SEARCH_PATTERN,0
        JNE     SHORT EDIT_FIND_NEXT_READY
        CALL    PREFILL_FIND_WORD       ; DOS 5: no search yet, the word here
        CMP     BYTE PTR SEARCH_PATTERN,0
        JNE     SHORT EDIT_FIND_HERE
        JMP     EDIT_REDRAW
EDIT_FIND_HERE:
        JMP     EDIT_FIND_SEARCH
EDIT_FIND_NEXT_READY:
        CALL    CURSOR_GLOBAL
        MOV     FIND_ORIGIN,AX
        MOV     FIND_ORIGIN+2,DX
        CALL    CURSOR_GLOBAL_NEXT
        CALL    FIND_AND_SELECT
        JNC     SHORT EDIT_FIND_NEXT_FOUND
        MOV     AX,FIND_ORIGIN
        MOV     DX,FIND_ORIGIN+2
        CALL    SET_CURSOR_GLOBAL
        JMP     SHORT EDIT_FIND_NEXT_MISSING
EDIT_FIND_NEXT_FOUND:
        JMP     EDIT_REDRAW
EDIT_FIND_NEXT_MISSING:
        CALL    SHOW_NOT_FOUND
        JMP     EDIT_REDRAW

EDIT_REPLACE:
        CALL    DRAW_SCREEN
        CALL    PROMPT_REPLACE
        OR      AL,AL
        JNZ     SHORT EDIT_REPLACE_ACCEPTED
        JMP     EDIT_REDRAW
EDIT_REPLACE_ACCEPTED:
        CMP     BYTE PTR REPLACE_ALL_REQUESTED,0
        JE      SHORT EDIT_REPLACE_ONE
        CALL    REPLACE_ALL
        JC      EDIT_REPLACE_MISSING
        CMP     BYTE PTR MEMORY_FULL,0  ; too large a growth: nothing changed
        JNE     EDIT_REPLACE_DONE
        CALL    SHOW_CHANGE_COMPLETE
EDIT_REPLACE_DONE:
        JMP     EDIT_REDRAW
EDIT_REPLACE_ONE:                       ; Find and Verify
        CALL    CHANGE_VERIFY
        JMP     EDIT_REDRAW
EDIT_REPLACE_MISSING:
        CALL    SHOW_NOT_FOUND
        JMP     EDIT_REDRAW

EDIT_MARK_BLOCK:
        MOV     AX,CURSOR_OFFSET
        MOV     BLOCK_MARK,AX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     BYTE PTR SHIFT_SELECTION,0
        JMP     EDIT_REDRAW

EDIT_COPY_BLOCK:
        CALL    COPY_BLOCK
        JNC     SHORT EDIT_COPY_BLOCK_DONE
        JMP     EDIT_MEMORY_ERROR
EDIT_COPY_BLOCK_DONE:
        JMP     EDIT_REDRAW

EDIT_CUT_BLOCK:
        CALL    CUT_BLOCK
        JNC     SHORT EDIT_CUT_BLOCK_DONE
        JMP     EDIT_MEMORY_ERROR
EDIT_CUT_BLOCK_DONE:
        JMP     EDIT_REDRAW

EDIT_CLEAR_BLOCK:
        CALL    CLEAR_BLOCK
        JNC     SHORT EDIT_CLEAR_BLOCK_DONE
        JMP     EDIT_LOOP
EDIT_CLEAR_BLOCK_DONE:
        JMP     EDIT_REDRAW

EDIT_PASTE_BLOCK:
        MOV     AX,CLIP_LEN
        OR      AX,CLIP_LEN+2
        JZ      SHORT EDIT_PASTE_READY
        CALL    DELETE_SELECTION        ; pasted text replaces a selection
        CALL    MATERIALIZE_VIRTUAL_COLUMN
EDIT_PASTE_READY:
        CALL    RESET_NAVIGATION_COLUMNS
        CALL    PASTE_BLOCK
        JNC     SHORT EDIT_PASTE_BLOCK_DONE
        JMP     EDIT_MEMORY_ERROR
EDIT_PASTE_BLOCK_DONE:
        JMP     EDIT_REDRAW

EDIT_EXIT_REQUEST:
        CMP     BYTE PTR MODIFIED,0
        JE      EDIT_EXIT
        CALL    CONFIRM_EXIT
        CMP     AL,0
        JNE     SHORT EDIT_EXIT_CONFIRMED
        JMP     EDIT_REDRAW              ; Cancel/Esc: take the dialog down
EDIT_EXIT_CONFIRMED:
        CMP     AL,2
        JE      EDIT_EXIT
        CALL    SAVE_FILE
        JNC     SHORT EDIT_EXIT
        JMP     EDIT_SAVE_ERROR

EDIT_EXIT:
        CALL    INI_SAVE
        CMP     BYTE PTR DOSUI_MOUSE_PRESENT,0
        JE      SHORT EDIT_EXIT_SCREEN
        MOV     AX,0021H                ; DOS 5: a software reset at exit
        INT     33H
EDIT_EXIT_SCREEN:
        CALL    DOSUI_SHUTDOWN
        CALL    FREE_MEMORY
        MOV     AX,4C00H
        INT     21H

EDIT_REDRAW:
        CMP     BYTE PTR MEMORY_FULL,0  ; a command ran out of memory
        JE      SHORT EDIT_REDRAW_SCREEN
        MOV     BYTE PTR MEMORY_FULL,0
        JMP     EDIT_MEMORY_ERROR
EDIT_REDRAW_SCREEN:
        CALL    DRAW_SCREEN
        JMP     EDIT_LOOP

; Fast path for pure cursor movement inside an unchanged viewport with no
; active or previous selection/search highlight: updates only the status-bar
; row:col field, the vertical scrollbar thumb cell (if moved), and the
; hardware cursor, avoiding a 2,000-cell redraw on every arrow key.
EDIT_CURSOR_REDRAW:
        CMP     BYTE PTR MEMORY_FULL,0
        JNE     SHORT EDIT_REDRAW
        CMP     BYTE PTR DRAWN_VALID,0
        JE      SHORT EDIT_REDRAW
        CMP     BYTE PTR HELP_OPEN,0
        JNE     SHORT EDIT_REDRAW
        CMP     BYTE PTR STARTUP_ERROR,0
        JNE     SHORT EDIT_REDRAW
        CMP     BYTE PTR DRAWN_HAD_HIGHLIGHT,0
        JNE     SHORT EDIT_REDRAW
        CMP     WORD PTR BLOCK_MARK,0FFFFH
        JNE     SHORT EDIT_REDRAW
        CMP     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        JNE     SHORT EDIT_REDRAW
        CALL    CALCULATE_POSITION
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT EDIT_CURSOR_HAVE_COL
        MOV     AX,VIRTUAL_COLUMN
        MOV     CURRENT_COLUMN,AX
EDIT_CURSOR_HAVE_COL:
        CALL    UPDATE_LEFT_COLUMN
        MOV     AX,LEFT_COLUMN
        CMP     AX,DRAWN_LEFT_COLUMN
        JNE     SHORT EDIT_REDRAW_SCREEN
        MOV     AX,CURRENT_LINE
        MOV     BX,DRAWN_TOP_LINE
        CMP     AX,BX
        JB      SHORT EDIT_REDRAW_SCREEN
        ADD     BX,TEXT_ROW_COUNT
        CMP     AX,BX
        JAE     SHORT EDIT_REDRAW_SCREEN
        XOR     AL,AL
        CALL    SET_ROW_OFFSET
        CALL    UPDATE_SCROLL_THUMB
        CMP     BYTE PTR VBAR_SHOWN,0
        JE      SHORT EDIT_CURSOR_STATUS
        MOV     AL,SCROLL_THUMB_ROW
        CMP     AL,DRAWN_THUMB_ROW
        JE      SHORT EDIT_CURSOR_STATUS
        MOV     DH,DRAWN_THUMB_ROW
        MOV     DL,79
        MOV     AL,0B0H
        MOV     BL,70H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     DH,SCROLL_THUMB_ROW
        MOV     DRAWN_THUMB_ROW,DH
        MOV     AL,' '
        MOV     BL,07H
        CALL    DOSUI_FILL_ROW
EDIT_CURSOR_STATUS:
        CALL    FORMAT_STATUS_POSITION
        MOV     SI,OFFSET STATUS_POSITION_TEXT
        MOV     DH,STATUS_ROW
        MOV     DL,70
        MOV     BL,30H
        CALL    DOSUI_WRITE_Z
        MOV     AX,CURRENT_LINE
        SUB     AX,TOP_LINE
        ADD     AL,TEXT_FIRST_ROW
        MOV     DH,AL
        MOV     AX,CURRENT_COLUMN
        SUB     AX,LEFT_COLUMN
        INC     AX
        MOV     DL,AL
        CALL    DOSUI_SET_CURSOR
        JMP     EDIT_LOOP

; Route one main-loop click (DH = cell row, DL = cell column) to the same
; handlers the keyboard reaches: the menu bar, the status-bar help
; shortcut, the scroll rails, and caret placement in the text viewport.
EDIT_MOUSE:
        MOV     MOUSE_CLICK_CELL,DX
        MOV     AL,MOUSE_REPEATING      ; a held press again (MOUSE_REPEAT_WAIT)
        MOV     MOUSE_THIS_REPEAT,AL
        MOV     BYTE PTR MOUSE_REPEATING,0
        MOV     AH,02H                  ; Shift+click extends a selection
        INT     16H
        MOV     MODIFIER_STATE,AL
        OR      DH,DH
        JNZ     SHORT EDIT_MOUSE_NOT_MENU_BAR
        CALL    MENU_FROM_BAR_COLUMN
        CMP     AL,0FFH
        JNE     SHORT EDIT_MOUSE_OPEN_MENU
        JMP     EDIT_LOOP
EDIT_MOUSE_OPEN_MENU:
        JMP     EDIT_MENU_SYSTEM
EDIT_MOUSE_NOT_MENU_BAR:
        CMP     DH,STATUS_HIT_ROW
        JNE     SHORT EDIT_MOUSE_NOT_STATUS
        CMP     DL,16                   ; the <F1=Help> status shortcut
        JB      SHORT EDIT_MOUSE_MISS
        CMP     DL,24
        JA      SHORT EDIT_MOUSE_MISS
        JMP     EDIT_HELP
EDIT_MOUSE_MISS:
        JMP     EDIT_LOOP
EDIT_MOUSE_NOT_STATUS:
        CMP     BYTE PTR HELP_OPEN,0    ; with Help open: its window, the
        JE      SHORT EDIT_MOUSE_IN_WINDOW      ; editor's title rule, or the
        MOV     AL,HELP_WINDOW_ROWS     ; editor's window below it
        INC     AL
        CMP     DH,AL
        JA      SHORT EDIT_MOUSE_NOT_HELP
        MOV     BYTE PTR HELP_ACTIVE,1  ; Help becomes active, takes the click
        MOV     BYTE PTR HELP_PENDING_CLICK,1
        MOV     HELP_CLICK_CELL,DX
        JMP     EDIT_REDRAW
EDIT_MOUSE_NOT_HELP:
        INC     AL
        CMP     DH,AL
        JNE     SHORT EDIT_MOUSE_SHIFT_ROW
        JMP     EDIT_MOUSE_BORDER
EDIT_MOUSE_SHIFT_ROW:
        SUB     DH,EDITOR_ROW_SHIFT     ; the window's own rows from here
EDIT_MOUSE_IN_WINDOW:
        CMP     BYTE PTR BARS_SHOWN,0   ; without them: frame and text only
        JE      SHORT EDIT_MOUSE_NOT_HORIZONTAL
        CMP     DH,SCROLL_ROW           ; horizontal rail arrows
        JNE     SHORT EDIT_MOUSE_NOT_HORIZONTAL
        CMP     DL,1                    ; the arrows move the view a column
        JNE     SHORT EDIT_MOUSE_NOT_LEFT_ARROW
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_SCROLL_LEFT_ONE
EDIT_MOUSE_NOT_LEFT_ARROW:
        CMP     DL,78
        JNE     SHORT EDIT_MOUSE_TRACK
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_SCROLL_RIGHT_ONE
EDIT_MOUSE_TRACK:                       ; the track: a window sideways
        JA      SHORT EDIT_MOUSE_MISS
        MOV     AX,LEFT_COLUMN          ; the thumb: 2 + left * 75 / 178
        CMP     AX,LEFT_COLUMN_MAX
        JBE     SHORT EDIT_MOUSE_TRACK_LEFT
        MOV     AX,LEFT_COLUMN_MAX
EDIT_MOUSE_TRACK_LEFT:
        PUSH    DX
        MOV     CX,75
        MUL     CX
        MOV     CX,LEFT_COLUMN_MAX
        DIV     CX
        POP     DX
        ADD     AL,2
        CMP     DL,AL
        JE      SHORT EDIT_MOUSE_MISS
        JA      SHORT EDIT_MOUSE_TRACK_RIGHT
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_PAGE_LEFT
EDIT_MOUSE_TRACK_RIGHT:
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_PAGE_RIGHT
EDIT_MOUSE_WINDOW_MISS:
        JMP     EDIT_LOOP
EDIT_MOUSE_NOT_HORIZONTAL:
        CMP     DH,TEXT_FIRST_ROW
        JB      SHORT EDIT_MOUSE_WINDOW_MISS
        CMP     DH,TEXT_BOTTOM_ROW
        JA      SHORT EDIT_MOUSE_WINDOW_MISS
        CMP     DL,79                   ; vertical scroll bar
        JNE     SHORT EDIT_MOUSE_TEXT
        CMP     BYTE PTR VBAR_SHOWN,0
        JE      SHORT EDIT_MOUSE_WINDOW_MISS
        CMP     DH,TEXT_FIRST_ROW       ; the arrows scroll the view a line,
        JNE     SHORT EDIT_MOUSE_NOT_SCROLL_UP  ; as Ctrl+W and Ctrl+Z do
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_SCROLL_UP
EDIT_MOUSE_NOT_SCROLL_UP:
        CMP     DH,TEXT_LAST_ROW
        JNE     SHORT EDIT_MOUSE_NOT_SCROLL_DOWN
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_SCROLL_DOWN
EDIT_MOUSE_NOT_SCROLL_DOWN:
        CMP     DH,SCROLL_THUMB_ROW
        JB      SHORT EDIT_MOUSE_TRACK_UP
        JNE     SHORT EDIT_MOUSE_TRACK_DOWN
        CMP     BYTE PTR MOUSE_THIS_REPEAT,0    ; paging reached the pointer
        JE      SHORT EDIT_MOUSE_THUMB_PRESSED
        JMP     EDIT_LOOP
EDIT_MOUSE_THUMB_PRESSED:
        JMP     EDIT_MOUSE_THUMB
EDIT_MOUSE_TRACK_DOWN:
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_PAGE_DOWN
EDIT_MOUSE_TRACK_UP:
        CALL    MOUSE_REPEAT_ARM
        JMP     EDIT_PAGE_UP
EDIT_MOUSE_TEXT:
        OR      DL,DL                   ; the left rail is not text
        JNZ     SHORT EDIT_MOUSE_IN_TEXT
        JMP     EDIT_LOOP
EDIT_MOUSE_IN_TEXT:
        CALL    MOUSE_DOUBLE_CLICK      ; MOUSE_DOUBLE: a word is selected
        CALL    PREPARE_SHIFT_SELECTION
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     AL,DH
        SUB     AL,TEXT_FIRST_ROW
        XOR     AH,AH
        ADD     AX,TOP_LINE
        MOV     BL,DL
        DEC     BL
        XOR     BH,BH
        ADD     BX,LEFT_COLUMN
        MOV     DX,BX
        PUSH    DX
        CALL    MOVE_TO_LINE_COLUMN
        POP     DX
        JNC     SHORT EDIT_MOUSE_TEXT_DONE
        PUSH    DX                      ; below the last line: clamp to it
        CALL    FIND_LAST_LINE
        POP     DX
        CALL    MOVE_TO_LINE_COLUMN
EDIT_MOUSE_TEXT_DONE:
        CMP     BYTE PTR MOUSE_DOUBLE,0
        JNE     SHORT EDIT_MOUSE_WORD
        JMP     EDIT_MOUSE_DRAG
EDIT_MOUSE_WORD:
        CALL    SELECT_WORD_AT_CURSOR
        JMP     EDIT_REDRAW

; The editor's title rule under Help, dragged: Help's window ends above
; where it is let go; the editor's window becomes active (DOS 5, M2).
EDIT_MOUSE_BORDER:
        MOV     BYTE PTR HELP_ACTIVE,0
EDIT_BORDER_DRAW:
        CALL    DRAW_SCREEN
        CALL    DOSUI_END_FRAME
        MOV     AX,0001H
        INT     33H
EDIT_BORDER_POLL:
        MOV     AX,0003H
        INT     33H
        TEST    BX,1
        JZ      SHORT EDIT_BORDER_DONE
        MOV     AX,DX
        MOV     CL,3
        SHR     AX,CL
        SUB     AL,2                    ; Help's rows above the rule
        JNC     SHORT EDIT_BORDER_ROWS
        XOR     AL,AL
EDIT_BORDER_ROWS:
        MOV     AH,HELP_WINDOW_ROWS
        CALL    HELP_SET_ROWS
        CMP     AH,HELP_WINDOW_ROWS
        JE      SHORT EDIT_BORDER_IDLE
        MOV     AX,0002H
        INT     33H
        JMP     EDIT_BORDER_DRAW
EDIT_BORDER_IDLE:
        STI
        HLT
        JMP     EDIT_BORDER_POLL
EDIT_BORDER_DONE:
        MOV     AX,0002H
        INT     33H
        JMP     EDIT_REDRAW

; The horizontal scroll bar's arrows move the view a column; the cursor
; stays unless the view would leave it behind, then it keeps to the
; view's edge (DOS 5, QEMU capture M1, 2026-11-12).
EDIT_SCROLL_RIGHT_ONE:
        MOV     AX,LEFT_COLUMN
        CMP     AX,LEFT_COLUMN_MAX
        JB      SHORT EDIT_SCROLL_RIGHT_ROOM
        JMP     EDIT_LOOP
EDIT_SCROLL_RIGHT_ROOM:
        INC     AX
        MOV     LEFT_COLUMN,AX
        CALL    CURSOR_COLUMN
        CMP     AX,LEFT_COLUMN
        JAE     SHORT EDIT_SCROLL_SIDEWAYS_DONE
        MOV     AX,LEFT_COLUMN
        JMP     SHORT EDIT_SCROLL_SIDEWAYS_CURSOR
EDIT_SCROLL_LEFT_ONE:
        MOV     AX,LEFT_COLUMN
        OR      AX,AX
        JNZ     SHORT EDIT_SCROLL_LEFT_ROOM
        JMP     EDIT_LOOP
EDIT_SCROLL_LEFT_ROOM:
        DEC     AX
        MOV     LEFT_COLUMN,AX
        CALL    CURSOR_COLUMN
        MOV     BX,LEFT_COLUMN
        ADD     BX,77
        CMP     AX,BX
        JBE     SHORT EDIT_SCROLL_SIDEWAYS_DONE
        MOV     AX,BX
EDIT_SCROLL_SIDEWAYS_CURSOR:
        CALL    SET_CURSOR_COLUMN
EDIT_SCROLL_SIDEWAYS_DONE:
        JMP     EDIT_REDRAW

; A press in the text and a drag with the button down select, as in DOS 5;
; the view follows the pointer past the text's top or bottom edge, a line
; per clock tick.
EDIT_MOUSE_DRAG:
        CALL    HAS_SELECTION           ; Shift+click keeps its anchor
        JC      SHORT EDIT_DRAG_ANCHORED
        MOV     AX,CURSOR_OFFSET
        MOV     BLOCK_MARK,AX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     BYTE PTR SHIFT_SELECTION,1
EDIT_DRAG_ANCHORED:
        MOV     WORD PTR DRAG_LAST,0FFFFH
EDIT_DRAG_DRAW:
        CALL    NORMALIZE_VIEW          ; a long drag scrolls the view on
        CALL    DRAW_SCREEN
        CALL    DOSUI_END_FRAME
        MOV     AX,0001H
        INT     33H
EDIT_DRAG_POLL:
        MOV     AX,0003H
        INT     33H
        TEST    BX,1
        JNZ     SHORT EDIT_DRAG_HELD
        JMP     EDIT_DRAG_END
EDIT_DRAG_HELD:
        MOV     AX,DX                   ; AH = row, AL = column
        MOV     CL,3
        SHR     AX,CL
        MOV     AH,AL
        PUSH    AX
        MOV     AX,CX
        MOV     CL,3
        SHR     AX,CL
        POP     DX
        MOV     DL,AL
        MOV     AX,DX
        CALL    EDITOR_ROW_OF_AH
        CMP     AH,TEXT_FIRST_ROW       ; outside the text: keep scrolling
        JB      SHORT EDIT_DRAG_OUTSIDE
        CMP     AH,TEXT_LAST_ROW
        JA      SHORT EDIT_DRAG_OUTSIDE
        CMP     AX,DRAG_LAST
        JE      EDIT_DRAG_POLL
        JMP     SHORT EDIT_DRAG_MOVE
EDIT_DRAG_OUTSIDE:
        CALL    WAIT_CLOCK_TICK
EDIT_DRAG_MOVE:
        MOV     DRAG_LAST,AX
        PUSH    AX
        MOV     AX,0002H
        INT     33H
        CALL    RESET_NAVIGATION_COLUMNS
        POP     AX
        XOR     DX,DX                   ; DX = the column
        MOV     DL,AL
        OR      DL,DL
        JZ      SHORT EDIT_DRAG_COLUMN
        DEC     DX
        CMP     DL,77
        JBE     SHORT EDIT_DRAG_COLUMN
        MOV     DL,77
EDIT_DRAG_COLUMN:
        ADD     DX,LEFT_COLUMN
        MOV     AL,AH                   ; AX = the line
        XOR     AH,AH
        CMP     AL,TEXT_FIRST_ROW
        JAE     SHORT EDIT_DRAG_NOT_ABOVE
        MOV     AX,TOP_LINE
        OR      AX,AX
        JZ      SHORT EDIT_DRAG_LINE
        DEC     AX
        JMP     SHORT EDIT_DRAG_LINE
EDIT_DRAG_NOT_ABOVE:
        CMP     AL,TEXT_LAST_ROW
        JBE     SHORT EDIT_DRAG_INSIDE
        MOV     AX,TOP_LINE
        ADD     AX,TEXT_ROW_COUNT
        JMP     SHORT EDIT_DRAG_LINE
EDIT_DRAG_INSIDE:
        SUB     AL,TEXT_FIRST_ROW
        ADD     AX,TOP_LINE
EDIT_DRAG_LINE:
        PUSH    DX
        CALL    MOVE_TO_LINE_COLUMN
        POP     DX
        JNC     SHORT EDIT_DRAG_PLACED
        PUSH    DX                      ; below the last line: clamp to it
        CALL    FIND_LAST_LINE
        POP     DX
        CALL    MOVE_TO_LINE_COLUMN
EDIT_DRAG_PLACED:
        MOV     BYTE PTR SHIFT_SELECTION,1
        JMP     EDIT_DRAG_DRAW
EDIT_DRAG_END:
        MOV     AX,0002H
        INT     33H
        JMP     EDIT_LOOP

; The vertical thumb dragged: on release the cursor goes to the line its
; place stands for, keeping its column.
EDIT_MOUSE_THUMB:
        MOV     AX,0001H
        INT     33H
EDIT_THUMB_POLL:
        MOV     AX,0003H
        INT     33H
        TEST    BX,1
        JNZ     EDIT_THUMB_POLL
        PUSH    DX
        MOV     AX,0002H
        INT     33H
        POP     AX
        MOV     CL,3
        SHR     AX,CL                   ; the row released on
        MOV     AH,AL
        CALL    EDITOR_ROW_OF_AH
        MOV     AL,AH
        XOR     AH,AH
        CMP     AL,TEXT_FIRST_ROW+1
        JAE     SHORT EDIT_THUMB_NOT_ABOVE
        MOV     AL,TEXT_FIRST_ROW+1
EDIT_THUMB_NOT_ABOVE:
        SUB     AL,TEXT_FIRST_ROW+1
        XOR     AH,AH
        PUSH    AX
        CALL    TOTAL_LINE_ENDS         ; DX:AX = the last line of the text
        POP     CX
        CALL    MUL32_16
        MOV     CX,TEXT_ROWS
        SUB     CX,3
        CALL    DIV32_16
        PUSH    AX
        PUSH    DX
        CALL    CURSOR_COLUMN
        MOV     BX,AX
        POP     DX
        POP     AX
        CALL    VIEW_TO_LINE
        MOV     DX,BX
        PUSH    DX
        CALL    MOVE_TO_LINE_COLUMN
        POP     DX
        JNC     SHORT EDIT_THUMB_DONE
        CALL    FIND_LAST_LINE
        CALL    MOVE_TO_LINE_COLUMN
EDIT_THUMB_DONE:
        JMP     EDIT_REDRAW

; AH = a screen row: the row of the editor's window (0 above it).
EDITOR_ROW_OF_AH PROC NEAR
        SUB     AH,EDITOR_ROW_SHIFT
        JNC     SHORT EDITOR_ROW_OF_AH_DONE
        XOR     AH,AH
EDITOR_ROW_OF_AH_DONE:
        RET
EDITOR_ROW_OF_AH ENDP

; A menu command for the text makes the editor's window the active one;
; Display, Help Path, the help topics and About leave it as it is.
MENU_COMMAND_FOCUS PROC NEAR
        CMP     AL,COMMAND_NONE
        JE      SHORT MENU_COMMAND_FOCUS_DONE
        CMP     AL,COMMAND_EXIT
        JE      SHORT MENU_COMMAND_FOCUS_DONE
        CMP     AL,COMMAND_DISPLAY
        JB      SHORT MENU_COMMAND_FOCUS_EDITOR
        CMP     AL,COMMAND_ABOUT
        JBE     SHORT MENU_COMMAND_FOCUS_DONE
MENU_COMMAND_FOCUS_EDITOR:
        MOV     BYTE PTR HELP_ACTIVE,0
MENU_COMMAND_FOCUS_DONE:
        RET
MENU_COMMAND_FOCUS ENDP

; AX = the BIOS clock's tick count (its low word).
BIOS_TICKS PROC NEAR
        PUSH    DS
        MOV     AX,40H
        MOV     DS,AX
        MOV     AX,DS:[6CH]
        POP     DS
        RET
BIOS_TICKS ENDP

; A press on a scroll bar's arrow or track repeats while the button stays
; down on it, as in DOS 5 (QEMU capture M1, 2026-11-12): once at once, again
; after about 0.44 s (8 ticks), then about nine times a second (2 ticks).
MOUSE_REPEAT_ARM PROC NEAR
        PUSH    AX
        CALL    BIOS_TICKS
        ADD     AX,2
        CMP     BYTE PTR MOUSE_THIS_REPEAT,0
        JNE     SHORT MOUSE_REPEAT_ARM_SET
        ADD     AX,6
MOUSE_REPEAT_ARM_SET:
        MOV     MOUSE_REPEAT_DUE,AX
        MOV     AX,MOUSE_CLICK_CELL
        MOV     MOUSE_REPEAT_CELL,AX
        MOV     BYTE PTR MOUSE_REPEAT,1
        POP     AX
        RET
MOUSE_REPEAT_ARM ENDP

; Wait while the armed press is held. CF set: it repeats now, DX = its cell
; (screen rows). CF clear: the button came up or a key arrived. The pointer
; is shown while waiting; off the pressed cell nothing repeats.
MOUSE_REPEAT_WAIT PROC NEAR
        CALL    DOSUI_END_FRAME
        MOV     BYTE PTR MOUSE_REPEAT,0
        MOV     AX,0001H
        INT     33H
MOUSE_REPEAT_POLL:
        MOV     AH,11H
        INT     16H
        JNZ     SHORT MOUSE_REPEAT_STOP
        MOV     AX,0003H
        INT     33H
        TEST    BX,1
        JZ      SHORT MOUSE_REPEAT_STOP
        MOV     AX,CX
        MOV     CL,3
        SHR     AX,CL
        SHR     DX,CL
        MOV     AH,DL
        CMP     AX,MOUSE_REPEAT_CELL
        JNE     SHORT MOUSE_REPEAT_IDLE
        CALL    BIOS_TICKS
        SUB     AX,MOUSE_REPEAT_DUE
        JS      SHORT MOUSE_REPEAT_IDLE
        MOV     AX,0002H
        INT     33H
        MOV     DX,MOUSE_REPEAT_CELL
        STC
        RET
MOUSE_REPEAT_IDLE:
        STI
        HLT
        JMP     MOUSE_REPEAT_POLL
MOUSE_REPEAT_STOP:
        MOV     AX,0002H
        INT     33H
        CLC
        RET
MOUSE_REPEAT_WAIT ENDP

; MOUSE_DOUBLE = 1 when this press in the text (MOUSE_CLICK_CELL) is the
; second of a double click: the same cell within half a second.
MOUSE_DOUBLE_CLICK PROC NEAR
        PUSH    AX
        PUSH    BX
        CALL    BIOS_TICKS
        MOV     BX,AX
        SUB     AX,MOUSE_LAST_TICK
        CMP     AX,9
        JA      SHORT MOUSE_DOUBLE_FIRST
        MOV     AX,MOUSE_CLICK_CELL
        CMP     AX,MOUSE_LAST_CELL
        JNE     SHORT MOUSE_DOUBLE_FIRST
        MOV     WORD PTR MOUSE_LAST_CELL,0FFFFH ; a third press starts again
        MOV     BYTE PTR MOUSE_DOUBLE,1
        JMP     SHORT MOUSE_DOUBLE_DONE
MOUSE_DOUBLE_FIRST:
        MOV     MOUSE_LAST_TICK,BX
        MOV     AX,MOUSE_CLICK_CELL
        MOV     MOUSE_LAST_CELL,AX
        MOV     BYTE PTR MOUSE_DOUBLE,0
MOUSE_DOUBLE_DONE:
        POP     BX
        POP     AX
        RET
MOUSE_DOUBLE_CLICK ENDP

; A double click selects the word under the pointer, or the word it follows
; (a click past a line's end or on the blank after a word), the cursor at
; the word's end (DOS 5, QEMU capture M1, 2026-11-12). Word characters are
; Ctrl+T's (IS_WORD_BYTE).
SELECT_WORD_AT_CURSOR PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    DX
        MOV     BX,CURSOR_OFFSET
        CALL    WORD_BYTE_AT
        JC      SHORT SELECT_WORD_FOUND
        OR      BX,BX
        JZ      SHORT SELECT_WORD_DONE
        DEC     BX
        CALL    WORD_BYTE_AT
        JNC     SHORT SELECT_WORD_DONE
SELECT_WORD_FOUND:
        MOV     DX,BX                   ; on to the word's end
SELECT_WORD_END:
        INC     DX
        PUSH    BX
        MOV     BX,DX
        CALL    WORD_BYTE_AT
        POP     BX
        JC      SELECT_WORD_END
SELECT_WORD_START:                      ; back to its start
        OR      BX,BX
        JZ      SHORT SELECT_WORD_SET
        DEC     BX
        CALL    WORD_BYTE_AT
        JC      SELECT_WORD_START
        INC     BX
SELECT_WORD_SET:
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     BLOCK_MARK,BX
        MOV     CURSOR_OFFSET,DX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     BYTE PTR SHIFT_SELECTION,1
SELECT_WORD_DONE:
        POP     DX
        POP     BX
        POP     AX
        RET
SELECT_WORD_AT_CURSOR ENDP

; CF set: view offset BX holds a word character.
WORD_BYTE_AT PROC NEAR
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT WORD_BYTE_AT_NO
        PUSH    AX
        MOV     AL,BUFFER[BX]
        CALL    IS_WORD_BYTE
        POP     AX
        RET
WORD_BYTE_AT_NO:
        CLC
        RET
WORD_BYTE_AT ENDP

; Wait for the next BIOS clock tick (about 55 ms).
WAIT_CLOCK_TICK PROC NEAR
        PUSH    AX
        PUSH    DS
        MOV     AX,40H
        MOV     DS,AX
        MOV     AX,DS:[6CH]
WAIT_CLOCK_TICK_NEXT:
        STI
        HLT
        CMP     AX,DS:[6CH]
        JE      WAIT_CLOCK_TICK_NEXT
        POP     DS
        POP     AX
        RET
WAIT_CLOCK_TICK ENDP

; Carry means EDIT should print its usage and exit: /? or a switch DOS 5
; EDIT does not know (captured 2026-11-11: EDIT /X A.TXT prints the /? text
; and returns to DOS). Otherwise FILE_NAME is empty or holds the first token
; that is not a switch (63 bytes at most), and /B, /G, /H and /NOHI, in any
; case and order around the name, are taken. /G (fast CGA updates) changes
; nothing on a VGA, as in DOS 5; /B, /H and /NOHI are kept in EDIT_SWITCHES
; for SETUP_DISPLAY_MODE and SETUP_ROWS.
PARSE_COMMAND_LINE PROC NEAR
        XOR     BX,BX
        MOV     BL,BYTE PTR DS:[80H]
        MOV     SI,81H
PARSE_SKIP:
        OR      BX,BX
        JZ      PARSE_DONE
        CMP     BYTE PTR [SI],' '
        JE      SHORT PARSE_BLANK
        CMP     BYTE PTR [SI],9
        JNE     SHORT PARSE_TOKEN
PARSE_BLANK:
        INC     SI
        DEC     BX
        JMP     PARSE_SKIP
PARSE_TOKEN:
        CMP     BYTE PTR [SI],'/'
        JNE     SHORT PARSE_NAME
        CALL    PARSE_SWITCH
        JNC     PARSE_SKIP
        RET                             ; carry: print the usage
PARSE_NAME:
        CMP     BYTE PTR FILE_NAME,0
        JNE     SHORT PARSE_SKIP_NAME   ; a second name is ignored
        MOV     DI,OFFSET FILE_NAME
        MOV     CX,63
PARSE_COPY_NEXT:
        OR      BX,BX
        JZ      SHORT PARSE_TERMINATE
        MOV     AL,[SI]
        CMP     AL,' '
        JE      SHORT PARSE_TERMINATE
        CMP     AL,9
        JE      SHORT PARSE_TERMINATE
        CMP     AL,'/'
        JE      SHORT PARSE_TERMINATE
        STOSB
        INC     SI
        DEC     BX
        LOOP    PARSE_COPY_NEXT
PARSE_TERMINATE:
        MOV     AL,0
        STOSB
        JMP     PARSE_SKIP
PARSE_SKIP_NAME:
        OR      BX,BX
        JZ      SHORT PARSE_DONE
        MOV     AL,[SI]
        CMP     AL,' '
        JE      PARSE_SKIP
        CMP     AL,9
        JE      PARSE_SKIP
        CMP     AL,'/'
        JE      PARSE_SKIP
        INC     SI
        DEC     BX
        JMP     PARSE_SKIP_NAME
PARSE_DONE:
        CLC
        RET
PARSE_COMMAND_LINE ENDP

; SI at '/', BX bytes left: take one switch and advance past it. Carry for
; /? or an unknown switch.
PARSE_SWITCH PROC NEAR
        INC     SI
        DEC     BX
        MOV     DI,OFFSET SWITCH_WORD
        XOR     CX,CX
PARSE_SWITCH_READ:
        OR      BX,BX
        JZ      SHORT PARSE_SWITCH_READ_DONE
        MOV     AL,[SI]
        CMP     AL,' '
        JE      SHORT PARSE_SWITCH_READ_DONE
        CMP     AL,9
        JE      SHORT PARSE_SWITCH_READ_DONE
        CMP     AL,'/'
        JE      SHORT PARSE_SWITCH_READ_DONE
        CMP     AL,'a'
        JB      SHORT PARSE_SWITCH_STORE
        CMP     AL,'z'
        JA      SHORT PARSE_SWITCH_STORE
        SUB     AL,20H
PARSE_SWITCH_STORE:
        CMP     CX,4
        JAE     SHORT PARSE_SWITCH_LONG
        MOV     [DI],AL
        INC     DI
PARSE_SWITCH_LONG:
        INC     CX
        INC     SI
        DEC     BX
        JMP     PARSE_SWITCH_READ
PARSE_SWITCH_READ_DONE:
        MOV     AL,1                    ; /B
        CMP     CX,1
        JNE     SHORT PARSE_SWITCH_NOHI
        MOV     AH,SWITCH_WORD
        CMP     AH,'B'
        JE      SHORT PARSE_SWITCH_TAKE
        MOV     AL,2                    ; /G
        CMP     AH,'G'
        JE      SHORT PARSE_SWITCH_TAKE
        MOV     AL,4                    ; /H
        CMP     AH,'H'
        JE      SHORT PARSE_SWITCH_TAKE
        STC                             ; /? or another letter
        RET
PARSE_SWITCH_NOHI:
        CMP     CX,4
        JNE     SHORT PARSE_SWITCH_BAD
        CMP     WORD PTR SWITCH_WORD,'ON'
        JNE     SHORT PARSE_SWITCH_BAD
        CMP     WORD PTR SWITCH_WORD+2,'IH'
        JNE     SHORT PARSE_SWITCH_BAD
        MOV     AL,8                    ; /NOHI
PARSE_SWITCH_TAKE:
        OR      EDIT_SWITCHES,AL
        CLC
        RET
PARSE_SWITCH_BAD:
        STC
        RET
PARSE_SWITCH ENDP

LOAD_FILE PROC NEAR
        MOV     BYTE PTR TAB_FILE,0
        CMP     BYTE PTR FILE_NAME,0
        JNE     SHORT LOAD_NAMED
        JMP     LOAD_DONE
LOAD_NAMED:
        MOV     SI,OFFSET FILE_NAME
        CALL    CHECK_FILE_NAME
        JC      SHORT LOAD_REFUSED
        MOV     SI,OFFSET FILE_NAME
        CALL    CHECK_FILE_DRIVE
        JNC     SHORT LOAD_ATTRIBUTES
LOAD_REFUSED:
        MOV     BYTE PTR STARTUP_ERROR,1
        RET
LOAD_ATTRIBUTES:
        MOV     DX,OFFSET FILE_NAME
        MOV     AX,4300H
        INT     21H
        JC      LOAD_OPEN_REQUEST
        TEST    CL,1
        JZ      LOAD_OPEN_REQUEST
        MOV     BYTE PTR SAVE_DISABLED,1
LOAD_OPEN_REQUEST:
        MOV     DX,OFFSET FILE_NAME
        MOV     AX,3D00H
        INT     21H
        JNC     LOAD_OPEN
        CALL    NOTE_DOS_ERROR          ; 3 for a missing directory, as DOS 5
        CMP     WORD PTR FILE_ERROR,2
        JE      LOAD_DONE                 ; a named new file
        MOV     BYTE PTR STARTUP_ERROR,1
        RET
LOAD_OPEN:
        MOV     BX,AX
        CALL    LOAD_TEXT
        JNC     LOAD_CLOSE
        MOV     BYTE PTR STARTUP_ERROR,1
LOAD_CLOSE:
        MOV     AH,3EH
        INT     21H
LOAD_DONE:
        RET
LOAD_FILE ENDP

; DOS 5 EDIT keeps no tab characters in its text. Loading a file turns every
; tab into the blanks up to the next Tab Stops column (QBASIC.INI or Options
; Display), so a tab shows as that run of cells and the cursor moves through
; it a column at a time; when the file had any tab at all, saving writes
; each line's leading blanks back as tabs plus the remaining spaces
; (SAVE_TEXT_BUFFER), and a file without one keeps its blanks (plans TB5,
; TB6, BS2: "a<tab>b" makes eight leading spaces save as a tab, a file of
; spaces only saves them as spaces). Captured on DOS 5 under
; QEMU, 2026-11-11, with Tab Stops 8 and 4: "a<tab>b" saves as "a" and
; blanks to the stop, eight leading spaces save as a tab, "  <tab>W" as
; "<tab>W", and typing inside a leading tab leaves spaces before the typed
; character. LOAD_TEXT and EXPAND_LOADED_TABS (with the document's memory)
; do the loading.

; EDIT /B and /NOHI, as DOS 5 EDIT shows them on a VGA (captured under QEMU,
; 2026-11-11). Every
; menu, dialog, status and frame attribute DOS 5 uses in colour maps to one
; attribute of the mode, cell for cell over 48 screens (normal against /B,
; /NOHI and /B /NOHI): DOSUI recolours what EDIT draws through that table,
; while the text colours the user chose are written raw. /B makes the text
; white on black whatever QBASIC.INI says and offers Black, White, Gray and
; BrWhite; /B /NOHI only Black and White; /NOHI takes QBASIC.INI's colours
; and offers the eight dim ones. QBASIC.INI is written with each mode's own
; statement and breakpoint colour sets. Boxes only CK-DOS draws (its help,
; About and message boxes in 1Fh/4Fh/2Fh) take the mode's equivalent.
SETUP_DISPLAY_MODE PROC NEAR
        MOV     AL,EDIT_SWITCHES
        AND     AL,9                    ; /B and /NOHI
        JNZ     SHORT SETUP_MODE_ON
        RET
SETUP_MODE_ON:
        MOV     DI,OFFSET MODE_MAP
        PUSH    DS
        POP     ES
        XOR     AX,AX
SETUP_MODE_IDENTITY:
        STOSB
        INC     AL
        JNZ     SETUP_MODE_IDENTITY
        MOV     AL,EDIT_SWITCHES
        AND     AL,9
        MOV     SI,OFFSET MONO_PAIRS
        MOV     BX,OFFSET MONO_COLOURS
        MOV     WORD PTR MODE_STATEMENT_SETS,OFFSET MONO_STATEMENT_SETS
        MOV     WORD PTR DISPLAY_FOREGROUND_COUNT,4
        CMP     AL,1
        JE      SHORT SETUP_MODE_PAIRS
        MOV     SI,OFFSET NOHI_PAIRS
        MOV     BX,OFFSET COLOR_NAMES
        MOV     WORD PTR MODE_STATEMENT_SETS,OFFSET NOHI_STATEMENT_SETS
        MOV     WORD PTR DISPLAY_FOREGROUND_COUNT,8
        CMP     AL,8
        JE      SHORT SETUP_MODE_PAIRS
        MOV     SI,OFFSET MONO_NOHI_PAIRS
        MOV     BX,OFFSET MONO_COLOURS
        MOV     WORD PTR MODE_STATEMENT_SETS,OFFSET MONO_NOHI_STATEMENT_SETS
        MOV     WORD PTR DISPLAY_FOREGROUND_COUNT,2
SETUP_MODE_PAIRS:
        MOV     AX,DISPLAY_FOREGROUND_COUNT
        CMP     AX,8
        JBE     SHORT SETUP_MODE_BACKGROUNDS
        MOV     AX,8
SETUP_MODE_BACKGROUNDS:
        MOV     DISPLAY_BACKGROUND_COUNT,AX
        MOV     DISPLAY_FOREGROUND_LIST,BX
        MOV     DISPLAY_BACKGROUND_LIST,BX
SETUP_MODE_PAIR_NEXT:
        LODSW                           ; AL = colour attribute, AH = mode's
        OR      AX,AX
        JZ      SHORT SETUP_MODE_MAPPED
        XOR     BX,BX
        MOV     BL,AL
        MOV     MODE_MAP[BX],AH
        JMP     SETUP_MODE_PAIR_NEXT
SETUP_MODE_MAPPED:
        MOV     WORD PTR DOSUI_ATTR_MAP,OFFSET MODE_MAP
        TEST    BYTE PTR EDIT_SWITCHES,1
        JZ      SHORT SETUP_MODE_DONE
        MOV     BYTE PTR DOSUI_SHADOW_ATTR,07H  ; /B: shadows 08h -> 07h
        MOV     AL,07H                  ; white on black
        CALL    SET_TEXT_ATTRIBUTE
SETUP_MODE_DONE:
        RET
SETUP_DISPLAY_MODE ENDP

; Write a cell run in the user's text colours (BL = TEXT_ATTR or SELECT_ATTR),
; past the display mode's attribute map.
TEXT_FILL_ROW PROC NEAR
        MOV     BYTE PTR DOSUI_ATTR_RAW,1
        CALL    DOSUI_FILL_ROW
        MOV     BYTE PTR DOSUI_ATTR_RAW,0
        RET
TEXT_FILL_ROW ENDP

; AL = the text attribute the Options Display lists select: the chosen
; background and foreground; outside /B the background intensity a
; QBASIC.INI may hold has no control and stays.
DISPLAY_SELECTED_ATTR PROC NEAR
        PUSH    CX
        PUSH    DX
        MOV     AX,DISPLAY_FOREGROUND
        CALL    DISPLAY_INDEX_COLOUR
        MOV     DX,AX
        MOV     AX,DISPLAY_BACKGROUND
        CALL    DISPLAY_INDEX_COLOUR
        MOV     CL,4
        SHL     AL,CL
        OR      AX,DX
        TEST    BYTE PTR EDIT_SWITCHES,1
        JNZ     SHORT DISPLAY_SELECTED_DONE     ; /B lists BrWhite itself
        MOV     AH,TEXT_ATTR
        AND     AH,80H
        OR      AL,AH
DISPLAY_SELECTED_DONE:
        POP     DX
        POP     CX
        RET
DISPLAY_SELECTED_ATTR ENDP

; "Set colors for the text editor window:" is DOS 5's sample of the colours
; the two lists select, redrawn as either moves (plans DSB, DSN: the twenty
; cells of rows 8 and 9 from column 17 take the selection at once).
DISPLAY_FOREGROUND_MOVED PROC NEAR
        PUSH    AX
        PUSH    DX
        CALL    DISPLAY_SELECTED_ATTR
        MOV     DL,DISPLAY_ATTR
        AND     DL,0F0H
        AND     AL,0FH
        OR      AL,DL
        MOV     DISPLAY_ATTR,AL
        POP     DX
        POP     AX
        JMP     SHORT DISPLAY_PREVIEW
DISPLAY_FOREGROUND_MOVED ENDP

DISPLAY_BACKGROUND_MOVED PROC NEAR
        PUSH    AX
        PUSH    DX
        CALL    DISPLAY_SELECTED_ATTR
        MOV     DL,DISPLAY_ATTR
        AND     DL,0FH
        AND     AL,0F0H
        OR      AL,DL
        MOV     DISPLAY_ATTR,AL
        POP     DX
        POP     AX
DISPLAY_BACKGROUND_MOVED ENDP

DISPLAY_PREVIEW PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     BL,DISPLAY_ATTR
        MOV     BYTE PTR DOSUI_ATTR_RAW,1
        MOV     AL,' '
        MOV     CX,20
        MOV     DH,8
        MOV     DL,17
        CALL    DOSUI_FILL_ROW
        INC     DH
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET DISPLAY_PREVIEW_LINE1
        MOV     DH,8
        MOV     DL,18
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET DISPLAY_PREVIEW_LINE2
        INC     DH
        CALL    DOSUI_WRITE_Z
        MOV     BYTE PTR DOSUI_ATTR_RAW,0
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
DISPLAY_PREVIEW ENDP

; AX = a colour list index -> AX = the colour it means (the /B lists hold
; 0, 7, 8 and 15; the others are the colour itself).
DISPLAY_INDEX_COLOUR PROC NEAR
        TEST    BYTE PTR EDIT_SWITCHES,1
        JZ      SHORT DISPLAY_INDEX_DONE
        PUSH    BX
        MOV     BX,AX
        MOV     AL,MONO_VALUES[BX]
        XOR     AH,AH
        POP     BX
DISPLAY_INDEX_DONE:
        RET
DISPLAY_INDEX_COLOUR ENDP

; AX = a colour -> AX = its index in the mode's list (0 when it has none).
DISPLAY_COLOUR_INDEX PROC NEAR
        TEST    BYTE PTR EDIT_SWITCHES,1
        JZ      SHORT DISPLAY_COLOUR_PLAIN
        PUSH    BX
        XOR     BX,BX
DISPLAY_COLOUR_FIND:
        CMP     BX,DISPLAY_FOREGROUND_COUNT
        JAE     SHORT DISPLAY_COLOUR_NONE
        CMP     AL,MONO_VALUES[BX]
        JE      SHORT DISPLAY_COLOUR_FOUND
        INC     BX
        JMP     DISPLAY_COLOUR_FIND
DISPLAY_COLOUR_NONE:
        XOR     BX,BX
DISPLAY_COLOUR_FOUND:
        MOV     AX,BX
        POP     BX
        RET
DISPLAY_COLOUR_PLAIN:
        CMP     AX,DISPLAY_FOREGROUND_COUNT
        JB      SHORT DISPLAY_COLOUR_KEPT
        AND     AX,7                    ; /NOHI lists only the dim eight
DISPLAY_COLOUR_KEPT:
        RET
DISPLAY_COLOUR_INDEX ENDP

; QBASIC.INI keeps Options Display (and DOS 5's help path) across sessions,
; in the binary layout real DOS 5 EDIT (QBASIC /EDITOR) writes: "QBP",4;
; three colour sets of background, foreground, foreground-intensity and
; background-intensity words (normal text, then QBasic's current-statement
; and breakpoint sets); the Tab Stops and Scroll Bars words; 13 bytes of
; QBasic options; the help path's length and NUL-terminated text. Observed
; in DOS 5 under QEMU (2026-11-11): the file is read once at start from the
; program's directory, else the current directory, else each PATH
; directory; the first file found is the only one read, and one without
; the "QBP",4 header leaves the defaults, while a short one supplies the
; fields it holds. It is written at exit only after
; an Options Display OK, always to the program's directory, or to the
; current directory when that file cannot be created; nothing is reported
; when neither can be created or the disk is full. The text buffer is free
; while this runs, so the file is read there.
INI_LOAD PROC NEAR
        CALL    INI_PROGRAM_PATH
        MOV     DX,OFFSET INI_PATH
        CALL    INI_TRY_READ
        JNC     SHORT INI_LOAD_DONE
        MOV     DX,OFFSET INI_FILE_NAME
        CALL    INI_TRY_READ
        JNC     SHORT INI_LOAD_DONE
        MOV     ES,DS:[2CH]             ; PATH= in the environment
        XOR     SI,SI
INI_LOAD_FIND_PATH:
        CMP     BYTE PTR ES:[SI],0
        JE      SHORT INI_LOAD_RESTORE
        MOV     DI,OFFSET INI_PATH_VARIABLE
        MOV     CX,5
INI_LOAD_COMPARE:
        MOV     AL,ES:[SI]
        CMP     AL,[DI]
        JNE     SHORT INI_LOAD_SKIP
        INC     SI
        INC     DI
        LOOP    INI_LOAD_COMPARE
INI_LOAD_NEXT_DIRECTORY:
        MOV     DI,OFFSET INI_SCRATCH_PATH
        MOV     BX,DI
INI_LOAD_COPY_DIRECTORY:
        MOV     AL,ES:[SI]
        OR      AL,AL
        JZ      SHORT INI_LOAD_DIRECTORY_READY
        INC     SI
        CMP     AL,';'
        JE      SHORT INI_LOAD_DIRECTORY_READY
        CMP     DI,OFFSET INI_SCRATCH_PATH+63
        JAE     SHORT INI_LOAD_COPY_DIRECTORY
        MOV     [DI],AL
        INC     DI
        JMP     INI_LOAD_COPY_DIRECTORY
INI_LOAD_DIRECTORY_READY:
        CMP     DI,BX
        JE      SHORT INI_LOAD_DIRECTORY_DONE   ; an empty PATH entry
        MOV     AL,[DI-1]
        CMP     AL,'\'
        JE      SHORT INI_LOAD_DIRECTORY_NAME
        CMP     AL,':'
        JE      SHORT INI_LOAD_DIRECTORY_NAME
        MOV     BYTE PTR [DI],'\'
        INC     DI
INI_LOAD_DIRECTORY_NAME:
        CALL    INI_APPEND_NAME
        PUSH    ES
        PUSH    SI
        PUSH    DS
        POP     ES
        MOV     DX,OFFSET INI_SCRATCH_PATH
        CALL    INI_TRY_READ
        POP     SI
        POP     ES
        JNC     SHORT INI_LOAD_RESTORE
INI_LOAD_DIRECTORY_DONE:
        CMP     BYTE PTR ES:[SI],0
        JNE     INI_LOAD_NEXT_DIRECTORY
        JMP     SHORT INI_LOAD_RESTORE
INI_LOAD_SKIP:
        LODS    BYTE PTR ES:[SI]
        OR      AL,AL
        JNZ     INI_LOAD_SKIP
        JMP     INI_LOAD_FIND_PATH
INI_LOAD_RESTORE:
        PUSH    DS
        POP     ES
INI_LOAD_DONE:
        RET
INI_LOAD ENDP

; Read the QBASIC.INI named at DX. CF set: it cannot be opened, and the
; search goes on. Otherwise the search ends here: with the "QBP",4 header
; the bytes it holds replace the leading part of the settings image and the
; settings take effect; without it (an empty file included) DOS 5's
; defaults stay.
INI_TRY_READ PROC NEAR
        MOV     AX,3D00H
        INT     21H
        JC      SHORT INI_TRY_READ_DONE
        MOV     BX,AX
        MOV     DX,OFFSET SCRATCH
        MOV     CX,INI_IMAGE_MAX
        MOV     AH,3FH
        INT     21H
        PUSHF
        PUSH    AX
        MOV     AH,3EH
        INT     21H
        POP     CX
        POPF
        JC      SHORT INI_TRY_READ_ABSENT
        CMP     CX,4
        JB      SHORT INI_TRY_READ_ABSENT
        MOV     SI,OFFSET SCRATCH
        MOV     DI,OFFSET INI_IMAGE
        CMPSW
        JNE     SHORT INI_TRY_READ_ABSENT
        CMPSW
        JNE     SHORT INI_TRY_READ_ABSENT
        MOV     SI,OFFSET SCRATCH
        MOV     DI,OFFSET INI_IMAGE
        REP     MOVSB
        CALL    INI_APPLY
INI_TRY_READ_ABSENT:
        CLC
INI_TRY_READ_DONE:
        RET
INI_TRY_READ ENDP

; Take the editor's settings from the image. DOS 5 combines each colour
; with its intensity word as-is; a Tab Stops value of zero, which DOS 5
; only meets with a "Division by zero" at the next Tab, is not taken.
INI_APPLY PROC NEAR
        TEST    BYTE PTR EDIT_SWITCHES,1
        JNZ     SHORT INI_APPLY_COLOURS_DONE    ; /B: white on black (DSBI)
        MOV     AL,BYTE PTR INI_BACKGROUND
        CMP     WORD PTR INI_BACKGROUND_BRIGHT,0
        JE      SHORT INI_APPLY_BACKGROUND
        OR      AL,8
INI_APPLY_BACKGROUND:
        MOV     CL,4
        SHL     AL,CL
        MOV     AH,BYTE PTR INI_FOREGROUND
        CMP     WORD PTR INI_FOREGROUND_BRIGHT,0
        JE      SHORT INI_APPLY_FOREGROUND
        OR      AH,8
INI_APPLY_FOREGROUND:
        AND     AH,0FH
        OR      AL,AH
        CALL    SET_TEXT_ATTRIBUTE
INI_APPLY_COLOURS_DONE:
        MOV     AL,1
        CMP     WORD PTR INI_SCROLL_BARS,0
        JNE     SHORT INI_APPLY_SCROLL
        DEC     AX
INI_APPLY_SCROLL:
        CALL    SET_SCROLL_BARS
        MOV     AX,INI_TAB_STOPS
        OR      AH,AH
        JNZ     SHORT INI_APPLY_DONE
        OR      AL,AL
        JZ      SHORT INI_APPLY_DONE
        MOV     TAB_STOPS,AL
INI_APPLY_DONE:
        RET
INI_APPLY ENDP

; SCROLL_BARS = AL (0 or 1), and the text window with it.
SET_SCROLL_BARS PROC NEAR
        MOV     SCROLL_BARS,AL
        JMP     SET_EDITOR_WINDOW
SET_SCROLL_BARS ENDP

; The editor's window: every row between the menu bar and the status line,
; or with Help open the rows below Help's window (EDITOR_ROW_SHIFT puts its
; title rule there; its rows count from that rule, the text from row 2). It
; has the scroll bars of Options Display, but none while Help is the active
; window or with fewer than two rows, and the vertical one only beside
; three rows of text or more (DOS 5, QEMU captures H1 and H2, 2026-11-12).
SET_EDITOR_WINDOW PROC NEAR
        PUSH    AX
        PUSH    BX
        MOV     AL,DOSUI_ROWS
        DEC     AL
        MOV     STATUS_ROW,AL
        MOV     BL,AL                   ; BL = the rows below the title rule
        SUB     BL,2
        XOR     AH,AH
        CMP     BYTE PTR HELP_OPEN,0
        JE      SHORT SET_WINDOW_SHIFT
        MOV     AH,HELP_WINDOW_ROWS
        INC     AH
        SUB     BL,AH
SET_WINDOW_SHIFT:
        MOV     EDITOR_ROW_SHIFT,AH
        MOV     AL,SCROLL_BARS
        CMP     BYTE PTR HELP_ACTIVE,0
        JE      SHORT SET_WINDOW_BARS_ROOM
        XOR     AL,AL
SET_WINDOW_BARS_ROOM:
        CMP     BL,2
        JAE     SHORT SET_WINDOW_BARS
        XOR     AL,AL
SET_WINDOW_BARS:
        MOV     BARS_SHOWN,AL
        MOV     AL,BL
        INC     AL
        MOV     SCROLL_ROW,AL           ; the last row: the horizontal bar
        CMP     BYTE PTR BARS_SHOWN,0
        JE      SHORT SET_WINDOW_TEXT
        DEC     BL
SET_WINDOW_TEXT:
        XOR     BH,BH                   ; BX = the rows of text
        MOV     TEXT_ROWS,BX
        MOV     TEXT_ROW_COUNT,BX
        MOV     AL,BL
        INC     AL
        MOV     TEXT_LAST_ROW,AL
        MOV     TEXT_BOTTOM_ROW,AL
        MOV     AL,BARS_SHOWN
        CMP     BL,3
        JAE     SHORT SET_WINDOW_VERTICAL
        XOR     AL,AL
SET_WINDOW_VERTICAL:
        MOV     VBAR_SHOWN,AL
        SHR     BX,1                    ; a far jump's line: a quarter down
        SHR     BX,1                    ; (DOS 5: row 6 of 21, 13 at /H)
        MOV     FAR_JUMP_LINES,BX
        POP     BX
        POP     AX
        RET
SET_EDITOR_WINDOW ENDP

; TEXT_ATTR = AL; the selection is its inverse, as in DOS 5.
SET_TEXT_ATTRIBUTE PROC NEAR
        MOV     TEXT_ATTR,AL
        MOV     CL,4                    ; 1Eh selects as E1h (plan DSNI)
        ROL     AL,CL
        MOV     SELECT_ATTR,AL
        RET
SET_TEXT_ATTRIBUTE ENDP

; INI_PATH = the directory of EDIT.COM (from the environment's program
; name) followed by QBASIC.INI; just QBASIC.INI when there is none.
INI_PROGRAM_PATH PROC NEAR
        MOV     DI,OFFSET INI_PATH
        MOV     BX,DI
        MOV     ES,DS:[2CH]
        XOR     SI,SI
INI_PROGRAM_SKIP:
        CMP     WORD PTR ES:[SI],0
        JE      SHORT INI_PROGRAM_COUNT
        INC     SI
        CMP     SI,7FFEH
        JB      INI_PROGRAM_SKIP
        JMP     SHORT INI_PROGRAM_NAME
INI_PROGRAM_COUNT:
        CMP     WORD PTR ES:[SI+2],1
        JB      SHORT INI_PROGRAM_NAME
        ADD     SI,4
INI_PROGRAM_COPY:
        MOV     AL,ES:[SI]
        INC     SI
        OR      AL,AL
        JZ      SHORT INI_PROGRAM_NAME
        CMP     DI,OFFSET INI_PATH+63
        JAE     SHORT INI_PROGRAM_NAME
        MOV     [DI],AL
        INC     DI
        CMP     AL,'\'
        JE      SHORT INI_PROGRAM_SEPARATOR
        CMP     AL,':'
        JNE     INI_PROGRAM_COPY
INI_PROGRAM_SEPARATOR:
        MOV     BX,DI
        JMP     INI_PROGRAM_COPY
INI_PROGRAM_NAME:
        PUSH    DS
        POP     ES
        MOV     DI,BX
        CALL    INI_APPEND_NAME
        RET
INI_PROGRAM_PATH ENDP

; Append "QBASIC.INI",0 at DS:DI.
INI_APPEND_NAME PROC NEAR
        PUSH    SI
        MOV     SI,OFFSET INI_FILE_NAME
INI_APPEND_NEXT:
        LODSB
        MOV     [DI],AL
        INC     DI
        OR      AL,AL
        JNZ     INI_APPEND_NEXT
        POP     SI
        RET
INI_APPEND_NAME ENDP

; At exit, after an Options Display OK in this session: write the image,
; with the editor's current settings, as DOS 5 does.
INI_SAVE PROC NEAR
        CMP     BYTE PTR INI_DIRTY,0
        JNE     SHORT INI_SAVE_DIRTY
        JMP     INI_SAVE_DONE
INI_SAVE_DIRTY:
        MOV     AL,TEXT_ATTR
        XOR     AH,AH
        MOV     DX,AX
        AND     AX,7
        MOV     INI_FOREGROUND,AX
        MOV     AX,DX
        MOV     CL,3
        SHR     AX,CL
        AND     AX,1
        MOV     INI_FOREGROUND_BRIGHT,AX
        MOV     AX,DX
        INC     CX                      ; CL = 4
        SHR     AX,CL
        AND     AX,7
        MOV     INI_BACKGROUND,AX
        MOV     AX,DX
        MOV     CL,7
        SHR     AX,CL
        MOV     INI_BACKGROUND_BRIGHT,AX
        MOV     AL,TAB_STOPS
        XOR     AH,AH
        MOV     INI_TAB_STOPS,AX
        MOV     AL,SCROLL_BARS
        MOV     INI_SCROLL_BARS,AX
        PUSH    SI
        PUSH    DI
        MOV     SI,MODE_STATEMENT_SETS  ; QBasic's other two colour sets
        MOV     DI,OFFSET INI_STATEMENT_SETS
        PUSH    DS
        POP     ES
        MOV     CX,8
        REP     MOVSW
        POP     DI
        POP     SI
        MOV     DX,OFFSET INI_PATH
        XOR     CX,CX
        MOV     AH,3CH
        INT     21H
        JNC     SHORT INI_SAVE_CREATED
        MOV     DX,OFFSET INI_FILE_NAME
        XOR     CX,CX
        MOV     AH,3CH
        INT     21H
        JC      SHORT INI_SAVE_DONE
INI_SAVE_CREATED:
        MOV     BX,AX
        MOV     CX,INI_HELP_PATH_LENGTH
        CMP     CX,INI_HELP_PATH_MAX
        JBE     SHORT INI_SAVE_LENGTH
        MOV     CX,INI_HELP_PATH_MAX
INI_SAVE_LENGTH:
        ADD     CX,OFFSET INI_HELP_PATH - OFFSET INI_IMAGE
        MOV     DX,OFFSET INI_IMAGE
        MOV     AH,40H
        INT     21H
        MOV     AH,3EH
        INT     21H
INI_SAVE_DONE:
        RET
INI_SAVE ENDP

SAVE_FILE PROC NEAR
        MOV     BYTE PTR SAVE_CANCELLED,0
        MOV     WORD PTR FILE_ERROR,5   ; access denied unless DOS says more
        CMP     BYTE PTR SAVE_DISABLED,0
        JE      SHORT SAVE_FILE_ALLOWED
        JMP     SAVE_FAIL
SAVE_FILE_ALLOWED:
        CMP     BYTE PTR SAVE_AS_REQUESTED,0
        JE      SHORT SAVE_FILE_CHECK_NAME
        MOV     BYTE PTR SAVE_AS_REQUESTED,0
        JMP     SHORT SAVE_FILE_PROMPT_NAME
SAVE_FILE_CHECK_NAME:
        CMP     BYTE PTR FILE_NAME,0
        JNE     SAVE_FILE_HAVE_NAME
SAVE_FILE_PROMPT_NAME:
        CALL    PROMPT_FILE_NAME
        OR      AL,AL
        JNZ     SHORT SAVE_FILE_NEW_NAME
        MOV     BYTE PTR SAVE_CANCELLED,1
        JMP     SAVE_FAIL
SAVE_FILE_NEW_NAME:
        MOV     BYTE PTR SAVE_AS_NAME,1
SAVE_FILE_HAVE_NAME:
        CMP     BYTE PTR SAVE_AS_NAME,0
        JE      SAVE_CREATE
        MOV     DX,OFFSET FILE_NAME
        MOV     AX,4300H
        INT     21H
        JC      SAVE_CREATE
        CALL    CONFIRM_OVERWRITE
        OR      AL,AL
        JNZ     SHORT SAVE_OVERWRITE
        CALL    NORMALIZE_SAVE_AS_NAME
        MOV     BYTE PTR SAVE_AS_RETRY,1
        JMP     SAVE_FILE_PROMPT_NAME
SAVE_OVERWRITE:
        MOV     BYTE PTR SAVE_AS_NAME,0
SAVE_CREATE:
        MOV     DX,OFFSET FILE_NAME
        XOR     CX,CX
        MOV     AH,3CH
        INT     21H
        JNC     SHORT SAVE_CREATED
        CALL    NOTE_DOS_ERROR
        JMP     SAVE_FAIL
SAVE_CREATED:
        MOV     BX,AX
        CALL    SAVE_TEXT_BUFFER
        JC      SAVE_CLOSE_FAIL
        MOV     AH,68H
        INT     21H
        JC      SHORT SAVE_CLOSE_DOS_FAIL
        MOV     AH,3EH
        INT     21H
        JNC     SHORT SAVE_CLOSED
        CALL    NOTE_DOS_ERROR
        JMP     SAVE_FAIL
SAVE_CLOSED:
        CALL    COMMIT_DISPLAY_NAME
        MOV     BYTE PTR MODIFIED,0
        MOV     BYTE PTR SAVE_AS_NAME,0
        CLC
        RET
SAVE_CLOSE_DOS_FAIL:
        CALL    NOTE_DOS_ERROR
SAVE_CLOSE_FAIL:
        PUSHF
        MOV     AH,3EH
        INT     21H
        POPF
SAVE_FAIL:
        STC
        RET
SAVE_FILE ENDP

; FILE_NAME is also the mutable Save As candidate.  Keep the editor title on
; the last committed name until creation, writing, flushing, and closing all
; succeed; DOS 5 continues to show Untitled throughout overwrite retry/cancel.
COMMIT_DISPLAY_NAME PROC NEAR
        PUSH    AX
        PUSH    SI
        PUSH    DI
        MOV     SI,OFFSET FILE_NAME
        MOV     DI,OFFSET DISPLAY_NAME
COMMIT_DISPLAY_NAME_NEXT:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     SHORT COMMIT_DISPLAY_NAME_NEXT
        POP     DI
        POP     SI
        POP     AX
        RET
COMMIT_DISPLAY_NAME ENDP

NORMALIZE_SAVE_AS_NAME PROC NEAR
        CMP     BYTE PTR FILE_NAME,'C'
        JE      SHORT NORMALIZE_SAVE_AS_DRIVE_CASE
        CMP     BYTE PTR FILE_NAME,'c'
        JNE     SHORT NORMALIZE_SAVE_AS_DONE
NORMALIZE_SAVE_AS_DRIVE_CASE:
        CMP     BYTE PTR FILE_NAME+1,':'
        JNE     SHORT NORMALIZE_SAVE_AS_DONE
        CMP     BYTE PTR FILE_NAME+2,'\'
        JNE     SHORT NORMALIZE_SAVE_AS_DONE
        MOV     SI,OFFSET FILE_NAME+3
        MOV     DI,OFFSET FILE_NAME
NORMALIZE_SAVE_AS_COPY:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     SHORT NORMALIZE_SAVE_AS_COPY
NORMALIZE_SAVE_AS_DONE:
        RET
NORMALIZE_SAVE_AS_NAME ENDP

; DOS 5 EDIT serializes the logical text buffer rather than preserving the
; source file's malformed byte separators. Every LF becomes CRLF, lone CR
; bytes disappear, and a non-empty unterminated final line receives CRLF.
; When the loaded file had a tab (TAB_FILE), a line's leading blanks are
; written as tabs to each Tab Stops column and spaces for the rest (see
; EXPAND_LOADED_TABS).
; BX is the already-created destination handle. Return carry on any failed or
; short DOS write without changing the text; the whole text is written from
; the document block, the view standing in as the output buffer.
SAVE_TEXT_BUFFER PROC NEAR
        PUSH    AX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    BP
        CALL    SAVE_POSITIONS          ; the whole text, the view written back
        CALL    DOC_LENGTH
        MOV     STREAM_END,AX
        MOV     STREAM_END+2,DX
        XOR     AX,AX
        MOV     STREAM_POS,AX
        MOV     STREAM_POS+2,AX
        MOV     OUT_LENGTH,AX           ; the view's room buffers the writes
        CALL    STREAM_BEGIN
        MOV     BP,4                    ; bit 0: output, bit 1: ended in CRLF,
                                        ; bit 2: at a line's start
SAVE_TEXT_NEXT:
        CALL    STREAM_NEXT
        JC      SAVE_TEXT_FINISH
SAVE_TEXT_BYTE:
        CMP     AL,13
        JE      SAVE_TEXT_NEXT          ; lone CR bytes disappear
        CMP     AL,10
        JE      SAVE_TEXT_NEWLINE
        TEST    BP,4
        JZ      SAVE_TEXT_TEXT
        AND     BP,NOT 4
        CMP     BYTE PTR TAB_FILE,0     ; a file without tabs keeps its blanks
        JE      SAVE_TEXT_TEXT
        CMP     AL,' '
        JE      SAVE_TEXT_INDENT
        CMP     AL,9
        JNE     SAVE_TEXT_TEXT
SAVE_TEXT_INDENT:
        MOV     BYTE PTR SAVE_AT_END,0
        CALL    SAVE_TEXT_WRITE_INDENT
        JC      SAVE_TEXT_FAIL
        MOV     BP,1
        CMP     BYTE PTR SAVE_AT_END,0
        JNE     SAVE_TEXT_FINISH
        JMP     SAVE_TEXT_BYTE
SAVE_TEXT_TEXT:
        CALL    SAVE_EMIT
        JC      SAVE_TEXT_FAIL
        MOV     BP,1
        JMP     SAVE_TEXT_NEXT
SAVE_TEXT_NEWLINE:
        CALL    SAVE_EMIT_CRLF
        JC      SAVE_TEXT_FAIL
        MOV     BP,7
        JMP     SAVE_TEXT_NEXT
SAVE_TEXT_FINISH:
        TEST    BP,1
        JZ      SAVE_TEXT_FLUSH
        TEST    BP,2
        JNZ     SAVE_TEXT_FLUSH
        CALL    SAVE_EMIT_CRLF
        JC      SAVE_TEXT_FAIL
SAVE_TEXT_FLUSH:
        CALL    SAVE_FLUSH_OUT
        JC      SAVE_TEXT_FAIL
        CLC
        JMP     SHORT SAVE_TEXT_DONE
SAVE_TEXT_FAIL:
        STC
SAVE_TEXT_DONE:
        PUSHF
        CALL    REBUILD_VIEW
        POPF
        POP     BP
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     AX
        RET
SAVE_TEXT_BUFFER ENDP

; AL = a line's first blank: count its run of blanks (spaces and tabs,
; measured in columns) and write it as tabs to each Tab Stops column, then
; spaces. AL returns the byte after the run (SAVE_AT_END set when the text
; ended in it). Carry: a write failed.
SAVE_TEXT_WRITE_INDENT PROC NEAR
        XOR     DX,DX                   ; columns
SAVE_INDENT_COUNT:
        CMP     AL,' '
        JE      SAVE_INDENT_SPACE
        CMP     AL,9
        JNE     SAVE_INDENT_COUNTED
        PUSH    BX
        XOR     BX,BX
        MOV     BL,TAB_STOPS
        PUSH    AX
        MOV     AX,DX
        PUSH    DX
        XOR     DX,DX
        DIV     BX
        SUB     BX,DX
        POP     DX
        ADD     DX,BX
        POP     AX
        POP     BX
        JMP     SHORT SAVE_INDENT_READ
SAVE_INDENT_SPACE:
        INC     DX
SAVE_INDENT_READ:
        CALL    STREAM_NEXT
        JNC     SAVE_INDENT_COUNT
        MOV     BYTE PTR SAVE_AT_END,1
SAVE_INDENT_COUNTED:
        PUSH    AX
        MOV     AX,DX
        XOR     DX,DX
        PUSH    BX
        XOR     BX,BX
        MOV     BL,TAB_STOPS
        DIV     BX                      ; AX tabs, DX spaces
        POP     BX
        PUSH    DX
        MOV     CX,AX
        MOV     AL,9
        CALL    SAVE_EMIT_REPEAT
        POP     CX
        JC      SAVE_INDENT_DONE
        MOV     AL,' '
        CALL    SAVE_EMIT_REPEAT
SAVE_INDENT_DONE:
        POP     AX
        RET
SAVE_TEXT_WRITE_INDENT ENDP

; Write CX copies of AL. Carry on a failed or short write.
SAVE_EMIT_REPEAT PROC NEAR
        JCXZ    SAVE_EMIT_REPEAT_OK
        CALL    SAVE_EMIT
        JC      SAVE_EMIT_REPEAT_DONE
        LOOP    SAVE_EMIT_REPEAT
SAVE_EMIT_REPEAT_OK:
        CLC
SAVE_EMIT_REPEAT_DONE:
        RET
SAVE_EMIT_REPEAT ENDP

SAVE_EMIT_CRLF PROC NEAR
        PUSH    AX
        MOV     AL,13
        CALL    SAVE_EMIT
        JC      SAVE_EMIT_CRLF_DONE
        MOV     AL,10
        CALL    SAVE_EMIT
SAVE_EMIT_CRLF_DONE:
        POP     AX
        RET
SAVE_EMIT_CRLF ENDP

; AL to the file (handle BX) through the view's room, 4 KB at a time.
; Carry on a failed or short write.
SAVE_EMIT PROC NEAR
        PUSH    DI
        MOV     DI,OUT_LENGTH
        MOV     BUFFER[DI],AL
        INC     DI
        MOV     OUT_LENGTH,DI
        POP     DI
        CMP     WORD PTR OUT_LENGTH,4096
        JAE     SAVE_FLUSH_OUT
        CLC
        RET
SAVE_EMIT ENDP

SAVE_FLUSH_OUT PROC NEAR
        PUSH    AX
        PUSH    CX
        PUSH    DX
        MOV     CX,OUT_LENGTH
        CLC
        JCXZ    SAVE_FLUSH_DONE
        MOV     WORD PTR OUT_LENGTH,0
        MOV     DX,BUFFER_BASE
        MOV     AH,40H
        PUSH    DS
        PUSH    SS
        POP     DS
        INT     21H
        POP     DS
        CALL    CHECK_WRITE
SAVE_FLUSH_DONE:
        POP     DX
        POP     CX
        POP     AX
        RET
SAVE_FLUSH_OUT ENDP

SAVE_TEXT_WRITE_CRLF PROC NEAR
        MOV     DX,OFFSET SAVE_CRLF
        MOV     CX,2
        MOV     AH,40H
        INT     21H
        JMP     SHORT CHECK_WRITE
SAVE_TEXT_WRITE_CRLF ENDP

; After INT 21h AH=40h for CX bytes: carry and FILE_ERROR set when it
; failed, or wrote less (a full disk).
CHECK_WRITE PROC NEAR
        JNC     SHORT CHECK_WRITE_COUNT
        JMP     NOTE_DOS_ERROR_FAILED
CHECK_WRITE_COUNT:
        CMP     AX,CX
        JE      SHORT CHECK_WRITE_DONE
        MOV     WORD PTR FILE_ERROR,ERROR_DISK_FULL
        STC
CHECK_WRITE_DONE:
        RET
CHECK_WRITE ENDP

NOTE_DOS_ERROR_FAILED PROC NEAR
        CALL    NOTE_DOS_ERROR
        STC
        RET
NOTE_DOS_ERROR_FAILED ENDP

INSERT_BYTE PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        MOV     BX,CURSOR_OFFSET
        CMP     BYTE PTR INSERT_MODE,0
        JNE     INSERT_SHIFT
        CMP     BX,BUFFER_LENGTH
        JAE     INSERT_SHIFT
        CMP     BYTE PTR BUFFER[BX],13
        JE      INSERT_SHIFT
        CMP     BYTE PTR BUFFER[BX],10
        JE      INSERT_SHIFT
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        POP     AX
        MOV     BX,CURSOR_OFFSET
        MOV     BUFFER[BX],AL
        INC     WORD PTR CURSOR_OFFSET
        MOV     BYTE PTR MODIFIED,1
        RET
INSERT_SHIFT:
        CALL    VIEW_ROOM
        OR      AX,AX
        JZ      INSERT_FULL
        MOV     CX,BUFFER_LENGTH
        SUB     CX,BX
        JCXZ    INSERT_STORE
        MOV     SI,BUFFER_BASE
        ADD     SI,BUFFER_LENGTH
        DEC     SI
        MOV     DI,SI
        INC     DI
        VIEW_SEGS
        STD
        REP     MOVSB
        CLD
        CODE_SEGS
INSERT_STORE:
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        POP     AX
        MOV     BX,CURSOR_OFFSET
        MOV     BUFFER[BX],AL
        INC     WORD PTR BUFFER_LENGTH
        INC     WORD PTR CURSOR_OFFSET
        MOV     BYTE PTR MODIFIED,1
        RET
INSERT_FULL:
        CLD
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        POP     AX
        MOV     BYTE PTR MEMORY_FULL,1  ; "Out of memory" at the redraw
        RET
INSERT_BYTE ENDP

DELETE_AT_CURSOR PROC NEAR
        MOV     BX,CURSOR_OFFSET
        CMP     BX,BUFFER_LENGTH
        JAE     DELETE_DONE
        MOV     AX,1
        CMP     BYTE PTR BUFFER[BX],13
        JNE     DELETE_COUNT_READY
        MOV     DX,BX
        INC     DX
        CMP     DX,BUFFER_LENGTH
        JAE     DELETE_COUNT_READY
        CMP     BYTE PTR BUFFER[BX+1],10
        JNE     DELETE_COUNT_READY
        INC     AX
DELETE_COUNT_READY:
        PUSH    AX
        MOV     SI,BUFFER_BASE
        ADD     SI,BX
        ADD     SI,AX
        MOV     DI,BUFFER_BASE
        ADD     DI,BX
        MOV     CX,BUFFER_LENGTH
        SUB     CX,BX
        SUB     CX,AX
        CLD
        VIEW_SEGS
        REP     MOVSB
        CODE_SEGS
        POP     AX
        SUB     BUFFER_LENGTH,AX
        MOV     BYTE PTR MODIFIED,1
DELETE_DONE:
        RET
DELETE_AT_CURSOR ENDP

DELETE_BEFORE_CURSOR PROC NEAR
        CMP     WORD PTR CURSOR_OFFSET,0
        JE      DELETE_BEFORE_DONE
        DEC     WORD PTR CURSOR_OFFSET
        MOV     BX,CURSOR_OFFSET
        CMP     BYTE PTR BUFFER[BX],10
        JNE     DELETE_BEFORE_ONE
        OR      BX,BX
        JZ      DELETE_BEFORE_ONE
        CMP     BYTE PTR BUFFER[BX-1],13
        JNE     DELETE_BEFORE_ONE
        DEC     WORD PTR CURSOR_OFFSET
DELETE_BEFORE_ONE:
        CALL    DELETE_AT_CURSOR
DELETE_BEFORE_DONE:
        RET
DELETE_BEFORE_CURSOR ENDP

CALCULATE_POSITION PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DI
        PUSH    ES
        XOR     AX,AX
        MOV     CURRENT_LINE,AX
        MOV     CURRENT_COLUMN,AX
        MOV     CX,CURSOR_OFFSET
        JCXZ    CALCULATE_DONE_FAST
        PUSH    SS
        POP     ES
        MOV     DI,BUFFER_BASE
        MOV     BX,DI
        MOV     AL,10
        CLD
CALCULATE_SCAN_LF:
        REPNE   SCASB
        JNE     SHORT CALCULATE_COUNT_COL
        INC     WORD PTR CURRENT_LINE
        MOV     BX,DI
        JCXZ    CALCULATE_DONE_FAST
        JMP     CALCULATE_SCAN_LF
CALCULATE_COUNT_COL:
        MOV     SI,BX
        SUB     SI,BUFFER_BASE
CALCULATE_COL_LOOP:
        CMP     SI,CURSOR_OFFSET
        JAE     SHORT CALCULATE_DONE_FAST
        MOV     AL,BUFFER[SI]
        CMP     AL,13
        JE      SHORT CALCULATE_COL_SKIP
        INC     WORD PTR CURRENT_COLUMN
CALCULATE_COL_SKIP:
        INC     SI
        JMP     CALCULATE_COL_LOOP
CALCULATE_DONE_FAST:
        MOV     SI,CURSOR_OFFSET
        POP     ES
        POP     DI
        POP     CX
        POP     BX
        RET
CALCULATE_POSITION ENDP

; Move to target line AX and target column DX. Short lines clamp to their end.
; Carry is set when the requested line does not exist.
MOVE_TO_LINE_COLUMN PROC NEAR
        XOR     SI,SI
        XOR     BX,BX
        OR      AX,AX
        JZ      SHORT MOVE_TO_COLUMN
        PUSH    CX
        PUSH    DI
        PUSH    ES
        PUSH    SS
        POP     ES
        MOV     DI,BUFFER_BASE
        MOV     CX,BUFFER_LENGTH
        CLD
MOVE_TO_LINE_FIND:
        JCXZ    MOVE_TO_LINE_SCAN_FAIL
        PUSH    AX
        MOV     AL,10
        REPNE   SCASB
        POP     AX
        JNE     SHORT MOVE_TO_LINE_SCAN_FAIL
        INC     BX
        CMP     BX,AX
        JB      MOVE_TO_LINE_FIND
        MOV     SI,DI
        SUB     SI,BUFFER_BASE
        POP     ES
        POP     DI
        POP     CX
        JMP     SHORT MOVE_TO_COLUMN
MOVE_TO_LINE_SCAN_FAIL:
        MOV     SI,BUFFER_LENGTH
        POP     ES
        POP     DI
        POP     CX
        JMP     SHORT MOVE_TO_LINE_FAIL
MOVE_TO_COLUMN:
        MOV     CX,DX
MOVE_TO_COLUMN_NEXT:
        JCXZ    MOVE_TO_LINE_DONE
        CMP     SI,BUFFER_LENGTH
        JAE     MOVE_TO_LINE_DONE
        MOV     AL,BUFFER[SI]
        CMP     AL,13
        JE      MOVE_TO_LINE_DONE
        CMP     AL,10
        JE      MOVE_TO_LINE_DONE
        INC     SI
        DEC     CX
        JMP     MOVE_TO_COLUMN_NEXT
MOVE_TO_LINE_DONE:
        MOV     CURSOR_OFFSET,SI
        CLC
        RET
MOVE_TO_LINE_FAIL:
        STC
        RET
MOVE_TO_LINE_COLUMN ENDP

FIND_LAST_LINE PROC NEAR
        PUSH    CX
        PUSH    DI
        PUSH    ES
        XOR     AX,AX
        MOV     CX,BUFFER_LENGTH
        JCXZ    FIND_LAST_LINE_DONE
        PUSH    SS
        POP     ES
        MOV     DI,BUFFER_BASE
        CLD
FIND_LAST_LINE_NEXT:
        PUSH    AX
        MOV     AL,10
        REPNE   SCASB
        POP     AX
        JNE     SHORT FIND_LAST_LINE_DONE
        INC     AX
        JCXZ    FIND_LAST_LINE_DONE
        JMP     FIND_LAST_LINE_NEXT
FIND_LAST_LINE_DONE:
        MOV     SI,BUFFER_LENGTH
        POP     ES
        POP     DI
        POP     CX
        RET
FIND_LAST_LINE ENDP

; Line AX, column DX for the vertical moves. As in DOS 5 EDIT the cursor
; keeps its column on a shorter line, out past the line end. Carry: no line.
VERTICAL_MOVE_TO PROC NEAR
        PUSH    DX
        CALL    MOVE_TO_LINE_COLUMN
        POP     DX
        JC      SHORT VERTICAL_MOVE_TO_DONE
        PUSH    AX
        PUSH    SI
        CALL    CALCULATE_POSITION
        POP     SI
        POP     AX
        CMP     DX,CURRENT_COLUMN
        JBE     SHORT VERTICAL_MOVE_TO_OK
        MOV     VIRTUAL_COLUMN,DX
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,1
VERTICAL_MOVE_TO_OK:
        CLC
VERTICAL_MOVE_TO_DONE:
        RET
VERTICAL_MOVE_TO ENDP

MOVE_UP PROC NEAR
        CALL    PREPARE_VERTICAL_COLUMN
        CMP     WORD PTR CURRENT_LINE,0
        JE      MOVE_UP_DONE
        MOV     AX,CURRENT_LINE
        DEC     AX
        CALL    VERTICAL_MOVE_TO
MOVE_UP_DONE:
        RET
MOVE_UP ENDP

MOVE_DOWN PROC NEAR
        CALL    PREPARE_VERTICAL_COLUMN
        MOV     AX,CURRENT_LINE
        INC     AX
        CALL    VERTICAL_MOVE_TO
        RET
MOVE_DOWN ENDP

MOVE_PAGE_UP PROC NEAR
        CALL    PREPARE_VERTICAL_COLUMN
        MOV     BX,TOP_LINE
        OR      BX,BX
        JZ      SHORT MOVE_PAGE_UP_FIRST
        MOV     CX,TEXT_ROW_COUNT
        CMP     BX,CX
        JAE     SHORT MOVE_PAGE_UP_HAVE_DELTA
        MOV     CX,BX
MOVE_PAGE_UP_HAVE_DELTA:
        SUB     WORD PTR TOP_LINE,CX
        MOV     AX,CURRENT_LINE
        SUB     AX,CX
        CALL    VERTICAL_MOVE_TO
        RET
MOVE_PAGE_UP_FIRST:
        XOR     AX,AX
        CALL    VERTICAL_MOVE_TO
        RET
MOVE_PAGE_UP ENDP

MOVE_PAGE_DOWN PROC NEAR
        CALL    PREPARE_VERTICAL_COLUMN
        MOV     BX,CURRENT_LINE
        ADD     BX,TEXT_ROW_COUNT
        CALL    FIND_LAST_LINE
        CMP     BX,AX                   ; DOS EDIT does not scroll a partial page.
        JA      SHORT MOVE_PAGE_DOWN_DONE
        MOV     AX,BX
        CALL    VERTICAL_MOVE_TO
        JC      SHORT MOVE_PAGE_DOWN_DONE
        MOV     AX,TEXT_ROW_COUNT
        ADD     TOP_LINE,AX
MOVE_PAGE_DOWN_DONE:
        RET
MOVE_PAGE_DOWN ENDP

; Vertical motion retains the requested column across short lines. Horizontal
; motion resets this goal, matching DOS EDIT's preferred-column navigation.
PREPARE_VERTICAL_COLUMN PROC NEAR
        CALL    CALCULATE_POSITION
        CMP     BYTE PTR PREFERRED_COLUMN_VALID,0
        JNE     SHORT PREPARE_VERTICAL_HAVE_COLUMN
        MOV     AX,CURRENT_COLUMN
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT PREPARE_VERTICAL_SET
        MOV     AX,VIRTUAL_COLUMN
PREPARE_VERTICAL_SET:
        MOV     PREFERRED_COLUMN,AX
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,1
PREPARE_VERTICAL_HAVE_COLUMN:
        MOV     DX,PREFERRED_COLUMN
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,0
        RET
PREPARE_VERTICAL_COLUMN ENDP

; Shift-navigation starts or extends the DOS EDIT selection. An unshifted
; cursor move collapses only a selection that was created this way; the legacy
; Ctrl+B mark remains active until its explicit block command completes.
PREPARE_SHIFT_SELECTION PROC NEAR
        TEST    BYTE PTR MODIFIER_STATE,03H
        JZ      SHORT PREPARE_SHIFT_NOT_HELD
        CMP     WORD PTR BLOCK_MARK,0FFFFH
        JNE     SHORT PREPARE_SHIFT_DONE
        MOV     AX,CURSOR_OFFSET
        MOV     BLOCK_MARK,AX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     BYTE PTR SHIFT_SELECTION,1
        JMP     SHORT PREPARE_SHIFT_DONE
PREPARE_SHIFT_NOT_HELD:
        CMP     BYTE PTR SHIFT_SELECTION,0
        JE      SHORT PREPARE_SHIFT_DONE
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
PREPARE_SHIFT_DONE:
        RET
PREPARE_SHIFT_SELECTION ENDP

; AX = leading blanks of the cursor's line, at most the cursor's column.
LINE_INDENT PROC NEAR
        PUSH    BX
        PUSH    SI
        CALL    CALCULATE_POSITION
        MOV     BX,CURSOR_OFFSET
        SUB     BX,CURRENT_COLUMN
        XOR     AX,AX
LINE_INDENT_NEXT:
        CMP     AX,CURRENT_COLUMN
        JAE     SHORT LINE_INDENT_DONE
        CMP     BYTE PTR BUFFER[BX],' '
        JNE     SHORT LINE_INDENT_DONE
        INC     AX
        INC     BX
        JMP     LINE_INDENT_NEXT
LINE_INDENT_DONE:
        POP     SI
        POP     BX
        RET
LINE_INDENT ENDP

; Delete a selection, if there is one; carry is set when text was removed.
DELETE_SELECTION PROC NEAR
        CALL    HAS_SELECTION
        JNC     SHORT DELETE_SELECTION_DONE
        PUSH    AX
        CALL    CLEAR_BLOCK
        POP     AX
        STC
DELETE_SELECTION_DONE:
        RET
DELETE_SELECTION ENDP

; Find the pattern from DX:AX (an offset in the whole text) on, wrapping to
; the top of the file as DOS 5 EDIT does, and select the match with the
; cursor at its start, so the next key acts on it like on any selection.
; Carry: not found anywhere.
FIND_AND_SELECT PROC NEAR
        CALL    RESET_NAVIGATION_COLUMNS
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
        CALL    FIND_FROM_GLOBAL
        JNC     SHORT FIND_AND_SELECT_FOUND
        XOR     AX,AX
        XOR     DX,DX
        CALL    FIND_FROM_GLOBAL
        JNC     SHORT FIND_AND_SELECT_FOUND
        RET
FIND_AND_SELECT_FOUND:
        MOV     AX,SEARCH_HIGHLIGHT_END
        MOV     BLOCK_MARK,AX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     BYTE PTR SHIFT_SELECTION,1
        MOV     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        MOV     WORD PTR SEARCH_HIGHLIGHT_END,0FFFFH
        CLC
        RET
FIND_AND_SELECT ENDP

; ---- Dialog focus model -------------------------------------------------
; DOS 5 dialogs: Tab and Shift+Tab move the focus through fields, check
; boxes, lists and buttons; Alt+letter focuses a control (a check box also
; toggles, a button is pressed); Enter presses the focused button, or else
; the default one; Esc cancels; Space toggles a check box or presses a
; button. The focused button, or the default one while the focus is not on
; a button, has bright brackets. A field shows its text selected when the
; focus enters it, so typing replaces it.
;
; Control table entries (DLG_ENTRY bytes): type, row, column, size, data
; word, Alt scan code, aux byte.
;   field    column of the first text cell, size = width, data = buffer,
;            aux = capacity
;   check    column of '[', data = byte variable (0/1)
;   button   column of '<', size = width, data = result code, aux = label
;            offset (cursor)
;   list     column of the item slot, size = visible rows, data = list
;            descriptor
;   radio    column of '(', data = byte variable, aux = this choice
; RUN_DIALOG: SI = table, CL = count, CH = default button, AL = first focus.
; AL returns the pressed button's code, 0 for Esc, DLG_HELP for Help/F1.
DLG_FIELD       EQU 1
DLG_CHECK       EQU 2
DLG_BUTTON      EQU 3
DLG_LIST        EQU 4
DLG_RADIO       EQU 5
DLG_ENTRY       EQU 8
DLG_HELP        EQU 0FEH
; List descriptor offsets.
LIST_ITEMS      EQU 0                   ; word: first entry
LIST_STRIDE     EQU 2                   ; byte: entry size
LIST_COLUMNS    EQU 3                   ; byte: columns shown (1 or 3)
LIST_COUNT      EQU 4                   ; word ptr: entry count
LIST_TOP        EQU 6                   ; word ptr: first shown entry
LIST_SELECTED   EQU 8                   ; word ptr: selection (FFFFh none)
LIST_WIDTH      EQU 10                  ; byte: slot width
LIST_BAR        EQU 11                  ; byte: scroll bar column (0 none)
LIST_ON_SELECT  EQU 12                  ; word: proc after a selection
LIST_ON_ENTER   EQU 14                  ; word: proc for Enter (CF: stay)

RUN_DIALOG PROC NEAR
        MOV     BYTE PTR DRAWN_VALID,0
        MOV     DLG_TABLE,SI
        MOV     DLG_COUNT,CL
        MOV     DLG_DEFAULT,CH
        CALL    DLG_SET_FOCUS
DLG_REDRAW:
        CALL    DLG_DRAW
DLG_KEY:
        CALL    DOSUI_READ_EVENT
        JNC     SHORT DLG_HAVE_KEY
        JMP     DLG_MOUSE
DLG_HAVE_KEY:
        CALL    DLG_FOCUSED             ; BX = focused entry
        CMP     AL,27
        JNE     SHORT DLG_NOT_ESCAPE
        XOR     AL,AL
        RET
DLG_NOT_ESCAPE:
        CMP     AL,13
        JNE     SHORT DLG_NOT_ENTER
        JMP     DLG_ENTER
DLG_NOT_ENTER:
        CMP     AL,9
        JNE     SHORT DLG_NOT_TAB
        JMP     DLG_NEXT
DLG_NOT_TAB:
        OR      AL,AL
        JNZ     SHORT DLG_CHARACTER
        CMP     AH,0FH                  ; Shift+Tab
        JNE     SHORT DLG_NOT_BACK_TAB
        JMP     DLG_PREVIOUS
DLG_NOT_BACK_TAB:
        CMP     AH,3BH                  ; F1
        JNE     SHORT DLG_NOT_F1
        MOV     AL,DLG_HELP
        RET
DLG_NOT_F1:
        CALL    DLG_ALT_KEY
        JNC     SHORT DLG_NOT_ALT
        JMP     DLG_REDRAW_OR_DONE
DLG_NOT_ALT:
        CALL    DLG_FOCUSED
        MOV     AL,0                    ; an extended key for the control
DLG_CHARACTER:
        MOV     CL,[BX]
        CMP     CL,DLG_FIELD
        JNE     SHORT DLG_NOT_FIELD_KEY
        CALL    FIELD_KEY
        JMP     DLG_REDRAW
DLG_NOT_FIELD_KEY:
        CMP     CL,DLG_LIST
        JNE     SHORT DLG_NOT_LIST_KEY
        CALL    LIST_KEY
        JMP     DLG_REDRAW
DLG_NOT_LIST_KEY:
        CMP     CL,DLG_RADIO
        JNE     SHORT DLG_NOT_RADIO_KEY
        CALL    RADIO_KEY
        JMP     DLG_REDRAW
DLG_NOT_RADIO_KEY:
        CMP     AL,' '
        JNE     SHORT DLG_ARROW
        CMP     CL,DLG_CHECK
        JNE     SHORT DLG_SPACE_BUTTON
        CALL    CHECK_TOGGLE
        JMP     DLG_REDRAW
DLG_SPACE_BUTTON:
        CMP     CL,DLG_BUTTON
        JNE     SHORT DLG_IGNORE
        MOV     AL,[BX+4]
        RET
DLG_ARROW:                              ; Arrow=Next Item on the rest
        OR      AL,AL
        JZ      SHORT DLG_ARROW_KEY
        CALL    DLG_ALT_KEY             ; a letter: as Alt and that letter
        JNC     SHORT DLG_IGNORE
        JMP     DLG_REDRAW_OR_DONE
DLG_ARROW_KEY:
        CMP     AH,4DH
        JE      SHORT DLG_NEXT
        CMP     AH,50H
        JE      SHORT DLG_NEXT
        CMP     AH,4BH
        JE      SHORT DLG_PREVIOUS
        CMP     AH,48H
        JE      SHORT DLG_PREVIOUS
DLG_IGNORE:
        JMP     DLG_KEY
DLG_NEXT:
        MOV     AL,DLG_FOCUS
        INC     AL
        CMP     AL,DLG_COUNT
        JB      SHORT DLG_MOVE
        XOR     AL,AL
        JMP     SHORT DLG_MOVE
DLG_PREVIOUS:
        MOV     AL,DLG_FOCUS
        OR      AL,AL
        JNZ     SHORT DLG_PREVIOUS_ONE
        MOV     AL,DLG_COUNT
DLG_PREVIOUS_ONE:
        DEC     AL
DLG_MOVE:
        CALL    DLG_SET_FOCUS
        JMP     DLG_REDRAW
DLG_ENTER:
        MOV     CL,[BX]
        CMP     CL,DLG_BUTTON
        JNE     SHORT DLG_ENTER_NOT_BUTTON
        MOV     AL,[BX+4]
        RET
DLG_ENTER_NOT_BUTTON:
        CMP     CL,DLG_LIST
        JNE     SHORT DLG_ENTER_DEFAULT
        MOV     SI,[BX+4]
        MOV     DX,[SI+LIST_ON_ENTER]
        OR      DX,DX
        JZ      SHORT DLG_ENTER_DEFAULT
        CALL    DX
        JNC     SHORT DLG_ENTER_DEFAULT
        JMP     DLG_REDRAW
DLG_ENTER_DEFAULT:
        MOV     AL,DLG_DEFAULT
        CALL    DLG_ENTRY_OF
        MOV     AL,[BX+4]
        RET
DLG_REDRAW_OR_DONE:                     ; DLG_ALT_KEY: AH=0 redraw, else AL
        OR      AH,AH
        JNZ     SHORT DLG_DONE
        JMP     DLG_REDRAW
DLG_DONE:
        RET

; A click: the status-bar shortcuts answer like their keys, a control is
; focused and acted on like a key would act on it.
DLG_MOUSE:
        CMP     DH,STATUS_HIT_ROW
        JNE     SHORT DLG_MOUSE_CONTROL
        CMP     DL,7
        JA      SHORT DLG_MOUSE_NOT_HELP
        MOV     AL,DLG_HELP
        RET
DLG_MOUSE_NOT_HELP:
        CMP     DL,23
        JA      SHORT DLG_MOUSE_NOT_ENTER
        CALL    DLG_FOCUSED
        JMP     DLG_ENTER
DLG_MOUSE_NOT_ENTER:
        CMP     DL,36
        JA      SHORT DLG_MOUSE_MISS
        XOR     AL,AL
        RET
DLG_MOUSE_CONTROL:
        XOR     AL,AL
DLG_MOUSE_SCAN:
        CMP     AL,DLG_COUNT
        JAE     SHORT DLG_MOUSE_MISS
        CALL    DLG_ENTRY_OF
        CALL    DLG_HIT
        JC      SHORT DLG_MOUSE_FOUND
        INC     AL
        JMP     DLG_MOUSE_SCAN
DLG_MOUSE_FOUND:
        PUSH    DX
        CALL    DLG_SET_FOCUS
        POP     DX
        CALL    DLG_FOCUSED
        MOV     CL,[BX]
        CMP     CL,DLG_BUTTON
        JNE     SHORT DLG_MOUSE_NOT_BUTTON
        MOV     AL,[BX+4]
        RET
DLG_MOUSE_NOT_BUTTON:
        CMP     CL,DLG_CHECK
        JNE     SHORT DLG_MOUSE_NOT_CHECK
        CALL    CHECK_TOGGLE
        JMP     DLG_REDRAW
DLG_MOUSE_NOT_CHECK:
        CMP     CL,DLG_LIST
        JNE     SHORT DLG_MOUSE_NOT_LIST
        CALL    LIST_CLICK
        JMP     DLG_REDRAW
DLG_MOUSE_NOT_LIST:
        CMP     CL,DLG_RADIO
        JNE     SHORT DLG_MOUSE_REDRAW
        MOV     SI,[BX+4]
        MOV     AL,DH
        SUB     AL,[BX+1]
        MOV     [SI],AL
DLG_MOUSE_REDRAW:
        JMP     DLG_REDRAW
DLG_MOUSE_MISS:
        JMP     DLG_KEY
RUN_DIALOG ENDP

; BX = entry AL of the table.
DLG_ENTRY_OF PROC NEAR
        PUSH    AX
        XOR     AH,AH
        MOV     BX,AX
        SHL     BX,1
        SHL     BX,1
        SHL     BX,1
        ADD     BX,DLG_TABLE
        POP     AX
        RET
DLG_ENTRY_OF ENDP

DLG_FOCUSED PROC NEAR
        PUSH    AX
        MOV     AL,DLG_FOCUS
        CALL    DLG_ENTRY_OF
        POP     AX
        RET
DLG_FOCUSED ENDP

; Focus control AL. A field gets its whole text selected and the cursor
; after it; a list without a selection keeps none.
DLG_SET_FOCUS PROC NEAR
        MOV     DLG_FOCUS,AL
        CALL    DLG_FOCUSED
        CMP     BYTE PTR [BX],DLG_FIELD
        JNE     SHORT DLG_SET_FOCUS_DONE
        MOV     DI,[BX+4]
        MOV     FIELD_POINTER,DI
        MOV     AL,[BX+7]
        MOV     FIELD_CAPACITY,AL
        XOR     CX,CX
DLG_SET_FOCUS_LENGTH:
        CMP     BYTE PTR [DI],0
        JE      SHORT DLG_SET_FOCUS_HAVE_LENGTH
        INC     DI
        INC     CX
        JMP     DLG_SET_FOCUS_LENGTH
DLG_SET_FOCUS_HAVE_LENGTH:
        MOV     FIELD_LENGTH,CX
        MOV     FIELD_POSITION,CX
        MOV     BYTE PTR FIELD_SELECT_ALL,1
DLG_SET_FOCUS_DONE:
        RET
DLG_SET_FOCUS ENDP

; Alt+letter (AH = scan code). Carry when a control took it: AH = 0 to
; redraw, else AL is a pressed button's code.
DLG_ALT_KEY PROC NEAR
        XOR     AL,AL
DLG_ALT_SCAN:
        CMP     AL,DLG_COUNT
        JAE     SHORT DLG_ALT_NONE
        CALL    DLG_ENTRY_OF
        CMP     AH,[BX+6]
        JE      SHORT DLG_ALT_FOUND
        INC     AL
        JMP     DLG_ALT_SCAN
DLG_ALT_FOUND:
        CALL    DLG_SET_FOCUS
        CALL    DLG_FOCUSED
        CMP     BYTE PTR [BX],DLG_BUTTON
        JNE     SHORT DLG_ALT_NOT_BUTTON
        MOV     AL,[BX+4]
        MOV     AH,1
        STC
        RET
DLG_ALT_NOT_BUTTON:
        CMP     BYTE PTR [BX],DLG_CHECK
        JNE     SHORT DLG_ALT_FOCUSED
        CALL    CHECK_TOGGLE
DLG_ALT_FOCUSED:
        XOR     AH,AH
        STC
        RET
DLG_ALT_NONE:
        CLC
        RET
DLG_ALT_KEY ENDP

; Carry: cell DH:DL lies on entry BX.
DLG_HIT PROC NEAR
        PUSH    AX
        MOV     AL,[BX+1]               ; row
        MOV     AH,[BX]
        CMP     AH,DLG_LIST
        JE      SHORT DLG_HIT_ROWS
        CMP     AH,DLG_RADIO
        JNE     SHORT DLG_HIT_ONE_ROW
DLG_HIT_ROWS:
        CMP     DH,AL
        JB      SHORT DLG_HIT_NO
        ADD     AL,[BX+3]
        CMP     DH,AL
        JAE     SHORT DLG_HIT_NO
        JMP     SHORT DLG_HIT_COLUMNS
DLG_HIT_ONE_ROW:
        CMP     DH,AL
        JNE     SHORT DLG_HIT_NO
DLG_HIT_COLUMNS:
        MOV     AL,[BX+2]
        CMP     DL,AL
        JB      SHORT DLG_HIT_NO
        MOV     AH,[BX]
        CMP     AH,DLG_CHECK
        JE      SHORT DLG_HIT_WIDE
        CMP     AH,DLG_RADIO
        JE      SHORT DLG_HIT_WIDE
        CMP     AH,DLG_LIST
        JNE     SHORT DLG_HIT_SIZE
        PUSH    SI
        MOV     SI,[BX+4]
        MOV     AH,[SI+LIST_WIDTH]
        CMP     BYTE PTR [SI+LIST_COLUMNS],1
        JE      SHORT DLG_HIT_LIST_WIDTH
        MOV     AH,42
DLG_HIT_LIST_WIDTH:
        POP     SI
        ADD     AL,AH
        JMP     SHORT DLG_HIT_END
DLG_HIT_WIDE:
        ADD     AL,24
        JMP     SHORT DLG_HIT_END
DLG_HIT_SIZE:
        ADD     AL,[BX+3]
DLG_HIT_END:
        CMP     DL,AL
        JAE     SHORT DLG_HIT_NO
        POP     AX
        STC
        RET
DLG_HIT_NO:
        POP     AX
        CLC
        RET
DLG_HIT ENDP

CHECK_TOGGLE PROC NEAR
        PUSH    SI
        MOV     SI,[BX+4]
        XOR     BYTE PTR [SI],1
        POP     SI
        RET
CHECK_TOGGLE ENDP

; Up/Down (and Left/Right) choose among the radio buttons sharing a
; variable; Space chooses the focused one.
RADIO_KEY PROC NEAR
        MOV     SI,[BX+4]
        OR      AL,AL
        JNZ     SHORT RADIO_KEY_DONE
        CMP     AH,48H
        JE      SHORT RADIO_KEY_PREVIOUS
        CMP     AH,4BH
        JE      SHORT RADIO_KEY_PREVIOUS
        CMP     AH,50H
        JE      SHORT RADIO_KEY_NEXT
        CMP     AH,4DH
        JNE     SHORT RADIO_KEY_DONE
RADIO_KEY_NEXT:
        MOV     AL,[SI]
        INC     AL
        CMP     AL,[BX+3]
        JAE     SHORT RADIO_KEY_DONE
        MOV     [SI],AL
        RET
RADIO_KEY_PREVIOUS:
        CMP     BYTE PTR [SI],0
        JE      SHORT RADIO_KEY_DONE
        DEC     BYTE PTR [SI]
RADIO_KEY_DONE:
        RET
RADIO_KEY ENDP

; A key for the focused field (FIELD_* describe it).
FIELD_KEY PROC NEAR
        MOV     DI,FIELD_POINTER
        OR      AL,AL
        JNZ     SHORT FIELD_KEY_CHARACTER
        MOV     BYTE PTR FIELD_SELECT_ALL,0
        MOV     CX,FIELD_POSITION
        CMP     AH,4BH
        JNE     SHORT FIELD_KEY_NOT_LEFT
        JCXZ    FIELD_KEY_DONE
        DEC     WORD PTR FIELD_POSITION
        RET
FIELD_KEY_NOT_LEFT:
        CMP     AH,4DH
        JNE     SHORT FIELD_KEY_NOT_RIGHT
        CMP     CX,FIELD_LENGTH
        JAE     SHORT FIELD_KEY_DONE
        INC     WORD PTR FIELD_POSITION
        RET
FIELD_KEY_NOT_RIGHT:
        CMP     AH,47H
        JNE     SHORT FIELD_KEY_NOT_HOME
        MOV     WORD PTR FIELD_POSITION,0
        RET
FIELD_KEY_NOT_HOME:
        CMP     AH,4FH
        JNE     SHORT FIELD_KEY_NOT_END
        MOV     AX,FIELD_LENGTH
        MOV     FIELD_POSITION,AX
        RET
FIELD_KEY_NOT_END:
        CMP     AH,53H
        JNE     SHORT FIELD_KEY_DONE
        CMP     CX,FIELD_LENGTH
        JAE     SHORT FIELD_KEY_DONE
        JMP     SHORT FIELD_KEY_REMOVE
FIELD_KEY_CHARACTER:
        CMP     AL,8
        JNE     SHORT FIELD_KEY_NOT_BACKSPACE
        CMP     BYTE PTR FIELD_SELECT_ALL,0
        JNE     SHORT FIELD_KEY_CLEAR
        MOV     CX,FIELD_POSITION
        JCXZ    FIELD_KEY_DONE
        DEC     WORD PTR FIELD_POSITION
FIELD_KEY_REMOVE:                       ; drop the byte at FIELD_POSITION
        MOV     SI,DI
        ADD     SI,FIELD_POSITION
FIELD_KEY_REMOVE_NEXT:
        MOV     AL,[SI+1]
        MOV     [SI],AL
        INC     SI
        OR      AL,AL
        JNZ     FIELD_KEY_REMOVE_NEXT
        DEC     WORD PTR FIELD_LENGTH
FIELD_KEY_DONE:
        RET
FIELD_KEY_NOT_BACKSPACE:
        CMP     AL,' '
        JB      SHORT FIELD_KEY_DONE
        CMP     BYTE PTR FIELD_SELECT_ALL,0
        JE      SHORT FIELD_KEY_INSERT
        PUSH    AX
        CALL    FIELD_KEY_CLEAR
        POP     AX
FIELD_KEY_INSERT:
        XOR     CX,CX
        MOV     CL,FIELD_CAPACITY
        CMP     FIELD_LENGTH,CX
        JAE     SHORT FIELD_KEY_DONE
        MOV     SI,DI
        ADD     SI,FIELD_LENGTH         ; shift the tail, terminator included
        MOV     CX,FIELD_LENGTH
        SUB     CX,FIELD_POSITION
        INC     CX
FIELD_KEY_SHIFT:
        MOV     AH,[SI]
        MOV     [SI+1],AH
        DEC     SI
        LOOP    FIELD_KEY_SHIFT
        MOV     SI,DI
        ADD     SI,FIELD_POSITION
        MOV     [SI],AL
        INC     WORD PTR FIELD_POSITION
        INC     WORD PTR FIELD_LENGTH
        RET
FIELD_KEY_CLEAR:
        MOV     BYTE PTR FIELD_SELECT_ALL,0
        MOV     BYTE PTR [DI],0
        MOV     WORD PTR FIELD_LENGTH,0
        MOV     WORD PTR FIELD_POSITION,0
        RET
FIELD_KEY ENDP

; Draw every control and place the cursor on the focused one.
DLG_DRAW PROC NEAR
        XOR     AL,AL
DLG_DRAW_NEXT:
        CMP     AL,DLG_COUNT
        JB      SHORT DLG_DRAW_ONE
        JMP     DLG_DRAW_CURSOR
DLG_DRAW_ONE:
        PUSH    AX
        CALL    DLG_ENTRY_OF
        MOV     DH,[BX+1]
        MOV     DL,[BX+2]
        MOV     CL,[BX]
        CMP     CL,DLG_FIELD
        JNE     SHORT DLG_DRAW_NOT_FIELD
        CALL    DRAW_FIELD_CONTROL
        JMP     DLG_DRAW_ADVANCE
DLG_DRAW_NOT_FIELD:
        CMP     CL,DLG_CHECK
        JNE     SHORT DLG_DRAW_NOT_CHECK
        MOV     SI,[BX+4]
        MOV     AL,' '
        CMP     BYTE PTR [SI],0
        JE      SHORT DLG_DRAW_CHECK_MARK
        MOV     AL,'X'
DLG_DRAW_CHECK_MARK:
        INC     DL
        JMP     SHORT DLG_DRAW_MARK
DLG_DRAW_NOT_CHECK:
        CMP     CL,DLG_RADIO
        JNE     SHORT DLG_DRAW_NOT_RADIO
        MOV     SI,[BX+4]               ; one dot per row, on the choice
        INC     DL
        XOR     CX,CX
DLG_DRAW_RADIO_ROW:
        MOV     AL,' '
        CMP     CL,[SI]
        JNE     SHORT DLG_DRAW_RADIO_DOT
        MOV     AL,07H                  ; the DOS 5 radio dot
DLG_DRAW_RADIO_DOT:
        PUSH    BX
        PUSH    CX
        MOV     BL,70H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     CX
        POP     BX
        INC     DH
        INC     CL
        CMP     CL,[BX+3]
        JB      DLG_DRAW_RADIO_ROW
        JMP     DLG_DRAW_ADVANCE
DLG_DRAW_MARK:
        PUSH    BX
        MOV     BL,70H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     BX
        JMP     SHORT DLG_DRAW_ADVANCE
DLG_DRAW_NOT_RADIO:
        CMP     CL,DLG_LIST
        JNE     SHORT DLG_DRAW_BUTTON
        CALL    DRAW_LIST_CONTROL
        JMP     SHORT DLG_DRAW_ADVANCE
DLG_DRAW_BUTTON:
        POP     AX
        PUSH    AX
        PUSH    BX
        MOV     AH,70H                  ; bright when focused, or default
        CMP     AL,DLG_FOCUS            ; while no button has the focus
        JE      SHORT DLG_DRAW_BUTTON_BRIGHT
        CMP     AL,DLG_DEFAULT
        JNE     SHORT DLG_DRAW_BUTTON_ATTR
        CALL    DLG_FOCUSED
        CMP     BYTE PTR [BX],DLG_BUTTON
        JE      SHORT DLG_DRAW_BUTTON_ATTR
DLG_DRAW_BUTTON_BRIGHT:
        MOV     AH,7FH
DLG_DRAW_BUTTON_ATTR:
        POP     BX
        MOV     CL,[BX+3]
        PUSH    CX
        MOV     BL,AH
        MOV     AL,'<'
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     CX
        DEC     CL
        ADD     DL,CL
        MOV     AL,'>'
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
DLG_DRAW_ADVANCE:
        POP     AX
        INC     AL
        JMP     DLG_DRAW_NEXT
DLG_DRAW_CURSOR:
        CALL    DLG_FOCUSED
        MOV     DH,[BX+1]
        MOV     DL,[BX+2]
        MOV     CL,[BX]
        CMP     CL,DLG_FIELD
        JNE     SHORT DLG_CURSOR_NOT_FIELD
        ADD     DL,BYTE PTR FIELD_POSITION
        JMP     SHORT DLG_CURSOR_SET
DLG_CURSOR_NOT_FIELD:
        CMP     CL,DLG_BUTTON
        JNE     SHORT DLG_CURSOR_NOT_BUTTON
        ADD     DL,[BX+7]
        JMP     SHORT DLG_CURSOR_SET
DLG_CURSOR_NOT_BUTTON:
        CMP     CL,DLG_LIST
        JNE     SHORT DLG_CURSOR_MARK
        CALL    LIST_CURSOR
        JMP     SHORT DLG_CURSOR_SET
DLG_CURSOR_MARK:
        CMP     CL,DLG_RADIO
        JNE     SHORT DLG_CURSOR_CHECK
        MOV     SI,[BX+4]
        ADD     DH,[SI]
DLG_CURSOR_CHECK:
        INC     DL
DLG_CURSOR_SET:
        CALL    DOSUI_SET_CURSOR
        CALL    DOSUI_SHOW_CURSOR
        RET
DLG_DRAW ENDP

; Entry BX, a field at DH:DL: the text (selected while focused and freshly
; entered), blanks up to its width.
DRAW_FIELD_CONTROL PROC NEAR
        MOV     SI,[BX+4]
        XOR     CX,CX
        MOV     CL,[BX+3]
        MOV     AH,70H
        CALL    DLG_FOCUSED_IS_BX
        JNE     SHORT DRAW_FIELD_NEXT
        CMP     BYTE PTR FIELD_SELECT_ALL,0
        JE      SHORT DRAW_FIELD_NEXT
        MOV     AH,07H
DRAW_FIELD_NEXT:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      SHORT DRAW_FIELD_BLANKS
        PUSH    BX
        PUSH    CX
        MOV     BL,AH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     CX
        POP     BX
        INC     SI
        INC     DL
        LOOP    DRAW_FIELD_NEXT
        RET
DRAW_FIELD_BLANKS:
        PUSH    BX
        MOV     AL,' '
        MOV     BL,70H
        CALL    DOSUI_FILL_ROW
        POP     BX
        RET
DRAW_FIELD_CONTROL ENDP

; ZF: entry BX is the focused one.
DLG_FOCUSED_IS_BX PROC NEAR
        PUSH    BX
        PUSH    AX
        MOV     AX,BX
        CALL    DLG_FOCUSED
        CMP     AX,BX
        POP     AX
        POP     BX
        RET
DLG_FOCUSED_IS_BX ENDP

; ---- Lists --------------------------------------------------------------
; Entries are fixed-size, zero-terminated names. One column shows SIZE
; rows with a vertical scroll bar; three columns of 14 cells show 3*SIZE
; and scroll by columns. The selection is highlighted (07h) whether or not
; the list has the focus; the cursor sits on the selection, or on the first
; shown entry while there is none.
DRAW_LIST_CONTROL PROC NEAR
        PUSH    BX
        MOV     SI,[BX+4]
        MOV     CH,[BX+3]               ; rows
        MOV     DI,[SI+LIST_TOP]
        MOV     AX,[DI]
        MOV     LIST_DRAW_INDEX,AX
        MOV     CL,0                    ; column number
DRAW_LIST_COLUMN:
        PUSH    DX
        MOV     AH,CH
DRAW_LIST_ROW:
        PUSH    AX
        CALL    DRAW_LIST_SLOT
        POP     AX
        INC     WORD PTR LIST_DRAW_INDEX
        INC     DH
        DEC     AH
        JNZ     DRAW_LIST_ROW
        POP     DX
        INC     CL
        CMP     CL,[SI+LIST_COLUMNS]
        JAE     SHORT DRAW_LIST_BAR
        ADD     DL,14
        JMP     DRAW_LIST_COLUMN
DRAW_LIST_BAR:
        POP     BX
        CALL    DRAW_LIST_SCROLL_BAR
        RET
DRAW_LIST_CONTROL ENDP

; Slot LIST_DRAW_INDEX of list SI at DH:DL.
DRAW_LIST_SLOT PROC NEAR
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     BL,70H
        MOV     AX,LIST_DRAW_INDEX
        MOV     DI,[SI+LIST_SELECTED]
        CMP     AX,[DI]
        JNE     SHORT DRAW_LIST_SLOT_ATTR
        MOV     BL,07H
DRAW_LIST_SLOT_ATTR:
        XOR     CX,CX
        MOV     CL,[SI+LIST_WIDTH]
        MOV     AL,' '
        CALL    DOSUI_FILL_ROW
        MOV     AX,LIST_DRAW_INDEX
        MOV     DI,[SI+LIST_COUNT]
        CMP     AX,[DI]
        JAE     SHORT DRAW_LIST_SLOT_DONE
        CALL    LIST_ENTRY_ADDRESS      ; DI = entry AX
        MOV     SI,DI
        INC     DL
        CALL    DOSUI_WRITE_Z
DRAW_LIST_SLOT_DONE:
        POP     SI
        POP     DX
        POP     CX
        RET
DRAW_LIST_SLOT ENDP

; DI = address of entry AX of list SI.
LIST_ENTRY_ADDRESS PROC NEAR
        PUSH    AX
        PUSH    CX
        PUSH    DX
        XOR     CX,CX
        MOV     CL,[SI+LIST_STRIDE]
        MUL     CX
        ADD     AX,[SI+LIST_ITEMS]
        MOV     DI,AX
        POP     DX
        POP     CX
        POP     AX
        RET
LIST_ENTRY_ADDRESS ENDP

; The scroll bar of list SI (entry BX): vertical at column LIST_BAR over
; the rows, or, for three columns, on the row below from column LIST_BAR
; for 43 cells. Arrows at both ends; the thumb (07h) in proportion.
DRAW_LIST_SCROLL_BAR PROC NEAR
        CMP     BYTE PTR [SI+LIST_BAR],0
        JNE     SHORT DRAW_LIST_SCROLL_HAVE
        RET
DRAW_LIST_SCROLL_HAVE:
        PUSH    BX
        XOR     CX,CX
        MOV     CL,[BX+3]               ; rows
        MOV     DI,[SI+LIST_COUNT]
        MOV     AX,[DI]                 ; AX = count
        MOV     DI,[SI+LIST_TOP]
        MOV     DI,[DI]                 ; DI = first shown
        MOV     DH,[BX+1]
        MOV     DL,[SI+LIST_BAR]
        CMP     BYTE PTR [SI+LIST_COLUMNS],1
        JE      SHORT DRAW_LIST_SCROLL_VERTICAL
        ; across: in columns of CX entries
        PUSH    AX
        MOV     AX,DI
        XOR     DX,DX
        DIV     CX
        MOV     DI,AX                   ; first shown column
        POP     AX
        ADD     AX,CX
        DEC     AX
        XOR     DX,DX
        DIV     CX                      ; AX = columns
        SUB     AX,3
        JA      SHORT DRAW_LIST_ACROSS_RANGE
        XOR     AX,AX
DRAW_LIST_ACROSS_RANGE:
        MOV     BX,41                   ; track cells
        CALL    LIST_THUMB_OFFSET       ; AX = thumb offset
        POP     BX
        PUSH    AX
        MOV     DH,[BX+1]
        ADD     DH,[BX+3]
        MOV     DL,[SI+LIST_BAR]
        MOV     AL,1BH
        MOV     BL,70H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        INC     DL
        MOV     AL,0B0H
        MOV     CX,41
        CALL    DOSUI_FILL_ROW
        ADD     DL,41
        MOV     AL,1AH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        SUB     DL,41
        POP     AX
        ADD     DL,AL
        JMP     SHORT DRAW_LIST_THUMB
DRAW_LIST_SCROLL_VERTICAL:
        SUB     AX,CX
        JA      SHORT DRAW_LIST_VERTICAL_RANGE
        XOR     AX,AX
DRAW_LIST_VERTICAL_RANGE:
        PUSH    CX
        MOV     BX,CX
        SUB     BX,2                    ; track cells
        CALL    LIST_THUMB_OFFSET
        POP     CX
        POP     BX
        ADD     AL,[BX+1]
        INC     AL
        PUSH    AX                      ; AL = thumb row
        MOV     AL,18H
        MOV     BL,70H
        PUSH    CX
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     CX
        SUB     CX,2
DRAW_LIST_VERTICAL_TRACK:
        INC     DH
        MOV     AL,0B0H
        PUSH    CX
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     CX
        LOOP    DRAW_LIST_VERTICAL_TRACK
        INC     DH
        MOV     AL,19H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     AX
        MOV     DH,AL
DRAW_LIST_THUMB:
        MOV     AL,' '
        MOV     BL,07H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        RET
DRAW_LIST_SCROLL_BAR ENDP

; AX = range (positions beyond the first), DI = position, BX = track
; cells. AX returns the thumb offset DI * (BX - 1) / range, 0 for no range.
LIST_THUMB_OFFSET PROC NEAR
        OR      AX,AX
        JZ      SHORT LIST_THUMB_DONE
        PUSH    CX
        PUSH    DX
        MOV     CX,AX
        CMP     DI,CX
        JBE     SHORT LIST_THUMB_HAVE
        MOV     DI,CX
LIST_THUMB_HAVE:
        MOV     AX,BX
        DEC     AX
        MUL     DI
        DIV     CX
        POP     DX
        POP     CX
LIST_THUMB_DONE:
        RET
LIST_THUMB_OFFSET ENDP

; DH:DL from the entry's origin to the cursor cell of list entry BX.
LIST_CURSOR PROC NEAR
        PUSH    SI
        MOV     SI,[BX+4]
        MOV     DI,[SI+LIST_SELECTED]
        MOV     AX,[DI]
        CMP     AX,0FFFFH
        JNE     SHORT LIST_CURSOR_HAVE
        MOV     DI,[SI+LIST_TOP]
        MOV     AX,[DI]
LIST_CURSOR_HAVE:
        MOV     DI,[SI+LIST_TOP]
        SUB     AX,[DI]
        XOR     CX,CX
        MOV     CL,[BX+3]
        PUSH    DX
        XOR     DX,DX
        DIV     CX                      ; AX = column, DX = row
        MOV     CX,DX
        POP     DX
        ADD     DH,CL
        MOV     CL,14
        MUL     CL
        ADD     DL,AL
        INC     DL
        POP     SI
        RET
LIST_CURSOR ENDP

; A key for list entry BX: Up/Down a row, Left/Right a column (three
; columns), PgUp/PgDn a page, Home/End. The shown range follows.
LIST_KEY PROC NEAR
        OR      AL,AL
        JZ      SHORT LIST_KEY_EXTENDED
        RET
LIST_KEY_EXTENDED:
        MOV     LIST_LAST_SCAN,AH
        MOV     SI,[BX+4]
        MOV     DI,[SI+LIST_COUNT]
        MOV     CX,[DI]
        JCXZ    LIST_KEY_DONE
        MOV     DI,[SI+LIST_SELECTED]
        MOV     DX,[DI]                 ; DX = selection
        XOR     DI,DI
        MOV     AL,[BX+3]
        XOR     AH,AH
        MOV     LIST_PAGE,AX
        CMP     BYTE PTR [SI+LIST_COLUMNS],1
        JE      SHORT LIST_KEY_PAGE_READY
        MOV     LIST_STEP,AX            ; a column
        MOV     DI,3
        PUSH    DX
        MUL     DI
        POP     DX
        MOV     LIST_PAGE,AX
LIST_KEY_PAGE_READY:
        CMP     DX,0FFFFH               ; nothing selected: the first entry
        JNE     SHORT LIST_KEY_HAVE_SELECTION
        MOV     DI,[SI+LIST_TOP]
        MOV     DX,[DI]
        JMP     SHORT LIST_KEY_SET
LIST_KEY_HAVE_SELECTION:
        PUSH    BX
        CALL    LIST_KEY_MOVE           ; DX = new selection
        POP     BX
LIST_KEY_SET:
        CMP     DX,CX
        JB      SHORT LIST_KEY_IN_RANGE
        MOV     DX,CX
        DEC     DX
LIST_KEY_IN_RANGE:
        CALL    LIST_SELECT
LIST_KEY_DONE:
        RET
LIST_KEY ENDP

; DX = selection, CX = count, AH (from the caller's key) re-read here.
LIST_KEY_MOVE PROC NEAR
        MOV     AH,LIST_LAST_SCAN
        CMP     AH,50H                  ; Down
        JNE     SHORT LIST_MOVE_NOT_DOWN
        INC     DX
        RET
LIST_MOVE_NOT_DOWN:
        CMP     AH,48H                  ; Up
        JNE     SHORT LIST_MOVE_NOT_UP
        OR      DX,DX
        JZ      SHORT LIST_MOVE_DONE
        DEC     DX
        RET
LIST_MOVE_NOT_UP:
        CMP     AH,47H
        JNE     SHORT LIST_MOVE_NOT_HOME
        XOR     DX,DX
        RET
LIST_MOVE_NOT_HOME:
        CMP     AH,4FH
        JNE     SHORT LIST_MOVE_NOT_END
        MOV     DX,CX
        DEC     DX
        RET
LIST_MOVE_NOT_END:
        CMP     AH,51H                  ; PgDn
        JNE     SHORT LIST_MOVE_NOT_PAGE_DOWN
        ADD     DX,LIST_PAGE
        RET
LIST_MOVE_NOT_PAGE_DOWN:
        CMP     AH,49H                  ; PgUp
        JNE     SHORT LIST_MOVE_NOT_PAGE_UP
        SUB     DX,LIST_PAGE
        JNC     SHORT LIST_MOVE_DONE
        XOR     DX,DX
        RET
LIST_MOVE_NOT_PAGE_UP:
        CMP     BYTE PTR [SI+LIST_COLUMNS],1
        JE      SHORT LIST_MOVE_DONE
        CMP     AH,4DH                  ; Right: a column on
        JNE     SHORT LIST_MOVE_NOT_RIGHT
        ADD     DX,LIST_STEP
        RET
LIST_MOVE_NOT_RIGHT:
        CMP     AH,4BH
        JNE     SHORT LIST_MOVE_DONE
        SUB     DX,LIST_STEP
        JNC     SHORT LIST_MOVE_DONE
        XOR     DX,DX
LIST_MOVE_DONE:
        RET
LIST_KEY_MOVE ENDP

; Select entry DX of list SI (entry BX): scroll it into view, then let the
; dialog react (LIST_ON_SELECT).
LIST_SELECT PROC NEAR
        MOV     DI,[SI+LIST_SELECTED]
        MOV     [DI],DX
        MOV     DI,[SI+LIST_TOP]
        MOV     AX,[DI]
        CMP     DX,AX
        JAE     SHORT LIST_SELECT_NOT_ABOVE
        MOV     AX,DX                   ; scroll back to it (to its column)
        JMP     SHORT LIST_SELECT_ALIGN
LIST_SELECT_NOT_ABOVE:
        ADD     AX,LIST_PAGE
        CMP     DX,AX
        JB      SHORT LIST_SELECT_SHOWN
        MOV     AX,DX
        SUB     AX,LIST_PAGE
        INC     AX
        CMP     BYTE PTR [SI+LIST_COLUMNS],1
        JE      SHORT LIST_SELECT_STORE
        ADD     AX,LIST_STEP            ; whole columns
        DEC     AX
LIST_SELECT_ALIGN:
        CMP     BYTE PTR [SI+LIST_COLUMNS],1
        JE      SHORT LIST_SELECT_STORE
        PUSH    DX
        XOR     DX,DX
        DIV     WORD PTR LIST_STEP
        MUL     WORD PTR LIST_STEP
        POP     DX
LIST_SELECT_STORE:
        MOV     [DI],AX
LIST_SELECT_SHOWN:
        MOV     DX,[SI+LIST_ON_SELECT]
        OR      DX,DX
        JZ      SHORT LIST_SELECT_DONE
        CALL    DX
LIST_SELECT_DONE:
        RET
LIST_SELECT ENDP

; A click DH:DL on list entry BX selects the entry under it.
LIST_CLICK PROC NEAR
        MOV     SI,[BX+4]
        MOV     AL,[BX+3]
        XOR     AH,AH
        MOV     LIST_PAGE,AX
        MOV     LIST_STEP,AX
        MOV     AL,DH
        SUB     AL,[BX+1]               ; row
        XOR     AH,AH
        PUSH    AX
        MOV     AL,DL
        SUB     AL,[BX+2]
        MOV     CL,14
        DIV     CL                      ; AL = column
        XOR     AH,AH
        MUL     BYTE PTR [BX+3]
        POP     DX
        ADD     DX,AX
        MOV     DI,[SI+LIST_TOP]
        ADD     DX,[DI]
        MOV     DI,[SI+LIST_COUNT]
        CMP     DX,[DI]
        JAE     SHORT LIST_CLICK_DONE
        CMP     BYTE PTR [SI+LIST_COLUMNS],1
        JE      SHORT LIST_CLICK_SELECT
        MOV     AX,LIST_PAGE
        MOV     CX,3
        PUSH    DX
        MUL     CX
        POP     DX
        MOV     LIST_PAGE,AX
LIST_CLICK_SELECT:
        CALL    LIST_SELECT
LIST_CLICK_DONE:
        RET
LIST_CLICK ENDP

; Find and Verify: each match from the cursor on is selected and the
; Change dialog at the bottom asks Change, Skip or Cancel, as in DOS 5.
; Find and Verify, as DOS 5 does it (QEMU captures CF, CF2, 2026-11-11):
; the first match after the cursor (never one starting at it, so a Find's
; selected match is not offered again), then on through the file and from
; its top back to the cursor; Change replaces, Skip moves past; Cancel puts
; the cursor back where it was, "Change complete" leaves it the searched
; length past the last match's start.
CHANGE_VERIFY PROC NEAR
        MOV     WORD PTR CHANGE_COUNT,0
        MOV     BYTE PTR CHANGE_WRAPPED,0
        MOV     SI,OFFSET SEARCH_PATTERN
        CALL    STRING_LENGTH
        MOV     REPLACE_SEARCH_LENGTH,CX
        MOV     SI,OFFSET REPLACE_PATTERN
        CALL    STRING_LENGTH
        MOV     REPLACE_REPLACEMENT_LENGTH,CX
        CALL    COLLAPSE_SELECTION
        CALL    CURSOR_GLOBAL           ; offsets in the whole text
        MOV     CHANGE_ORIGIN,AX
        MOV     CHANGE_ORIGIN+2,DX
        CALL    CURSOR_GLOBAL_NEXT      ; DOS 5 looks from after the cursor
CHANGE_VERIFY_FROM:
        MOV     FIND_START,AX
        MOV     FIND_START+2,DX
CHANGE_VERIFY_NEXT:
        CALL    COLLAPSE_SELECTION
        MOV     AX,FIND_START
        MOV     DX,FIND_START+2
        CALL    FIND_FROM_GLOBAL
        JNC     SHORT CHANGE_VERIFY_FOUND_ANY
        CMP     BYTE PTR CHANGE_WRAPPED,0
        JNE     SHORT CHANGE_VERIFY_END
        MOV     BYTE PTR CHANGE_WRAPPED,1  ; then from the top, up to the cursor
        XOR     AX,AX
        XOR     DX,DX
        JMP     CHANGE_VERIFY_FROM
CHANGE_VERIFY_FOUND_ANY:
        CMP     BYTE PTR CHANGE_WRAPPED,0
        JE      SHORT CHANGE_VERIFY_FOUND
        CALL    CURSOR_GLOBAL
        CMP     DX,CHANGE_ORIGIN+2
        JNE     CHANGE_VERIFY_WRAP_ORDERED
        CMP     AX,CHANGE_ORIGIN
CHANGE_VERIFY_WRAP_ORDERED:
        JBE     SHORT CHANGE_VERIFY_FOUND
CHANGE_VERIFY_END:
        MOV     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        MOV     WORD PTR SEARCH_HIGHLIGHT_END,0FFFFH
        CMP     WORD PTR CHANGE_COUNT,0
        JE      SHORT CHANGE_VERIFY_NONE
        MOV     AX,CHANGE_LAST          ; DOS 5 (plan CF2): the last match's
        MOV     DX,CHANGE_LAST+2        ; start plus the searched length
        ADD     AX,REPLACE_SEARCH_LENGTH
        ADC     DX,0
        MOV     BX,AX
        MOV     CX,DX
        CALL    DOC_LENGTH
        CMP     CX,DX
        JNE     CHANGE_VERIFY_END_ORDERED
        CMP     BX,AX
CHANGE_VERIFY_END_ORDERED:
        JA      CHANGE_VERIFY_PLACED
        MOV     AX,BX
        MOV     DX,CX
CHANGE_VERIFY_PLACED:
        CALL    SET_CURSOR_GLOBAL
        CALL    RESET_NAVIGATION_COLUMNS
        CALL    SHOW_CHANGE_COMPLETE
        RET
CHANGE_VERIFY_NONE:
        MOV     AX,CHANGE_ORIGIN
        MOV     DX,CHANGE_ORIGIN+2
        CALL    SET_CURSOR_GLOBAL
        CALL    SHOW_NOT_FOUND
        RET
CHANGE_VERIFY_FOUND:
        INC     WORD PTR CHANGE_COUNT
        CALL    CURSOR_GLOBAL
        MOV     CHANGE_LAST,AX
        MOV     CHANGE_LAST+2,DX
        MOV     AX,SEARCH_HIGHLIGHT_END
        MOV     BLOCK_MARK,AX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     BYTE PTR SHIFT_SELECTION,1
        MOV     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        MOV     WORD PTR SEARCH_HIGHLIGHT_END,0FFFFH
CHANGE_VERIFY_ASK:
        CALL    DRAW_SCREEN
        MOV     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     AL,VERIFY_ROW_OFFSET    ; at the bottom, not centred
        CALL    SET_ROW_OFFSET
        MOV     SI,OFFSET VERIFY_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET CHANGE_TITLE_TEXT
        MOV     BL,70H
        MOV     DH,19
        MOV     DL,35
        CALL    DOSUI_WRITE_Z
        MOV     DH,21
        MOV     DL,13
        MOV     CX,53
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET VERIFY_BUTTONS_TEXT
        MOV     DH,22
        MOV     DL,17
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET VERIFY_CONTROLS
        MOV     CX,0004H
        XOR     AL,AL
        CALL    RUN_DIALOG
        CMP     AL,DLG_HELP
        JNE     SHORT CHANGE_VERIFY_ANSWER
        CALL    SHOW_KEYBOARD_HELP
        JMP     CHANGE_VERIFY_ASK
CHANGE_VERIFY_ANSWER:
        CMP     AL,1
        JNE     SHORT CHANGE_VERIFY_NOT_CHANGE
        CALL    COLLAPSE_SELECTION
        CALL    CURSOR_GLOBAL           ; a change before the cursor moves it
        CMP     DX,CHANGE_ORIGIN+2
        JNE     CHANGE_VERIFY_ORIGIN_ORDERED
        CMP     AX,CHANGE_ORIGIN
CHANGE_VERIFY_ORIGIN_ORDERED:
        JAE     SHORT CHANGE_VERIFY_REPLACE
        MOV     AX,REPLACE_REPLACEMENT_LENGTH
        SUB     AX,REPLACE_SEARCH_LENGTH
        CWD
        ADD     CHANGE_ORIGIN,AX
        ADC     CHANGE_ORIGIN+2,DX
CHANGE_VERIFY_REPLACE:
        CALL    MAKE_VIEW_ROOM          ; room for the change
        CALL    REPLACE_CURRENT
        JNC     CHANGE_VERIFY_ON
        MOV     BYTE PTR MEMORY_FULL,1
        JMP     CHANGE_VERIFY_CANCEL
CHANGE_VERIFY_ON:
        CALL    CURSOR_GLOBAL
        JMP     CHANGE_VERIFY_FROM
CHANGE_VERIFY_NOT_CHANGE:
        CMP     AL,2
        JNE     SHORT CHANGE_VERIFY_CANCEL
        MOV     AX,BLOCK_MARK           ; Skip: on past this match
        MOV     CURSOR_OFFSET,AX
        JMP     CHANGE_VERIFY_ON
CHANGE_VERIFY_CANCEL:                   ; DOS 5 (plan CF): back where it began
        CALL    COLLAPSE_SELECTION
        MOV     AX,CHANGE_ORIGIN
        MOV     DX,CHANGE_ORIGIN+2
        CALL    SET_CURSOR_GLOBAL
        CALL    RESET_NAVIGATION_COLUMNS
        RET
CHANGE_VERIFY ENDP

; CX = the length of the zero-terminated string at SI.
STRING_LENGTH PROC NEAR
        PUSH    SI
        XOR     CX,CX
STRING_LENGTH_NEXT:
        CMP     BYTE PTR [SI],0
        JE      SHORT STRING_LENGTH_DONE
        INC     SI
        INC     CX
        JMP     STRING_LENGTH_NEXT
STRING_LENGTH_DONE:
        POP     SI
        RET
STRING_LENGTH ENDP

; Find and Change: a cancelled dialog leaves the search text and options
; as they were.
SAVE_SEARCH_SETTINGS PROC NEAR
        PUSH    SI
        PUSH    DI
        PUSH    CX
        MOV     SI,OFFSET SEARCH_PATTERN
        MOV     DI,OFFSET SAVED_SEARCH
        MOV     CX,32
        REP     MOVSB
        MOV     AL,MATCH_CASE
        MOV     SAVED_MATCH_CASE,AL
        MOV     AL,WHOLE_WORD
        MOV     SAVED_WHOLE_WORD,AL
        POP     CX
        POP     DI
        POP     SI
        RET
SAVE_SEARCH_SETTINGS ENDP

KEEP_OR_RESTORE_SEARCH PROC NEAR
        OR      AL,AL
        JNZ     SHORT KEEP_SEARCH_DONE
        PUSH    AX
        MOV     SI,OFFSET SAVED_SEARCH
        MOV     DI,OFFSET SEARCH_PATTERN
        MOV     CX,32
        REP     MOVSB
        MOV     AL,SAVED_MATCH_CASE
        MOV     MATCH_CASE,AL
        MOV     AL,SAVED_WHOLE_WORD
        MOV     WHOLE_WORD,AL
        POP     AX
KEEP_SEARCH_DONE:
        RET
KEEP_OR_RESTORE_SEARCH ENDP

; Carry: the user cancelled. A modified file gets DOS 5's "Loaded file is
; not saved. Save it now?"; Yes saves (a failed or cancelled save cancels).
OFFER_SAVE PROC NEAR
        CMP     BYTE PTR MODIFIED,0
        JE      SHORT OFFER_SAVE_GO
        CALL    CONFIRM_EXIT
        OR      AL,AL
        JZ      SHORT OFFER_SAVE_CANCEL
        CMP     AL,2
        JE      SHORT OFFER_SAVE_GO
        CALL    SAVE_FILE
        JC      SHORT OFFER_SAVE_CANCEL
OFFER_SAVE_GO:
        CLC
        RET
OFFER_SAVE_CANCEL:
        STC
        RET
OFFER_SAVE ENDP

; An empty Untitled document, as after File New.
CLEAR_DOCUMENT PROC NEAR
        CALL    CLEAR_TEXT
        XOR     AX,AX
        MOV     TAB_FILE,AL
        MOV     LEFT_COLUMN,AX
        MOV     MODIFIED,AL
        MOV     SAVE_DISABLED,AL
        MOV     SHIFT_SELECTION,AL
        MOV     FILE_NAME,AL
        MOV     DISPLAY_NAME,AL
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        CALL    RESET_NAVIGATION_COLUMNS
        RET
CLEAR_DOCUMENT ENDP

; The DOS 5 Open dialog. Carry: cancelled; else OPEN_NAME names the file.
SHOW_OPEN_DIALOG PROC NEAR
        MOV     SI,OFFSET OPEN_PATTERN
        MOV     DI,OFFSET OPEN_NAME
        MOV     CX,13
        REP     MOVSB
OPEN_DIALOG_REFILL:
        CALL    FILL_OPEN_LISTS
OPEN_DIALOG_DRAW:
        CALL    DRAW_SCREEN
        MOV     BYTE PTR MENU_ACTIVE,MENU_FILE
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET OPEN_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BL,70H
        MOV     SI,OFFSET OPEN_TITLE_TEXT
        MOV     DH,2
        MOV     DL,36
        CALL    DOSUI_WRITE_Z
        MOV     DH,3
        MOV     DL,19
        MOV     CX,52
        CALL    DRAW_DIALOG_INPUT
        MOV     SI,OFFSET FILE_NAME_TEXT
        MOV     DH,4
        MOV     DL,8
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET OPEN_PATH
        MOV     DH,6
        MOV     DL,8
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET OPEN_FILES_TEXT
        MOV     DH,7
        MOV     DL,28
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET SAVE_AS_DIRS_TEXT
        MOV     DL,57
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET OPEN_FILES_BOX
        CALL    DRAW_LIST_BOX
        MOV     SI,OFFSET OPEN_DIRS_BOX
        CALL    DRAW_LIST_BOX
        MOV     DH,19
        MOV     DL,6
        MOV     CX,67
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET OPEN_BUTTONS_TEXT
        MOV     DH,20
        MOV     DL,18
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET OPEN_CONTROLS
        MOV     CX,0306H                ; OK is the default
        XOR     AL,AL
        CALL    RUN_DIALOG
        CMP     AL,DLG_HELP
        JNE     SHORT OPEN_DIALOG_ANSWER
        CALL    SHOW_KEYBOARD_HELP
        JMP     OPEN_DIALOG_DRAW
OPEN_DIALOG_ANSWER:
        OR      AL,AL
        JNZ     SHORT OPEN_DIALOG_OK
        STC
        RET
OPEN_DIALOG_OK:
        MOV     SI,OFFSET OPEN_NAME
        CMP     BYTE PTR [SI],0
        JNE     SHORT OPEN_DIALOG_WILD
        JMP     OPEN_DIALOG_DRAW
OPEN_DIALOG_WILD:                       ; a pattern: list it
        LODSB
        OR      AL,AL
        JZ      SHORT OPEN_DIALOG_NOT_WILD
        CMP     AL,'*'
        JE      SHORT OPEN_DIALOG_PATTERN
        CMP     AL,'?'
        JNE     OPEN_DIALOG_WILD
OPEN_DIALOG_PATTERN:
        MOV     SI,OFFSET OPEN_NAME
        MOV     DI,OFFSET OPEN_PATTERN
        MOV     CX,13
        REP     MOVSB
        MOV     BYTE PTR OPEN_PATTERN+12,0
        JMP     OPEN_DIALOG_REFILL
OPEN_DIALOG_NOT_WILD:
        CMP     BYTE PTR OPEN_NAME+1,':'  ; "X:" changes the drive
        JNE     SHORT OPEN_DIALOG_NOT_DRIVE
        CMP     BYTE PTR OPEN_NAME+2,0
        JNE     SHORT OPEN_DIALOG_NOT_DRIVE
        MOV     DL,OPEN_NAME
        AND     DL,0DFH
        SUB     DL,'A'
        MOV     AH,0EH
        INT     21H
        JMP     SHORT OPEN_DIALOG_LISTED
OPEN_DIALOG_NOT_DRIVE:
        MOV     DX,OFFSET OPEN_NAME     ; a directory: go there
        MOV     AH,3BH
        INT     21H
        JC      SHORT OPEN_DIALOG_FILE
OPEN_DIALOG_LISTED:
        MOV     SI,OFFSET OPEN_PATTERN
        MOV     DI,OFFSET OPEN_NAME
        MOV     CX,13
        REP     MOVSB
        JMP     OPEN_DIALOG_REFILL
OPEN_DIALOG_FILE:                       ; DOS 5 (plan OE): a bad name and a
        MOV     SI,OFFSET OPEN_NAME     ; missing path stay in Open, a missing
        CALL    CHECK_FILE_NAME         ; drive offers Retry, a missing file
        JC      SHORT OPEN_DIALOG_REFUSED ; goes back to the editor
        MOV     SI,OFFSET OPEN_NAME
        CALL    CHECK_FILE_DRIVE
        JNC     SHORT OPEN_DIALOG_EXISTS
        CALL    SHOW_DEVICE_UNAVAILABLE
        JC      OPEN_DIALOG_FILE        ; Retry
        JMP     OPEN_DIALOG_DRAW
OPEN_DIALOG_EXISTS:
        MOV     DX,OFFSET OPEN_NAME
        MOV     AX,4300H
        INT     21H
        JNC     SHORT OPEN_DIALOG_CHOSEN
        CALL    NOTE_DOS_ERROR          ; 3 for a missing directory, as DOS 5
        CMP     WORD PTR FILE_ERROR,2
        JNE     SHORT OPEN_DIALOG_REFUSED
        CALL    SHOW_FILE_ERROR
        STC
        RET
OPEN_DIALOG_REFUSED:
        CALL    SHOW_FILE_ERROR_OVER
        JMP     OPEN_DIALOG_DRAW
OPEN_DIALOG_CHOSEN:
        CLC
        RET
SHOW_OPEN_DIALOG ENDP

; Fill the Files list (OPEN_PATTERN, sorted), the Dirs/Drives list
; (".." outside the root, subdirectories, then every drive) and the path.
FILL_OPEN_LISTS PROC NEAR
        MOV     DX,OFFSET OPEN_DTA
        MOV     AH,1AH
        INT     21H
        MOV     WORD PTR OPEN_FILE_COUNT,0
        MOV     WORD PTR OPEN_FILE_TOP,0
        MOV     WORD PTR OPEN_FILE_SELECTED,0FFFFH
        MOV     DX,OFFSET OPEN_PATTERN
        XOR     CX,CX
        MOV     AH,4EH
FILL_OPEN_FILES_NEXT:
        INT     21H
        JC      SHORT FILL_OPEN_FILES_DONE
        CMP     WORD PTR OPEN_FILE_COUNT,OPEN_FILE_MAX
        JAE     SHORT FILL_OPEN_FILES_DONE
        MOV     AX,OPEN_FILE_COUNT
        MOV     DI,OFFSET OPEN_FILE_ENTRIES
        CALL    STORE_LIST_NAME
        INC     WORD PTR OPEN_FILE_COUNT
        MOV     AH,4FH
        JMP     FILL_OPEN_FILES_NEXT
FILL_OPEN_FILES_DONE:
        MOV     SI,OFFSET OPEN_FILE_ENTRIES
        MOV     CX,OPEN_FILE_COUNT
        CALL    SORT_NAMES
        ; directories
        MOV     WORD PTR OPEN_DIR_COUNT,0
        MOV     WORD PTR OPEN_DIR_TOP,0
        MOV     WORD PTR OPEN_DIR_SELECTED,0FFFFH
        MOV     DX,OFFSET ALL_FILES_PATTERN
        MOV     CX,10H
        MOV     AH,4EH
FILL_OPEN_DIRS_NEXT:
        INT     21H
        JC      SHORT FILL_OPEN_DIRS_DONE
        TEST    BYTE PTR OPEN_DTA+15H,10H
        JZ      SHORT FILL_OPEN_DIRS_SKIP
        CMP     BYTE PTR OPEN_DTA+1EH,'.'   ; "." and "..": ".." is added below
        JE      SHORT FILL_OPEN_DIRS_SKIP
        CMP     WORD PTR OPEN_DIR_COUNT,OPEN_DIR_MAX-27
        JAE     SHORT FILL_OPEN_DIRS_DONE
        MOV     AX,OPEN_DIR_COUNT
        MOV     DI,OFFSET OPEN_DIR_ENTRIES
        CALL    STORE_LIST_NAME
        INC     WORD PTR OPEN_DIR_COUNT
FILL_OPEN_DIRS_SKIP:
        MOV     AH,4FH
        JMP     FILL_OPEN_DIRS_NEXT
FILL_OPEN_DIRS_DONE:
        MOV     SI,OFFSET OPEN_DIR_ENTRIES
        MOV     CX,OPEN_DIR_COUNT
        CALL    SORT_NAMES
        ; ".." leads the list outside the root, as DOS 5 shows it; it is not
        ; taken from the search, which not every drive answers with dot
        ; entries.
        MOV     SI,OFFSET OPEN_PATH
        XOR     DL,DL
        MOV     AH,47H
        INT     21H
        JC      SHORT FILL_OPEN_PARENT_DONE
        CMP     BYTE PTR OPEN_PATH,0
        JE      SHORT FILL_OPEN_PARENT_DONE
        MOV     CX,OPEN_DIR_COUNT
        MOV     AX,CX
        MOV     BX,13
        MUL     BX
        MOV     SI,OFFSET OPEN_DIR_ENTRIES-1
        ADD     SI,AX                   ; the last byte of the list
        MOV     DI,SI
        ADD     DI,13
        MOV     CX,AX
        STD
        REP     MOVSB
        CLD
        MOV     WORD PTR OPEN_DIR_ENTRIES,'..'
        MOV     BYTE PTR OPEN_DIR_ENTRIES+2,0
        INC     WORD PTR OPEN_DIR_COUNT
FILL_OPEN_PARENT_DONE:
        MOV     BYTE PTR DRIVE_PROBE,'A'
FILL_OPEN_DRIVES_NEXT:
        MOV     SI,OFFSET DRIVE_PROBE
        MOV     DI,OFFSET OPEN_FCB
        MOV     AX,2900H                ; AL=FFh: no such drive
        INT     21H
        CMP     AL,0FFH
        JE      SHORT FILL_OPEN_DRIVES_SKIP
        MOV     AX,OPEN_DIR_COUNT
        MOV     CX,13
        MUL     CX
        ADD     AX,OFFSET OPEN_DIR_ENTRIES
        MOV     DI,AX
        MOV     AL,DRIVE_PROBE
        MOV     BYTE PTR [DI],'['
        MOV     BYTE PTR [DI+1],'-'
        MOV     [DI+2],AL
        MOV     BYTE PTR [DI+3],'-'
        MOV     BYTE PTR [DI+4],']'
        MOV     BYTE PTR [DI+5],0
        INC     WORD PTR OPEN_DIR_COUNT
FILL_OPEN_DRIVES_SKIP:
        INC     BYTE PTR DRIVE_PROBE
        CMP     BYTE PTR DRIVE_PROBE,'Z'
        JBE     FILL_OPEN_DRIVES_NEXT
        ; path: "X:" and the current directory
        MOV     AH,19H
        INT     21H
        ADD     AL,'A'
        MOV     OPEN_PATH,AL
        MOV     WORD PTR OPEN_PATH+1,'\:'
        MOV     SI,OFFSET OPEN_PATH+3
        XOR     DL,DL
        MOV     AH,47H
        INT     21H
        JNC     SHORT FILL_OPEN_PATH_DONE
        MOV     BYTE PTR OPEN_PATH+3,0
FILL_OPEN_PATH_DONE:
        RET
FILL_OPEN_LISTS ENDP

; Copy the DTA's file name into entry AX of the 13-byte table DI.
STORE_LIST_NAME PROC NEAR
        MOV     CX,13
        MUL     CX
        ADD     DI,AX
        MOV     SI,OFFSET OPEN_DTA+1EH
        MOV     CX,13
        REP     MOVSB
        RET
STORE_LIST_NAME ENDP

; Sort CX 13-byte names at SI (insertion sort; the lists are short).
SORT_NAMES PROC NEAR
        CMP     CX,2
        JB      SHORT SORT_NAMES_DONE
        MOV     SORT_BASE,SI
        MOV     SORT_COUNT,CX
        MOV     WORD PTR SORT_I,1
SORT_NAMES_OUTER:
        MOV     AX,SORT_I
        CMP     AX,SORT_COUNT
        JAE     SHORT SORT_NAMES_DONE
        MOV     SORT_J,AX
SORT_NAMES_INNER:
        MOV     AX,SORT_J
        OR      AX,AX
        JZ      SHORT SORT_NAMES_ADVANCE
        MOV     CX,13
        MUL     CX
        ADD     AX,SORT_BASE
        MOV     DI,AX                   ; entry j
        MOV     SI,AX
        SUB     SI,13                   ; entry j-1
        PUSH    SI
        PUSH    DI
        MOV     CX,13
        REPE    CMPSB
        POP     DI
        POP     SI
        JBE     SHORT SORT_NAMES_ADVANCE ; j-1 <= j
        MOV     CX,13
SORT_NAMES_SWAP:
        MOV     AL,[SI]
        XCHG    AL,[DI]
        MOV     [SI],AL
        INC     SI
        INC     DI
        LOOP    SORT_NAMES_SWAP
        DEC     WORD PTR SORT_J
        JMP     SORT_NAMES_INNER
SORT_NAMES_ADVANCE:
        INC     WORD PTR SORT_I
        JMP     SORT_NAMES_OUTER
SORT_NAMES_DONE:
        RET
SORT_NAMES ENDP

; Draw a list box frame: SI = top, left, bottom, right.
DRAW_LIST_BOX PROC NEAR
        MOV     DH,[SI]
        MOV     DL,[SI+1]
        XOR     CX,CX
        MOV     CL,[SI+3]
        SUB     CL,DL
        INC     CL
        MOV     BL,70H
        MOV     AL,0C4H
        CALL    DOSUI_FILL_ROW
        MOV     AL,0DAH
        PUSH    CX
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     DL,[SI+3]
        MOV     AL,0BFH
        CALL    DOSUI_FILL_ROW
DRAW_LIST_BOX_SIDE:
        INC     DH
        CMP     DH,[SI+2]
        JAE     SHORT DRAW_LIST_BOX_BOTTOM
        MOV     AL,0B3H
        MOV     DL,[SI+1]
        CALL    DOSUI_FILL_ROW
        MOV     DL,[SI+3]
        CALL    DOSUI_FILL_ROW
        JMP     DRAW_LIST_BOX_SIDE
DRAW_LIST_BOX_BOTTOM:
        POP     CX
        MOV     DL,[SI+1]
        MOV     AL,0C4H
        CALL    DOSUI_FILL_ROW
        MOV     CX,1
        MOV     AL,0C0H
        CALL    DOSUI_FILL_ROW
        MOV     DL,[SI+3]
        MOV     AL,0D9H
        CALL    DOSUI_FILL_ROW
        RET
DRAW_LIST_BOX ENDP

; Choosing a file or a directory/drive puts it in the File Name field.
OPEN_FILE_CHOSEN PROC NEAR
        MOV     AX,OPEN_FILE_SELECTED
        MOV     SI,OFFSET OPEN_FILE_LIST
        CALL    LIST_ENTRY_ADDRESS
        MOV     SI,DI
        MOV     DI,OFFSET OPEN_NAME
        MOV     CX,13
        REP     MOVSB
        RET
OPEN_FILE_CHOSEN ENDP

OPEN_DIR_CHOSEN PROC NEAR
        MOV     AX,OPEN_DIR_SELECTED
        MOV     SI,OFFSET OPEN_DIR_LIST
        CALL    LIST_ENTRY_ADDRESS
        MOV     SI,DI
        MOV     DI,OFFSET OPEN_NAME
        CMP     BYTE PTR [SI],'['
        JNE     SHORT OPEN_DIR_CHOSEN_NAME
        MOV     AL,[SI+2]
        MOV     [DI],AL
        MOV     WORD PTR [DI+1],':'
        RET
OPEN_DIR_CHOSEN_NAME:
        MOV     CX,13
        REP     MOVSB
        RET
OPEN_DIR_CHOSEN ENDP

; Write the document, or the selection, to PRN as real DOS 5 EDIT does
; (QEMU capture with a parallel port, 2026-11-11, plan P): every line ends
; in CR LF, the last one too, and one more CR LF closes the job; there is
; no form feed. An empty document prints CR LF CR LF. A selection within
; one line prints just its text; one over several lines prints them whole.
; Carry: DOS reported an error or a short write.
PRINT_DOCUMENT PROC NEAR
        CALL    SAVE_POSITIONS          ; the whole text, the view written back
        XOR     AX,AX
        MOV     STREAM_POS,AX
        MOV     STREAM_POS+2,AX
        CALL    DOC_LENGTH
        MOV     STREAM_END,AX
        MOV     STREAM_END+2,DX
        CMP     BYTE PTR PRINT_CHOICE,0
        JNE     SHORT PRINT_RANGE_READY
        MOV     STREAM_END,0            ; Selected Text Only, none: nothing
        MOV     WORD PTR STREAM_END+2,0
        TEST    BYTE PTR G_FLAGS,1
        JZ      SHORT PRINT_RANGE_READY
        CALL    SELECTION_ENDS
        MOV     AX,SEL_A                ; over lines: they print whole
        MOV     DX,SEL_A+2
        CALL    FLAT_LINE_START
        CALL    FLAT_NEXT_LINE
        JC      SHORT PRINT_SELECTION_IN_LINE
        CMP     DX,SEL_B+2
        JNE     PRINT_SELECTION_ORDERED
        CMP     AX,SEL_B
PRINT_SELECTION_ORDERED:
        JAE     SHORT PRINT_SELECTION_IN_LINE
        CALL    SELECTION_LINES
PRINT_SELECTION_IN_LINE:
        MOV     AX,SEL_A
        MOV     STREAM_POS,AX
        MOV     AX,SEL_A+2
        MOV     STREAM_POS+2,AX
        MOV     AX,SEL_B
        MOV     STREAM_END,AX
        MOV     AX,SEL_B+2
        MOV     STREAM_END+2,AX
PRINT_RANGE_READY:
        CALL    STREAM_BEGIN
        MOV     BX,4                    ; PRN
        XOR     DL,DL                   ; DL = the last byte taken
PRINT_NEXT:
        CALL    STREAM_NEXT
        JC      SHORT PRINT_END
        CMP     AL,13
        JE      PRINT_NEXT
        MOV     DL,AL
        CMP     AL,10
        JNE     SHORT PRINT_BYTE
        CALL    PRINT_CRLF
        JC      SHORT PRINT_FAIL
        JMP     PRINT_NEXT
PRINT_BYTE:
        CALL    PRINT_ONE
        JC      SHORT PRINT_FAIL
        JMP     PRINT_NEXT
PRINT_END:
        CMP     DL,10                   ; end an unterminated (or empty) last
        JE      SHORT PRINT_CLOSE       ; line
        CALL    PRINT_CRLF
        JC      SHORT PRINT_FAIL
PRINT_CLOSE:
        CALL    PRINT_CRLF
PRINT_FAIL:
        PUSHF
        CALL    REBUILD_VIEW
        POPF
        RET
PRINT_DOCUMENT ENDP

PRINT_CRLF PROC NEAR
        MOV     AL,13
        CALL    PRINT_ONE
        JC      SHORT PRINT_CRLF_DONE
        MOV     AL,10
        CALL    PRINT_ONE
PRINT_CRLF_DONE:
        RET
PRINT_CRLF ENDP

PRINT_ONE PROC NEAR
        PUSH    CX
        PUSH    DX
        MOV     IO_BYTE,AL
        MOV     DX,OFFSET IO_BYTE
        MOV     CX,1
        MOV     AH,40H
        INT     21H
        JC      SHORT PRINT_ONE_DONE
        CMP     AX,1
        JE      SHORT PRINT_ONE_DONE
        STC
PRINT_ONE_DONE:
        POP     DX
        POP     CX
        RET
PRINT_ONE ENDP

; With Scroll Bars off (Options Display) the rails give way to the frame's
; sides and the text takes row 23 as well, with no bottom edge, as in DOS 5.
DRAW_NO_SCROLL_BARS PROC NEAR
        MOV     BL,TEXT_ATTR
        MOV     AL,0B3H
        MOV     CX,1
        MOV     DL,79
        MOV     DH,TEXT_FIRST_ROW
        CMP     BYTE PTR BARS_SHOWN,0
        JE      SHORT DRAW_NO_SCROLL_SIDE
        CMP     BYTE PTR VBAR_SHOWN,0   ; the horizontal bar alone: an edge
        JNE     SHORT DRAW_NO_SCROLL_DONE       ; beside the text
DRAW_NO_VERTICAL_SIDE:
        CALL    TEXT_FILL_ROW
        INC     DH
        CMP     DH,TEXT_LAST_ROW
        JBE     DRAW_NO_VERTICAL_SIDE
        RET
DRAW_NO_SCROLL_SIDE:
        CALL    TEXT_FILL_ROW
        INC     DH
        CMP     DH,SCROLL_ROW
        JBE     DRAW_NO_SCROLL_SIDE
        MOV     DH,SCROLL_ROW
        XOR     DL,DL
        CALL    TEXT_FILL_ROW
        INC     DL
        MOV     AL,' '
        MOV     CX,78
        CALL    TEXT_FILL_ROW
DRAW_NO_SCROLL_DONE:
        RET
DRAW_NO_SCROLL_BARS ENDP

; An entry chosen in the Save As list goes into the File Name field, as
; DOS 5 shows it: a drive as X:, a directory (or ..) as NAME\.
DRIVE_SELECTED_INTO_FIELD PROC NEAR
        MOV     AX,SAVE_AS_DRIVE_SELECTED
        MOV     SI,OFFSET SAVE_AS_DRIVE_LIST
        CALL    LIST_ENTRY_ADDRESS
        MOV     SI,DI
        MOV     DI,OFFSET FILE_NAME
        CMP     BYTE PTR [SI],'['
        JNE     SHORT DRIVE_SELECTED_DIRECTORY
        MOV     AL,[SI+2]               ; [-X-] -> X:
        MOV     [DI],AL
        MOV     WORD PTR [DI+1],':'
        RET
DRIVE_SELECTED_DIRECTORY:                ; NAME -> NAME\, as DOS 5 shows it
        LODSB
        OR      AL,AL
        JZ      SHORT DRIVE_SELECTED_SEPARATOR
        STOSB
        JMP     DRIVE_SELECTED_DIRECTORY
DRIVE_SELECTED_SEPARATOR:
        MOV     WORD PTR [DI],'\'
        RET
DRIVE_SELECTED_INTO_FIELD ENDP

; Put the file name (without its path) into FILE_NAME for Save As.
BASE_NAME_INTO_FIELD PROC NEAR
        MOV     SI,OFFSET DISPLAY_NAME
        MOV     DI,SI
BASE_NAME_SCAN:
        LODSB
        OR      AL,AL
        JZ      SHORT BASE_NAME_COPY
        CMP     AL,'\'
        JE      SHORT BASE_NAME_AFTER
        CMP     AL,':'
        JNE     BASE_NAME_SCAN
BASE_NAME_AFTER:
        MOV     DI,SI
        JMP     BASE_NAME_SCAN
BASE_NAME_COPY:
        MOV     SI,DI
        MOV     DI,OFFSET FILE_NAME
        MOV     CX,24
BASE_NAME_COPY_NEXT:
        LODSB
        STOSB
        OR      AL,AL
        JZ      SHORT BASE_NAME_DONE
        LOOP    BASE_NAME_COPY_NEXT
        MOV     BYTE PTR [DI],0
BASE_NAME_DONE:
        RET
BASE_NAME_INTO_FIELD ENDP

; ---- Horizontal view ------------------------------------------------------
; The view shows 78 columns from LEFT_COLUMN. As real DOS 5 EDIT does
; (QEMU captures, 2026-11-11, plans A, H, H2, S): Left, Shift+Left and
; Ctrl+S one column out of the view scroll it by that column; any other
; move up to 19 columns left of it puts the cursor on the view's second
; column, and up to 18 columns past its right edge scrolls just enough;
; a cursor further away lands on the view's twentieth column (or the view
; goes back to column 0), the view never starting past column 178 unless a
; longer line's cursor needs it. The same holds with Scroll Bars off.
UPDATE_LEFT_COLUMN PROC NEAR
        MOV     AX,CURRENT_COLUMN
        MOV     BX,LEFT_COLUMN
        XOR     CL,CL
        XCHG    CL,STEP_LEFT
        CMP     AX,BX
        JAE     SHORT UPDATE_LEFT_NOT_BEFORE
        OR      CL,CL
        JNZ     SHORT UPDATE_LEFT_SET   ; one step left: follow it
        SUB     BX,AX                   ; BX = columns left of the view
        CMP     BX,20
        JAE     SHORT UPDATE_LEFT_FAR
        OR      AX,AX                   ; keep one column before the cursor
        JZ      SHORT UPDATE_LEFT_SET
        DEC     AX
        JMP     SHORT UPDATE_LEFT_SET
UPDATE_LEFT_NOT_BEFORE:
        ADD     BX,77
        CMP     AX,BX
        JBE     SHORT UPDATE_LEFT_DONE
        SUB     BX,AX
        NEG     BX                      ; BX = columns past the right edge
        CMP     BX,19
        JB      SHORT UPDATE_LEFT_MINIMAL
UPDATE_LEFT_FAR:
        SUB     AX,19
        JNC     SHORT UPDATE_LEFT_SET
        XOR     AX,AX
        JMP     SHORT UPDATE_LEFT_SET
UPDATE_LEFT_MINIMAL:
        SUB     AX,77
UPDATE_LEFT_SET:
        MOV     BX,CURRENT_COLUMN       ; the view never starts past 178, or
        SUB     BX,77                   ; past a longer line's cursor on the
        JC      SHORT UPDATE_LEFT_SHORT ; last column (End on a 300-column
        CMP     BX,LEFT_COLUMN_MAX      ; line: 223, plan LL)
        JAE     SHORT UPDATE_LEFT_LIMIT
UPDATE_LEFT_SHORT:
        MOV     BX,LEFT_COLUMN_MAX
UPDATE_LEFT_LIMIT:
        CMP     AX,BX
        JBE     SHORT UPDATE_LEFT_STORE
        MOV     AX,BX
UPDATE_LEFT_STORE:
        MOV     LEFT_COLUMN,AX
UPDATE_LEFT_DONE:
        RET
UPDATE_LEFT_COLUMN ENDP

; AX = the cursor's column, past the line end included.
CURSOR_COLUMN PROC NEAR
        CALL    CALCULATE_POSITION
        MOV     AX,CURRENT_COLUMN
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT CURSOR_COLUMN_DONE
        MOV     AX,VIRTUAL_COLUMN
CURSOR_COLUMN_DONE:
        RET
CURSOR_COLUMN ENDP

; Move the cursor to column AX of its line, past the line end if need be.
SET_CURSOR_COLUMN PROC NEAR
        PUSH    AX
        CALL    RESET_NAVIGATION_COLUMNS
        CALL    CALCULATE_POSITION
        POP     DX
        PUSH    DX
        MOV     AX,CURRENT_LINE
        CALL    MOVE_TO_LINE_COLUMN
        CALL    CALCULATE_POSITION
        POP     AX
        CMP     AX,CURRENT_COLUMN
        JBE     SHORT SET_CURSOR_COLUMN_DONE
        MOV     VIRTUAL_COLUMN,AX
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,1
SET_CURSOR_COLUMN_DONE:
        RET
SET_CURSOR_COLUMN ENDP

; ---- Line indentation -------------------------------------------------
; DOS 5 EDIT: Tab on a selection that spans lines inserts eight blanks at
; the start of every selected line (a line the selection only reaches at
; column 0 is not selected). Shift+Tab on it removes, from every line, as
; many blanks as take the first line back to the previous tab stop.
; Backspace with only blanks left of the cursor, and Shift+Tab without a
; selection, outdent the line to the indentation of the nearest line above
; that is indented less (INDENT_ABOVE: empty lines do not count, a line of
; blanks only counts as zero).

; Carry: a selection spans lines. BX = first line start, CX = line count.
SELECTED_LINES PROC NEAR
        CALL    HAS_SELECTION
        JNC     SHORT SELECTED_LINES_NONE
        MOV     BX,BLOCK_MARK
        MOV     DX,CURSOR_OFFSET
        CMP     BX,DX
        JBE     SHORT SELECTED_LINES_ORDERED
        XCHG    BX,DX
SELECTED_LINES_ORDERED:
        CALL    LINE_START_OF           ; BX = first line start
        MOV     SI,BX
        XOR     CX,CX
SELECTED_LINES_COUNT:
        INC     CX
        CALL    NEXT_LINE_START
        CMP     BX,DX                   ; selection ends at or before here
        JAE     SHORT SELECTED_LINES_COUNTED
        CMP     BX,BUFFER_LENGTH
        JB      SELECTED_LINES_COUNT
SELECTED_LINES_COUNTED:
        CMP     CX,2
        JB      SHORT SELECTED_LINES_NONE
        MOV     BX,SI
        STC
        RET
SELECTED_LINES_NONE:
        CLC
        RET
SELECTED_LINES ENDP

COLLAPSE_SELECTION PROC NEAR
        CMP     BYTE PTR SHIFT_SELECTION,0
        JE      SHORT COLLAPSE_SELECTION_DONE
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
COLLAPSE_SELECTION_DONE:
        RET
COLLAPSE_SELECTION ENDP

; BX = first line start, CX = line count. Tab over a selection indents
; each line by the Tab Stops width, as in DOS 5 (captured with 4, 3, 12, 1).
INDENT_LINES PROC NEAR
        MOV     AX,CX
        XOR     DX,DX
        MOV     DL,TAB_STOPS
        MUL     DX
        JC      SHORT INDENT_LINES_FULL
        MOV     DX,AX
        CALL    VIEW_ROOM
        CMP     DX,AX
        JA      SHORT INDENT_LINES_FULL
INDENT_LINES_NEXT:
        PUSH    CX
        XOR     CX,CX
        MOV     CL,TAB_STOPS
        CALL    INSERT_BLANKS_AT
        CALL    NEXT_LINE_START
        POP     CX
        LOOP    INDENT_LINES_NEXT
        RET
INDENT_LINES_FULL:                      ; past the view's room: in the block
        JMP     GLOBAL_INDENT
INDENT_LINES ENDP

; BX = first line start, CX = line count. Shift+Tab over a selection does
; not follow Tab Stops: as observed in DOS 5, the first line goes back to
; the indentation of the nearest non-blank line above the cursor's line
; that is indented less than it (zero if none), and every line loses that
; many leading blanks, or all it has.
OUTDENT_LINES PROC NEAR
        PUSH    CX
        CALL    LEADING_BLANKS          ; AX = the first line's indentation
        POP     CX
        OR      AX,AX
        JZ      SHORT OUTDENT_LINES_DONE
        MOV     DX,AX
        PUSH    BX
        MOV     BX,CURSOR_OFFSET
        CALL    LINE_START_OF
        CALL    INDENT_ABOVE
        POP     BX
        SUB     DX,AX                   ; DX = blanks to remove per line
OUTDENT_LINES_NEXT:
        PUSH    CX
        PUSH    DX
        CALL    LEADING_BLANKS
        MOV     CX,DX
        CMP     CX,AX
        JBE     SHORT OUTDENT_LINES_COUNT
        MOV     CX,AX
OUTDENT_LINES_COUNT:
        CALL    DELETE_SPAN_AT
        CALL    NEXT_LINE_START
        POP     DX
        POP     CX
        LOOP    OUTDENT_LINES_NEXT
OUTDENT_LINES_DONE:
        RET
OUTDENT_LINES ENDP

; Backspace in the indentation. Carry set when it outdented the line.
OUTDENT_AT_CURSOR PROC NEAR
        CALL    CALCULATE_POSITION
        MOV     DX,CURRENT_COLUMN
        OR      DX,DX
        JZ      SHORT OUTDENT_AT_CURSOR_NO
        MOV     BX,CURSOR_OFFSET
        SUB     BX,DX                   ; line start
        CALL    LEADING_BLANKS
        CMP     AX,DX
        JB      SHORT OUTDENT_AT_CURSOR_NO   ; text left of the cursor
        CALL    INDENT_ABOVE            ; AX = target below column DX
        MOV     CX,DX
        SUB     CX,AX
        ADD     BX,AX
        CALL    DELETE_SPAN_AT
        STC
        RET
OUTDENT_AT_CURSOR_NO:
        CLC
        RET
OUTDENT_AT_CURSOR ENDP

; Shift+Tab without a selection: outdent the cursor's line, the cursor
; keeping its screen column.
OUTDENT_CURSOR_LINE PROC NEAR
        CALL    CALCULATE_POSITION
        MOV     AX,CURRENT_COLUMN
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT OUTDENT_CURSOR_HAVE_COLUMN
        MOV     AX,VIRTUAL_COLUMN
OUTDENT_CURSOR_HAVE_COLUMN:
        PUSH    AX
        PUSH    WORD PTR CURRENT_LINE
        MOV     BX,CURSOR_OFFSET
        SUB     BX,CURRENT_COLUMN
        CALL    LEADING_BLANKS
        MOV     DX,AX
        OR      DX,DX
        JZ      SHORT OUTDENT_CURSOR_RESTORE
        CALL    INDENT_ABOVE
        MOV     CX,DX
        SUB     CX,AX
        ADD     BX,AX
        CALL    DELETE_SPAN_AT
OUTDENT_CURSOR_RESTORE:
        POP     AX                      ; line
        POP     DX                      ; column
        PUSH    DX
        CALL    MOVE_TO_LINE_COLUMN
        CALL    CALCULATE_POSITION
        POP     AX
        CMP     AX,CURRENT_COLUMN
        JBE     SHORT OUTDENT_CURSOR_DONE
        MOV     VIRTUAL_COLUMN,AX
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,1
OUTDENT_CURSOR_DONE:
        RET
OUTDENT_CURSOR_LINE ENDP

; BX = a line start, DX = a column. AX = the indentation of the nearest
; line above whose indentation is below DX, or zero. An empty line does not
; count; a line of blanks only counts as indentation zero (DOS 5, captured
; 2026-11-11, plans BS2 and BS3: Backspace and Shift+Tab under "  A",
; "        ", "        B" outdent B to column 1, under "    C", "",
; "        D" to column 5).
INDENT_ABOVE PROC NEAR
        PUSH    BX
        PUSH    CX
INDENT_ABOVE_PREVIOUS:
        OR      BX,BX
        JZ      SHORT INDENT_ABOVE_BEFORE
        DEC     BX                      ; into the previous line's end
        CALL    LINE_START_OF
        PUSH    BX
        CALL    LEADING_BLANKS          ; AX = indent, CX = line length
        POP     BX
        JCXZ    INDENT_ABOVE_PREVIOUS   ; an empty line
        CMP     AX,CX
        JAE     SHORT INDENT_ABOVE_NONE ; blanks only: indentation zero
        CMP     AX,DX
        JAE     INDENT_ABOVE_PREVIOUS
        JMP     SHORT INDENT_ABOVE_DONE
INDENT_ABOVE_BEFORE:                    ; on in the text before the view
        PUSH    DX
        MOV     BX,DX
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        CALL    FLAT_INDENT_ABOVE
        POP     DX
        JMP     SHORT INDENT_ABOVE_DONE
INDENT_ABOVE_NONE:
        XOR     AX,AX
INDENT_ABOVE_DONE:
        POP     CX
        POP     BX
        RET
INDENT_ABOVE ENDP

; BX = any offset; BX returns the start of its line.
LINE_START_OF PROC NEAR
LINE_START_OF_NEXT:
        OR      BX,BX
        JZ      SHORT LINE_START_OF_DONE
        CMP     BYTE PTR BUFFER[BX-1],10
        JE      SHORT LINE_START_OF_DONE
        DEC     BX
        JMP     LINE_START_OF_NEXT
LINE_START_OF_DONE:
        RET
LINE_START_OF ENDP

; BX = an offset in a line; BX returns the next line's start (or the end).
NEXT_LINE_START PROC NEAR
NEXT_LINE_START_NEXT:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT NEXT_LINE_START_DONE
        INC     BX
        CMP     BYTE PTR BUFFER[BX-1],10
        JNE     NEXT_LINE_START_NEXT
NEXT_LINE_START_DONE:
        RET
NEXT_LINE_START ENDP

; BX = a line start. AX = its leading blanks, CX = its length (no CR/LF).
LEADING_BLANKS PROC NEAR
        PUSH    SI
        MOV     SI,BX
        XOR     AX,AX
        XOR     CX,CX
LEADING_BLANKS_NEXT:
        CMP     SI,BUFFER_LENGTH
        JAE     SHORT LEADING_BLANKS_DONE
        CMP     BYTE PTR BUFFER[SI],13
        JE      SHORT LEADING_BLANKS_DONE
        CMP     BYTE PTR BUFFER[SI],10
        JE      SHORT LEADING_BLANKS_DONE
        CMP     BYTE PTR BUFFER[SI],' '
        JNE     SHORT LEADING_BLANKS_TEXT
        CMP     AX,CX
        JNE     SHORT LEADING_BLANKS_TEXT
        INC     AX
LEADING_BLANKS_TEXT:
        INC     CX
        INC     SI
        JMP     LEADING_BLANKS_NEXT
LEADING_BLANKS_DONE:
        POP     SI
        RET
LEADING_BLANKS ENDP

; Insert CX blanks at offset BX (the caller checked the room). Offsets past
; BX - the cursor and the selection mark - move with the text.
INSERT_BLANKS_AT PROC NEAR
        JCXZ    INSERT_BLANKS_DONE
        PUSH    AX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        PUSH    CX
        MOV     SI,BUFFER_BASE
        ADD     SI,BUFFER_LENGTH
        DEC     SI
        MOV     DI,SI
        ADD     DI,CX
        MOV     CX,BUFFER_LENGTH
        SUB     CX,BX
        VIEW_SEGS
        STD
        REP     MOVSB
        CLD
        CODE_SEGS
        POP     CX
        MOV     DI,BUFFER_BASE
        ADD     DI,BX
        MOV     AL,' '
        PUSH    CX
        VIEW_SEGS
        REP     STOSB
        CODE_SEGS
        POP     CX
        ADD     BUFFER_LENGTH,CX
        CMP     CURSOR_OFFSET,BX
        JBE     SHORT INSERT_BLANKS_MARK
        ADD     CURSOR_OFFSET,CX
INSERT_BLANKS_MARK:
        CMP     WORD PTR BLOCK_MARK,0FFFFH
        JE      SHORT INSERT_BLANKS_MODIFIED
        CMP     BLOCK_MARK,BX
        JBE     SHORT INSERT_BLANKS_MODIFIED
        ADD     BLOCK_MARK,CX
INSERT_BLANKS_MODIFIED:
        MOV     BYTE PTR MODIFIED,1
        POP     DI
        POP     SI
        POP     CX
        POP     AX
INSERT_BLANKS_DONE:
        RET
INSERT_BLANKS_AT ENDP

; Delete CX bytes at offset BX. Offsets past BX move back, never below BX.
DELETE_SPAN_AT PROC NEAR
        JCXZ    DELETE_SPAN_DONE
        PUSH    AX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        MOV     DI,BUFFER_BASE
        ADD     DI,BX
        MOV     SI,DI
        ADD     SI,CX
        PUSH    CX
        MOV     CX,BUFFER_LENGTH
        SUB     CX,BX
        POP     AX
        SUB     CX,AX
        VIEW_SEGS
        REP     MOVSB
        CODE_SEGS
        MOV     CX,AX
        SUB     BUFFER_LENGTH,CX
        MOV     AX,CURSOR_OFFSET
        CALL    DELETE_SPAN_ADJUST
        MOV     CURSOR_OFFSET,AX
        CMP     WORD PTR BLOCK_MARK,0FFFFH
        JE      SHORT DELETE_SPAN_MODIFIED
        MOV     AX,BLOCK_MARK
        CALL    DELETE_SPAN_ADJUST
        MOV     BLOCK_MARK,AX
DELETE_SPAN_MODIFIED:
        MOV     BYTE PTR MODIFIED,1
        POP     DI
        POP     SI
        POP     CX
        POP     AX
DELETE_SPAN_DONE:
        RET
DELETE_SPAN_AT ENDP

; AX = an offset after deleting CX bytes at BX.
DELETE_SPAN_ADJUST PROC NEAR
        CMP     AX,BX
        JBE     SHORT DELETE_SPAN_ADJUST_DONE
        SUB     AX,CX
        JC      SHORT DELETE_SPAN_ADJUST_CLAMP
        CMP     AX,BX
        JAE     SHORT DELETE_SPAN_ADJUST_DONE
DELETE_SPAN_ADJUST_CLAMP:
        MOV     AX,BX
DELETE_SPAN_ADJUST_DONE:
        RET
DELETE_SPAN_ADJUST ENDP

BEEP PROC NEAR
        PUSH    AX
        PUSH    BX
        MOV     AX,0E07H
        XOR     BX,BX
        INT     10H
        POP     BX
        POP     AX
        RET
BEEP ENDP

; Carry is set when a non-empty Shift (or Find) selection exists. A Ctrl+B
; mark is left alone: it stays active while the cursor moves to a target.
HAS_SELECTION PROC NEAR
        PUSH    AX
        CMP     BYTE PTR SHIFT_SELECTION,0
        JE      SHORT HAS_SELECTION_NONE
        MOV     AX,BLOCK_MARK
        CMP     AX,0FFFFH
        JE      SHORT HAS_SELECTION_NONE
        CMP     AX,CURSOR_OFFSET
        JE      SHORT HAS_SELECTION_NONE
        POP     AX
        STC
        RET
HAS_SELECTION_NONE:
        POP     AX
        CLC
        RET
HAS_SELECTION ENDP

; DOS 5 EDIT lets the cursor rest past the end of a line. Text entered there
; lands at the cursor, so the gap is first filled with blanks.
MATERIALIZE_VIRTUAL_COLUMN PROC NEAR
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT MATERIALIZE_DONE
        PUSH    AX
        PUSH    CX
        PUSH    SI
        CALL    CALCULATE_POSITION
        MOV     CX,VIRTUAL_COLUMN
        SUB     CX,CURRENT_COLUMN
        JBE     SHORT MATERIALIZE_CLEAR
        MOV     AL,' '
MATERIALIZE_NEXT:
        CALL    INSERT_BYTE
        LOOP    MATERIALIZE_NEXT
MATERIALIZE_CLEAR:
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,0
        POP     SI
        POP     CX
        POP     AX
MATERIALIZE_DONE:
        RET
MATERIALIZE_VIRTUAL_COLUMN ENDP

; AL = a character: AL = 0 for a blank (space or tab), 1 for a word
; character (IS_WORD_BYTE), 2 for anything else.
DELETE_WORD_CLASS PROC NEAR
        CMP     AL,' '
        JE      SHORT DELETE_WORD_CLASS_BLANK
        CMP     AL,9
        JE      SHORT DELETE_WORD_CLASS_BLANK
        CALL    IS_WORD_BYTE
        MOV     AL,1
        JC      SHORT DELETE_WORD_CLASS_DONE
        INC     AX
DELETE_WORD_CLASS_DONE:
        RET
DELETE_WORD_CLASS_BLANK:
        XOR     AL,AL
        RET
DELETE_WORD_CLASS ENDP

; DOS 5 EDIT's words are runs of letters, digits, periods, the QBasic
; type characters ! # $ % & and the characters from 80h (its QBasic
; heritage: "beta.gamma" and "a$b" are one word, "x_y" and "a-b" are two;
; QEMU captures, 2026-11-11, plans T5, T6). Ctrl+Right skips the
; rest of the current word and the gap after it, crossing line ends, and
; stays at the end of the last word when no word follows; Ctrl+Left goes
; back over a gap to the start of the previous word.
IS_WORD_BYTE PROC NEAR
        CMP     AL,80H
        JAE     SHORT IS_WORD_YES
        PUSH    DI
        PUSH    CX
        PUSH    ES
        PUSH    DS
        POP     ES
        MOV     DI,OFFSET WORD_MARKS
        MOV     CX,6
        REPNE   SCASB
        POP     ES
        POP     CX
        POP     DI
        JE      SHORT IS_WORD_YES
        CMP     AL,'0'
        JB      SHORT IS_WORD_NO
        CMP     AL,'9'
        JBE     SHORT IS_WORD_YES
        CMP     AL,'A'
        JB      SHORT IS_WORD_NO
        CMP     AL,'Z'
        JBE     SHORT IS_WORD_YES
        CMP     AL,'a'
        JB      SHORT IS_WORD_NO
        CMP     AL,'z'
        JBE     SHORT IS_WORD_YES
IS_WORD_NO:
        CLC
        RET
IS_WORD_YES:
        STC
        RET
IS_WORD_BYTE ENDP

MOVE_WORD_RIGHT PROC NEAR
        MOV     BX,CURSOR_OFFSET
MOVE_WORD_RIGHT_IN_WORD:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT MOVE_WORD_RIGHT_DONE
        MOV     AL,BUFFER[BX]
        CALL    IS_WORD_BYTE
        JNC     SHORT MOVE_WORD_RIGHT_GAP_START
        INC     BX
        JMP     MOVE_WORD_RIGHT_IN_WORD
MOVE_WORD_RIGHT_GAP_START:
        MOV     DX,BX                   ; end of this word
MOVE_WORD_RIGHT_IN_GAP:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT MOVE_WORD_RIGHT_NONE
        MOV     AL,BUFFER[BX]
        CALL    IS_WORD_BYTE
        JC      SHORT MOVE_WORD_RIGHT_DONE
        INC     BX
        JMP     MOVE_WORD_RIGHT_IN_GAP
MOVE_WORD_RIGHT_NONE:
        MOV     BX,DX
MOVE_WORD_RIGHT_DONE:
        MOV     CURSOR_OFFSET,BX
        RET
MOVE_WORD_RIGHT ENDP

MOVE_WORD_END PROC NEAR
        MOV     BX,CURSOR_OFFSET
MOVE_WORD_END_IN_GAP:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT MOVE_WORD_END_DONE
        MOV     AL,BUFFER[BX]
        CALL    IS_WORD_BYTE
        JC      SHORT MOVE_WORD_END_IN_WORD
        INC     BX
        JMP     MOVE_WORD_END_IN_GAP
MOVE_WORD_END_IN_WORD:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT MOVE_WORD_END_DONE
        MOV     AL,BUFFER[BX]
        CALL    IS_WORD_BYTE
        JNC     SHORT MOVE_WORD_END_DONE
        INC     BX
        JMP     MOVE_WORD_END_IN_WORD
MOVE_WORD_END_DONE:
        MOV     CURSOR_OFFSET,BX
        RET
MOVE_WORD_END ENDP

MOVE_WORD_LEFT PROC NEAR
        MOV     BX,CURSOR_OFFSET
MOVE_WORD_LEFT_IN_GAP:
        OR      BX,BX
        JZ      SHORT MOVE_WORD_LEFT_DONE
        MOV     AL,BUFFER[BX-1]
        CALL    IS_WORD_BYTE
        JC      SHORT MOVE_WORD_LEFT_IN_WORD
        DEC     BX
        JMP     MOVE_WORD_LEFT_IN_GAP
MOVE_WORD_LEFT_IN_WORD:
        OR      BX,BX
        JZ      SHORT MOVE_WORD_LEFT_DONE
        MOV     AL,BUFFER[BX-1]
        CALL    IS_WORD_BYTE
        JNC     SHORT MOVE_WORD_LEFT_DONE
        DEC     BX
        JMP     MOVE_WORD_LEFT_IN_WORD
MOVE_WORD_LEFT_DONE:
        MOV     CURSOR_OFFSET,BX
        RET
MOVE_WORD_LEFT ENDP

RESET_NAVIGATION_COLUMNS PROC NEAR
        MOV     BYTE PTR PREFERRED_COLUMN_VALID,0
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,0
        RET
RESET_NAVIGATION_COLUMNS ENDP

; The cursor types are DOS 5 EDIT's own legacy 8-line values: 0607h for
; insert and 0007h for overwrite, as its BIOS data area shows. They go through
; INT 10h AH=01h with the EGA/VGA cursor emulation left on, so the BIOS scales
; them to the cell height: an underline at the bottom and a full block.
DRAW_SCREEN PROC NEAR
        CALL    SET_EDITOR_WINDOW
        PUSH    AX                      ; the editor's own rows (below the
        MOV     AL,EDITOR_ROW_SHIFT     ; help window): a dialog drawn after
        CALL    SET_ROW_OFFSET          ; this sets its own
        POP     AX
        MOV     CX,0607H
        CMP     BYTE PTR INSERT_MODE,0
        JNE     SHORT DRAW_CURSOR_SHAPE_READY
        MOV     CX,0007H
DRAW_CURSOR_SHAPE_READY:
        CALL    DOSUI_SET_CURSOR_SHAPE
        CALL    CALCULATE_POSITION
        CMP     BYTE PTR VIRTUAL_COLUMN_VALID,0
        JE      SHORT DRAW_HAVE_COLUMN
        MOV     AX,VIRTUAL_COLUMN
        MOV     CURRENT_COLUMN,AX
DRAW_HAVE_COLUMN:
        CALL    UPDATE_LEFT_COLUMN
        CALL    UPDATE_SCROLL_THUMB
        ; A cursor up to four lines outside the view scrolls it just enough;
        ; one further away (Ctrl+End, Ctrl+Q C, Find, a bookmark...) puts
        ; its line on the view's sixth row, as real DOS 5 EDIT does in
        ; either direction from any start (QEMU captures, 2026-11-11).
        MOV     AX,CURRENT_LINE
        MOV     BX,TOP_LINE
        CMP     AX,BX
        JAE     SHORT DRAW_CHECK_BOTTOM
        SUB     BX,AX                   ; lines above the view
        CMP     BX,FAR_JUMP_LINES
        JB      SHORT DRAW_SET_TOP
        JMP     SHORT DRAW_FAR_JUMP
DRAW_CHECK_BOTTOM:
        ADD     BX,TEXT_ROW_COUNT
        CMP     AX,BX
        JB      SHORT DRAW_VISIBLE
        SUB     AX,BX                   ; lines below the view, less one
        INC     AX
        CMP     AX,FAR_JUMP_LINES
        MOV     AX,CURRENT_LINE
        JAE     SHORT DRAW_FAR_JUMP
        SUB     AX,TEXT_ROW_COUNT
        INC     AX
        JMP     SHORT DRAW_SET_TOP
DRAW_FAR_JUMP:
        MOV     AX,CURRENT_LINE
        SUB     AX,FAR_JUMP_LINES
        JNC     SHORT DRAW_SET_TOP
        XOR     AX,AX
DRAW_SET_TOP:
        MOV     TOP_LINE,AX
DRAW_VISIBLE:
        CALL    DOSUI_BEGIN_FRAME
        MOV     AL,TEXT_ATTR
        MOV     BYTE PTR DOSUI_ATTR_RAW,1
        CALL    DOSUI_CLEAR
        MOV     BYTE PTR DOSUI_ATTR_RAW,0

        MOV     AL,' '
        MOV     BL,70H
        MOV     DH,0
        XOR     DL,DL
        MOV     CX,80
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET MENU_TEXT
        MOV     DH,0
        XOR     DL,DL
        MOV     BL,70H
        CALL    DOSUI_WRITE_Z

        CALL    DRAW_EDITOR_CHROME

        XOR     SI,SI
        MOV     BX,TOP_LINE
        OR      BX,BX
        JZ      SHORT DRAW_TEXT_START
        PUSH    CX
        PUSH    DI
        PUSH    ES
        PUSH    SS
        POP     ES
        MOV     DI,BUFFER_BASE
        MOV     CX,BUFFER_LENGTH
        CLD
DRAW_FIND_TOP:
        JCXZ    DRAW_FIND_TOP_END
        MOV     AL,10
        REPNE   SCASB
        JNE     SHORT DRAW_FIND_TOP_END
        DEC     BX
        JNZ     DRAW_FIND_TOP
        MOV     SI,DI
        SUB     SI,BUFFER_BASE
        JMP     SHORT DRAW_FIND_TOP_POP
DRAW_FIND_TOP_END:
        MOV     SI,BUFFER_LENGTH
DRAW_FIND_TOP_POP:
        POP     ES
        POP     DI
        POP     CX

DRAW_TEXT_START:
        MOV     BYTE PTR DRAW_HAS_HIGHLIGHT,0
        CMP     WORD PTR BLOCK_MARK,0FFFFH
        JE      SHORT DRAW_PREP_SEARCH_HL
        MOV     AX,BLOCK_MARK
        MOV     BX,CURSOR_OFFSET
        CMP     AX,BX
        JE      SHORT DRAW_PREP_DONE
        JBE     SHORT DRAW_PREP_STORE_HL
        XCHG    AX,BX
        JMP     SHORT DRAW_PREP_STORE_HL
DRAW_PREP_SEARCH_HL:
        MOV     AX,SEARCH_HIGHLIGHT_START
        CMP     AX,0FFFFH
        JE      SHORT DRAW_PREP_DONE
        MOV     BX,SEARCH_HIGHLIGHT_END
        CMP     AX,BX
        JAE     SHORT DRAW_PREP_DONE
DRAW_PREP_STORE_HL:
        MOV     DRAW_SEL_START,AX
        MOV     DRAW_SEL_END,BX
        MOV     BYTE PTR DRAW_HAS_HIGHLIGHT,1
DRAW_PREP_DONE:
        PUSH    ES
        MOV     ES,CS:DOSUI_DRAW_SEGMENT
        MOV     DH,TEXT_FIRST_ROW
        XOR     AX,AX
        MOV     AL,DH
        ADD     AL,CS:DOSUI_ROW_OFFSET
        CMP     AL,CS:DOSUI_ROWS
        JB      SHORT DRAW_FIRST_ROW_OK
        JMP     DRAW_TEXT_POP_ES
DRAW_FIRST_ROW_OK:
        MOV     BL,160
        MUL     BL
        ADD     AX,2
        MOV     DRAW_ROW_DI,AX
        MOV     DI,AX
        MOV     DL,1
        MOV     WORD PTR DRAW_COLUMN,0
DRAW_TEXT_NEXT:
        CMP     DH,TEXT_BOTTOM_ROW
        JBE     SHORT DRAW_TEXT_ROW_OK
        JMP     DRAW_TEXT_POP_ES
DRAW_TEXT_ROW_OK:
        CMP     SI,BUFFER_LENGTH
        JB      SHORT DRAW_TEXT_HAVE_BYTE
        JMP     DRAW_TEXT_POP_ES
DRAW_TEXT_HAVE_BYTE:
        MOV     AL,BUFFER[SI]
        INC     SI
        CMP     AL,13
        JE      DRAW_TEXT_NEXT
        CMP     AL,10
        JE      SHORT DRAW_NEWLINE
        OR      AL,AL
        JZ      SHORT DRAW_SKIP_NUL_TAIL ; DOS 5 blanks the malformed line tail.
        MOV     CX,DRAW_COLUMN          ; columns left of the view are skipped
        INC     WORD PTR DRAW_COLUMN
        CMP     CX,LEFT_COLUMN
        JB      DRAW_TEXT_NEXT
        CMP     DL,79
        JAE     DRAW_TEXT_NEXT
        MOV     AH,TEXT_ATTR
        CMP     BYTE PTR DRAW_HAS_HIGHLIGHT,0
        JE      SHORT DRAW_TEXT_STORE
        MOV     BX,SI
        DEC     BX
        CMP     BX,DRAW_SEL_START
        JB      SHORT DRAW_TEXT_STORE
        CMP     BX,DRAW_SEL_END
        JAE     SHORT DRAW_TEXT_STORE
        MOV     AH,SELECT_ATTR
DRAW_TEXT_STORE:
        MOV     ES:[DI],AX
        ADD     DI,2
        INC     DL
        JMP     DRAW_TEXT_NEXT
DRAW_SKIP_NUL_TAIL:
        CMP     SI,BUFFER_LENGTH
        JAE     SHORT DRAW_TEXT_POP_ES
        MOV     AL,BUFFER[SI]
        INC     SI
        CMP     AL,10
        JE      SHORT DRAW_NEWLINE
        JMP     DRAW_SKIP_NUL_TAIL
DRAW_NEWLINE:
        INC     DH
        MOV     AL,DH
        ADD     AL,CS:DOSUI_ROW_OFFSET
        CMP     AL,CS:DOSUI_ROWS
        JAE     SHORT DRAW_TEXT_POP_ES
        ADD     WORD PTR DRAW_ROW_DI,160
        MOV     DI,DRAW_ROW_DI
        MOV     DL,1
        MOV     WORD PTR DRAW_COLUMN,0
        JMP     DRAW_TEXT_NEXT
DRAW_TEXT_POP_ES:
        POP     ES
DRAW_CURSOR:
        CMP     BYTE PTR HELP_OPEN,0    ; Help's window over the rows above
        JE      SHORT DRAW_EDITOR_CURSOR
        CALL    HELP_DRAW_WINDOW
        CMP     BYTE PTR HELP_ACTIVE,0
        JE      SHORT DRAW_EDITOR_CURSOR_SHIFTED
        CALL    HELP_PLACE_CURSOR
        JMP     SHORT DRAW_RECORD_STATE
DRAW_EDITOR_CURSOR_SHIFTED:
        MOV     AL,EDITOR_ROW_SHIFT
        CALL    SET_ROW_OFFSET
DRAW_EDITOR_CURSOR:
        MOV     AX,CURRENT_LINE
        SUB     AX,TOP_LINE
        ADD     AL,TEXT_FIRST_ROW
        MOV     DH,AL
        MOV     AX,CURRENT_COLUMN
        SUB     AX,LEFT_COLUMN
        INC     AX
        MOV     DL,AL
        CALL    DOSUI_SET_CURSOR
        CMP     BYTE PTR STARTUP_ERROR,0
        JE      SHORT DRAW_RECORD_STATE
        CALL    SHOW_STARTUP_ERROR
        MOV     BYTE PTR DRAWN_VALID,0
        JMP     SHORT DRAW_DONE
DRAW_RECORD_STATE:
        MOV     AX,TOP_LINE
        MOV     DRAWN_TOP_LINE,AX
        MOV     AX,LEFT_COLUMN
        MOV     DRAWN_LEFT_COLUMN,AX
        MOV     AL,SCROLL_THUMB_ROW
        MOV     DRAWN_THUMB_ROW,AL
        MOV     AL,DRAW_HAS_HIGHLIGHT
        MOV     DRAWN_HAD_HIGHLIGHT,AL
        MOV     BYTE PTR DRAWN_VALID,1
DRAW_DONE:
        RET
DRAW_SCREEN ENDP

; DOS 5 EDIT's vertical thumb tracks the current line across the 19 cells
; between the up/down arrows.  Its denominator is the logical line count,
; including a non-empty final unterminated line, rather than always using the
; last zero-based line index.  Keep the calculation integer and deterministic.
UPDATE_SCROLL_THUMB PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        MOV     BYTE PTR SCROLL_THUMB_ROW,TEXT_FIRST_ROW+1
        CALL    DOC_LENGTH
        OR      AX,DX
        JZ      SHORT UPDATE_SCROLL_THUMB_DONE
        CALL    TOTAL_LINE_ENDS
        MOV     SI,AX
        MOV     DI,DX                   ; DI:SI = the lines of the text
        CALL    DOC_LAST_BYTE
        CMP     BL,10
        JE      SHORT UPDATE_SCROLL_THUMB_HAVE_LINES
        ADD     SI,1
        ADC     DI,0
UPDATE_SCROLL_THUMB_HAVE_LINES:
        MOV     AX,CURRENT_LINE         ; the cursor's line in the text
        XOR     DX,DX
        ADD     AX,VIEW_LINE
        ADC     DX,VIEW_LINE+2
UPDATE_SCROLL_THUMB_SCALE:              ; both under 64K, in proportion
        OR      DI,DI
        JZ      SHORT UPDATE_SCROLL_THUMB_SCALED
        SHR     DI,1
        RCR     SI,1
        SHR     DX,1
        RCR     AX,1
        JMP     UPDATE_SCROLL_THUMB_SCALE
UPDATE_SCROLL_THUMB_SCALED:
        MOV     CX,TEXT_ROWS
        SUB     CX,3
        MUL     CX
        DIV     SI
        ADD     AL,TEXT_FIRST_ROW+1
        MOV     SCROLL_THUMB_ROW,AL
UPDATE_SCROLL_THUMB_DONE:
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        RET
UPDATE_SCROLL_THUMB ENDP

; Draw the persistent DOS 5 EDIT viewport chrome. The program retains CK-DOS
; branding, but geometry, colors, rails, and position fields follow the oracle.
DRAW_EDITOR_CHROME PROC NEAR
        MOV     AL,0C4H
        MOV     BL,TEXT_ATTR
        MOV     DH,1
        XOR     DL,DL
        MOV     CX,80
        CALL    TEXT_FILL_ROW
        MOV     AX,0BFDAH               ; the corners, the rule's ends
        CMP     BYTE PTR HELP_OPEN,0    ; under Help
        JE      SHORT DRAW_CHROME_CORNERS
        MOV     AX,0B4C3H
DRAW_CHROME_CORNERS:
        MOV     CX,1
        CALL    TEXT_FILL_ROW
        MOV     AL,AH
        MOV     DL,79
        CALL    TEXT_FILL_ROW
        CALL    DRAW_EDITOR_TITLE

        MOV     DH,TEXT_FIRST_ROW
DRAW_CHROME_VERTICAL:
        MOV     AL,0B3H
        MOV     BL,TEXT_ATTR
        XOR     DL,DL
        MOV     CX,1
        CALL    TEXT_FILL_ROW
        MOV     AL,0B0H
        MOV     BL,70H
        MOV     DL,79
        CMP     DH,TEXT_FIRST_ROW
        JNE     SHORT DRAW_CHROME_NOT_UP
        MOV     AL,18H
        JMP     SHORT DRAW_CHROME_VERTICAL_CELL
DRAW_CHROME_NOT_UP:
        CMP     DH,SCROLL_THUMB_ROW
        JNE     SHORT DRAW_CHROME_NOT_THUMB
        MOV     AL,' '
        MOV     BL,07H
        JMP     SHORT DRAW_CHROME_VERTICAL_CELL
DRAW_CHROME_NOT_THUMB:
        CMP     DH,TEXT_LAST_ROW
        JNE     SHORT DRAW_CHROME_VERTICAL_CELL
        MOV     AL,19H
DRAW_CHROME_VERTICAL_CELL:
        CALL    DOSUI_FILL_ROW
        INC     DH
        CMP     DH,TEXT_LAST_ROW
        JBE     DRAW_CHROME_VERTICAL

        MOV     DH,SCROLL_ROW
        MOV     AL,0B3H
        MOV     BL,TEXT_ATTR
        XOR     DL,DL
        MOV     CX,1
        CALL    TEXT_FILL_ROW
        MOV     AL,1BH
        MOV     BL,70H
        MOV     DL,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B0H
        MOV     DL,2
        MOV     CX,76
        CALL    DOSUI_FILL_ROW
        MOV     AX,LEFT_COLUMN          ; DOS 5: thumb = 2 + left * 75 / 178
        CMP     AX,LEFT_COLUMN_MAX
        JBE     SHORT DRAW_CHROME_HAVE_LEFT
        MOV     AX,LEFT_COLUMN_MAX
DRAW_CHROME_HAVE_LEFT:
        PUSH    DX
        MOV     CX,75
        MUL     CX
        MOV     CX,LEFT_COLUMN_MAX
        DIV     CX
        POP     DX
        ADD     AL,2
        MOV     DL,AL
        MOV     AL,' '
        MOV     BL,07H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,1AH
        MOV     BL,70H
        MOV     DL,78
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B3H
        MOV     BL,TEXT_ATTR
        MOV     DL,79
        CALL    TEXT_FILL_ROW
        CALL    DRAW_NO_SCROLL_BARS
        CMP     BYTE PTR HELP_OPEN,0    ; under Help: HELP_DRAW_WINDOW's
        JNE     SHORT DRAW_CHROME_DONE
        CALL    DRAW_EDITOR_STATUS
DRAW_CHROME_DONE:
        RET
DRAW_EDITOR_CHROME ENDP

; The editor's status line, on the screen's last row.
DRAW_EDITOR_STATUS PROC NEAR
        MOV     AL,' '
        MOV     BL,3FH
        MOV     DH,STATUS_ROW
        XOR     DL,DL
        MOV     CX,62
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET STATUS_LEFT_TEXT
        CALL    DOSUI_WRITE_Z
        MOV     AL,' '
        MOV     BL,30H
        MOV     DL,62
        MOV     CX,18
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B3H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        CALL    FORMAT_STATUS_POSITION
        MOV     SI,OFFSET STATUS_POSITION_TEXT
        MOV     DH,STATUS_ROW
        MOV     DL,70
        MOV     BL,30H
        CALL    DOSUI_WRITE_Z
        RET
DRAW_EDITOR_STATUS ENDP

DRAW_EDITOR_TITLE PROC NEAR
        CMP     BYTE PTR DISPLAY_NAME,0
        JNE     SHORT DRAW_TITLE_NAMED
        MOV     SI,OFFSET UNTITLED_TEXT
        JMP     SHORT DRAW_TITLE_HAVE_POINTER
DRAW_TITLE_NAMED:
        MOV     SI,OFFSET DISPLAY_NAME
        MOV     DI,SI
DRAW_TITLE_SCAN_PATH:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      SHORT DRAW_TITLE_PATH_DONE
        CMP     AL,'\'
        JE      SHORT DRAW_TITLE_NEW_BASE
        CMP     AL,':'
        JNE     SHORT DRAW_TITLE_SCAN_NEXT
DRAW_TITLE_NEW_BASE:
        MOV     DI,SI
        INC     DI
DRAW_TITLE_SCAN_NEXT:
        INC     SI
        JMP     DRAW_TITLE_SCAN_PATH
DRAW_TITLE_PATH_DONE:
        MOV     SI,DI
DRAW_TITLE_HAVE_POINTER:
        PUSH    SI
        XOR     CX,CX
DRAW_TITLE_LENGTH:
        CMP     BYTE PTR [SI],0
        JE      SHORT DRAW_TITLE_LENGTH_DONE
        INC     SI
        INC     CX
        JMP     DRAW_TITLE_LENGTH
DRAW_TITLE_LENGTH_DONE:
        POP     SI
        MOV     AX,79
        SUB     AX,CX
        SUB     AX,2
        SHR     AX,1
        MOV     DL,AL
        MOV     DH,1
        MOV     AL,' '
        MOV     BL,SELECT_ATTR          ; lit while the editor is active
        CMP     BYTE PTR HELP_ACTIVE,0
        JE      SHORT DRAW_TITLE_ATTRIBUTE
        MOV     BL,TEXT_ATTR
DRAW_TITLE_ATTRIBUTE:
        ADD     CX,2
        MOV     BYTE PTR DOSUI_ATTR_RAW,1
        CALL    DOSUI_FILL_ROW
        INC     DL
        CALL    DOSUI_WRITE_Z
        MOV     BYTE PTR DOSUI_ATTR_RAW,0
        RET
DRAW_EDITOR_TITLE ENDP

FORMAT_STATUS_POSITION PROC NEAR
        MOV     AX,CURRENT_LINE         ; the line in the whole text
        XOR     DX,DX
        ADD     AX,VIEW_LINE
        ADC     DX,VIEW_LINE+2
        ADD     AX,1
        ADC     DX,0
        MOV     DI,OFFSET STATUS_POSITION_TEXT+4
        PUSH    BX
        MOV     BX,5
FORMAT_STATUS_LINE_NEXT:
        MOV     CX,10
        CALL    DIV32_16
        ADD     CL,'0'
        MOV     [DI],CL
        DEC     DI
        DEC     BX
        JNZ     FORMAT_STATUS_LINE_NEXT
        POP     BX
        MOV     AX,CURRENT_COLUMN
        INC     AX
        MOV     DI,OFFSET STATUS_POSITION_TEXT+8
        MOV     CX,3
        CALL    FORMAT_DECIMAL_FIELD
        RET
FORMAT_STATUS_POSITION ENDP

; AX=value, CX=field width, DS:DI=last digit. Fields are zero padded.
FORMAT_DECIMAL_FIELD PROC NEAR
        PUSH    BX
        PUSH    DX
        MOV     BX,10
FORMAT_DECIMAL_NEXT:
        XOR     DX,DX
        DIV     BX
        ADD     DL,'0'
        MOV     [DI],DL
        DEC     DI
        LOOP    FORMAT_DECIMAL_NEXT
        POP     DX
        POP     BX
        RET
FORMAT_DECIMAL_FIELD ENDP

SHOW_WELCOME PROC NEAR
        ; DOS 5's Welcome panel is a single-line 58x11 dialog with a two-cell
        ; right shadow and a two-row bottom shadow.
        MOV     AL,DIALOG_ROW_OFFSET
        CALL    SET_ROW_OFFSET
        MOV     AL,' '
        MOV     BL,08H
        MOV     DH,6
WELCOME_RIGHT_SHADOW:
        MOV     DL,70
        MOV     CX,2
        CALL    DOSUI_FILL_ROW
        INC     DH
        CMP     DH,15
        JBE     WELCOME_RIGHT_SHADOW
        MOV     DH,16
        MOV     DL,14
        MOV     CX,58
        CALL    DOSUI_FILL_ROW

        MOV     AL,0C4H
        MOV     BL,70H
        MOV     DH,5
        MOV     DL,12
        MOV     CX,58
        CALL    DOSUI_FILL_ROW
        MOV     AL,0DAH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0BFH
        MOV     DL,69
        CALL    DOSUI_FILL_ROW

        MOV     DH,6
WELCOME_INTERIOR:
        MOV     AL,0B3H
        MOV     BL,70H
        MOV     DL,12
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,' '
        MOV     DL,13
        MOV     CX,56
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B3H
        MOV     DL,69
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        INC     DH
        CMP     DH,12
        JBE     WELCOME_INTERIOR

        MOV     AL,0C4H
        MOV     DH,13
        MOV     DL,12
        MOV     CX,58
        CALL    DOSUI_FILL_ROW
        MOV     AL,0C3H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B4H
        MOV     DL,69
        CALL    DOSUI_FILL_ROW

        MOV     DH,14
        MOV     AL,0B3H
        MOV     DL,12
        CALL    DOSUI_FILL_ROW
        MOV     AL,' '
        MOV     DL,13
        MOV     CX,56
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B3H
        MOV     DL,69
        MOV     CX,1
        CALL    DOSUI_FILL_ROW

        MOV     DH,15
        MOV     AL,0C4H
        MOV     DL,12
        MOV     CX,58
        CALL    DOSUI_FILL_ROW
        MOV     AL,0C0H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0D9H
        MOV     DL,69
        CALL    DOSUI_FILL_ROW

        MOV     BL,70H
        MOV     SI,OFFSET WELCOME_TITLE_TEXT
        MOV     DH,7
        MOV     DL,27
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET WELCOME_COPYRIGHT_TEXT
        MOV     DH,9
        MOV     DL,17
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET WELCOME_RIGHTS_TEXT
        MOV     DH,10
        MOV     DL,31
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET WELCOME_GUIDE_TEXT
        MOV     DH,12
        MOV     DL,20
        CALL    DOSUI_WRITE_Z
        MOV     AL,'<'
        MOV     BL,7FH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,'>'
        MOV     DL,60
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET WELCOME_DISMISS_TEXT
        MOV     DH,14
        MOV     DL,22
        MOV     BL,70H
        CALL    DOSUI_WRITE_Z

        CALL    DRAW_DIALOG_STATUS
        MOV     DH,12
        MOV     DL,22
        CALL    DOSUI_SET_CURSOR
        CALL    DOSUI_SHOW_CURSOR
WELCOME_KEY:
        CALL    DOSUI_READ_EVENT
        JC      SHORT WELCOME_MOUSE
        CMP     AL,27
        JE      SHORT WELCOME_DONE
        CMP     AL,13
        JNE     WELCOME_KEY
WELCOME_GUIDE:
        XOR     AL,AL
        CALL    HELP_OPEN_AT
WELCOME_DONE:
        RET
WELCOME_MOUSE:
        CMP     DH,12                   ; < Press Enter ... > button
        JNE     SHORT WELCOME_MOUSE_NOT_GUIDE
        CMP     DL,20
        JB      WELCOME_KEY
        CMP     DL,60
        JA      WELCOME_KEY
        JMP     SHORT WELCOME_GUIDE
WELCOME_MOUSE_NOT_GUIDE:
        CMP     DH,14                   ; < Press ESC ... > button
        JNE     WELCOME_KEY
        CMP     DL,22
        JB      WELCOME_KEY
        CMP     DL,59
        JA      WELCOME_KEY
        JMP     SHORT WELCOME_DONE
SHOW_WELCOME ENDP


; Draw a three-row, single-line input frame. DH:DL is the top-left cell and
; CX is the complete width. DOS 5 uses the dialog's 70h attribute throughout.
DRAW_DIALOG_INPUT PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    BP
        PUSH    DI
        MOV     BP,DX
        MOV     DI,CX

        MOV     AL,0C4H
        MOV     BL,70H
        CALL    DOSUI_FILL_ROW
        MOV     AL,0DAH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     DX,BP
        MOV     AX,DI
        ADD     DL,AL
        DEC     DL
        MOV     AL,0BFH
        CALL    DOSUI_FILL_ROW

        MOV     DX,BP
        INC     DH
        MOV     AL,0B3H
        CALL    DOSUI_FILL_ROW
        INC     DL
        MOV     AL,' '
        MOV     CX,DI
        SUB     CX,2
        CALL    DOSUI_FILL_ROW
        MOV     DX,BP
        INC     DH
        MOV     AX,DI
        ADD     DL,AL
        DEC     DL
        MOV     AL,0B3H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW

        MOV     DX,BP
        ADD     DH,2
        MOV     AL,0C4H
        MOV     CX,DI
        CALL    DOSUI_FILL_ROW
        MOV     AL,0C0H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     DX,BP
        ADD     DH,2
        MOV     AX,DI
        ADD     DL,AL
        DEC     DL
        MOV     AL,0D9H
        CALL    DOSUI_FILL_ROW

        POP     DI
        POP     BP
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
DRAW_DIALOG_INPUT ENDP

; The dialog keys on the status line; what is drawn after it is a dialog,
; centred on a screen taller than 25 rows.
DRAW_DIALOG_STATUS PROC NEAR
        XOR     AL,AL
        CALL    SET_ROW_OFFSET
        MOV     AL,' '
        MOV     BL,3FH
        MOV     DH,STATUS_ROW
        XOR     DL,DL
        MOV     CX,80
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET WELCOME_STATUS_TEXT
        CALL    DOSUI_WRITE_Z
        MOV     AL,DIALOG_ROW_OFFSET
        CALL    SET_ROW_OFFSET
        RET
DRAW_DIALOG_STATUS ENDP

; AL = the row offset DOSUI adds while a dialog laid out for 25 rows is up.
SET_ROW_OFFSET PROC NEAR
        PUSH    AX
        MOV     DOSUI_ROW_OFFSET,AL
        MOV     AH,STATUS_ROW
        SUB     AH,AL
        MOV     STATUS_HIT_ROW,AH
        POP     AX
        RET
SET_ROW_OFFSET ENDP

; /H: the 8x8 font (INT 10h AX=1112h) gives a VGA 50 rows and an EGA 43, as
; DOS 5 EDIT shows them; the BIOS's row count (40:84h) lays out the rest.
SETUP_ROWS PROC NEAR
        TEST    BYTE PTR EDIT_SWITCHES,4
        JZ      SHORT SETUP_ROWS_DONE
        MOV     AX,1112H
        XOR     BL,BL
        INT     10H
        PUSH    ES
        MOV     AX,40H
        MOV     ES,AX
        MOV     AL,ES:[84H]
        POP     ES
        INC     AL
        CMP     AL,25
        JBE     SHORT SETUP_ROWS_DONE
        CMP     AL,60
        JA      SHORT SETUP_ROWS_DONE
        MOV     DOSUI_ROWS,AL
        DEC     AL
        MOV     STATUS_ROW,AL
        MOV     STATUS_HIT_ROW,AL
        DEC     AL
        MOV     SCROLL_ROW,AL
        DEC     AL
        MOV     TEXT_LAST_ROW,AL
        SUB     AL,TEXT_FIRST_ROW-1
        XOR     AH,AH
        MOV     TEXT_ROWS,AX
        SHR     AX,1                    ; DOS 5 /H puts a far jump's line on
        SHR     AX,1                    ; row 13, the view's twelfth (HN)
        MOV     FAR_JUMP_LINES,AX
        MOV     AL,DOSUI_ROWS
        SUB     AL,25
        MOV     VERIFY_ROW_OFFSET,AL
        SHR     AL,1
        MOV     DIALOG_ROW_OFFSET,AL
        MOV     AL,SCROLL_BARS
        CALL    SET_SCROLL_BARS
SETUP_ROWS_DONE:
        RET
SETUP_ROWS ENDP

; DOS 5 EDIT sets the pointer up itself at every row count (CTMouse 2.1b4
; and MOUSE.COM captures, receipt 4ed6f22a4): after the reset, software
; reset (0021h), a text pointer (000Ah, 77FFh/7700h), an event handler for
; mask 001Fh (000Ch), X 3..632 (0007h), Y 4..rows*8-4 (0008h), the pointer
; to 0,0 (0004h); all of it once more after a further reset; 0021h at exit.
SETUP_MOUSE_ROWS PROC NEAR
        CMP     BYTE PTR DOSUI_MOUSE_PRESENT,0
        JE      SHORT SETUP_MOUSE_ROWS_DONE
        CALL    SETUP_MOUSE_ONCE
        XOR     AX,AX                   ; reset, and all of it again
        INT     33H
        CALL    SETUP_MOUSE_ONCE
SETUP_MOUSE_ROWS_DONE:
        RET
SETUP_MOUSE_ROWS ENDP

SETUP_MOUSE_ONCE PROC NEAR
        MOV     AX,0021H                ; software reset
        INT     33H
        MOV     AX,000AH                ; the text pointer
        XOR     BX,BX
        MOV     CX,77FFH
        MOV     DX,7700H
        INT     33H
        MOV     AX,000CH                ; an event handler for every event
        MOV     CX,001FH
        MOV     DX,OFFSET MOUSE_EVENT_HANDLER
        INT     33H
        MOV     AX,0007H                ; X 3..632
        MOV     CX,3
        MOV     DX,632
        INT     33H
        MOV     AL,DOSUI_ROWS           ; Y 4..rows*8-4
        MOV     AH,8
        MUL     AH
        SUB     AX,4
        MOV     DX,AX
        MOV     CX,4
        MOV     AX,0008H
        INT     33H
        MOV     AX,0004H                ; the pointer to the top left
        XOR     CX,CX
        XOR     DX,DX
        INT     33H
        RET
SETUP_MOUSE_ONCE ENDP

; Called by the driver for the events SETUP_MOUSE_ONCE registers; EDIT
; polls instead (DOSUI_READ_EVENT).
MOUSE_EVENT_HANDLER PROC FAR
        RET
MOUSE_EVENT_HANDLER ENDP

; DOS 5 seeds Find with the word under the cursor and selects it, so typing
; immediately replaces the suggestion. Whitespace and DOS line endings bound
; the word; the field itself remains safely capped.
PREFILL_FIND_WORD PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        MOV     DI,OFFSET SEARCH_PATTERN
        MOV     BYTE PTR [DI],0
        MOV     SI,CURSOR_OFFSET
        CMP     SI,BUFFER_LENGTH
        JAE     SHORT PREFILL_FIND_DONE
        CMP     BYTE PTR BUFFER[SI],' '
        JBE     SHORT PREFILL_FIND_DONE
PREFILL_FIND_BACK:
        OR      SI,SI
        JZ      SHORT PREFILL_FIND_COPY
        CMP     BYTE PTR BUFFER[SI-1],' '
        JBE     SHORT PREFILL_FIND_COPY
        DEC     SI
        JMP     SHORT PREFILL_FIND_BACK
PREFILL_FIND_COPY:
        MOV     CX,31
PREFILL_FIND_COPY_NEXT:
        JCXZ   PREFILL_FIND_TERMINATE
        CMP     SI,BUFFER_LENGTH
        JAE     SHORT PREFILL_FIND_TERMINATE
        MOV     AL,BUFFER[SI]
        CMP     AL,' '
        JBE     SHORT PREFILL_FIND_TERMINATE
        MOV     [DI],AL
        INC     DI
        INC     SI
        DEC     CX
        JMP     SHORT PREFILL_FIND_COPY_NEXT
PREFILL_FIND_TERMINATE:
        MOV     BYTE PTR [DI],0
PREFILL_FIND_DONE:
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        POP     AX
        RET
PREFILL_FIND_WORD ENDP

PROMPT_FIND PROC NEAR
        CALL    PREFILL_FIND_WORD
PROMPT_FIND_DRAW:
        MOV     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET FIND_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET FIND_TITLE_TEXT
        MOV     BL,70H
        MOV     DH,7
        MOV     DL,36
        CALL    DOSUI_WRITE_Z
        MOV     DH,8
        MOV     DL,23
        MOV     CX,44
        CALL    DRAW_DIALOG_INPUT
        MOV     SI,OFFSET FIND_TEXT
        MOV     DH,9
        MOV     DL,12
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET SEARCH_PATTERN
        MOV     BL,07H
        MOV     DL,24
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET FIND_OPTIONS_TEXT
        MOV     BL,70H
        MOV     DH,13
        MOV     DL,15
        CALL    DOSUI_WRITE_Z
        MOV     DH,15
        MOV     DL,10
        MOV     CX,59
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET FIND_BUTTONS_TEXT
        MOV     DH,16
        MOV     DL,19
        CALL    DOSUI_WRITE_Z
        MOV     AL,'<'
        MOV     BL,7FH
        MOV     DH,16
        MOV     DL,19
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,'>'
        MOV     DL,24
        CALL    DOSUI_FILL_ROW
        CALL    SAVE_SEARCH_SETTINGS
        MOV     SI,OFFSET FIND_CONTROLS
        MOV     CX,0306H                ; six controls, OK is the default
        XOR     AL,AL
        CALL    RUN_DIALOG
        CALL    KEEP_OR_RESTORE_SEARCH
        CMP     AL,DLG_HELP
        JNE     SHORT PROMPT_FIND_SETTLED
        CALL    SHOW_KEYBOARD_HELP
        CALL    DRAW_SCREEN
        JMP     PROMPT_FIND_DRAW
PROMPT_FIND_SETTLED:
        RET
PROMPT_FIND ENDP

PROMPT_REPLACE PROC NEAR
        MOV     BYTE PTR REPLACE_ALL_REQUESTED,0
PROMPT_REPLACE_DRAW:
        MOV     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET REPLACE_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET CHANGE_TITLE_TEXT
        MOV     BL,70H
        MOV     DH,5
        MOV     DL,36
        CALL    DOSUI_WRITE_Z
        MOV     DH,6
        MOV     DL,24
        MOV     CX,43
        CALL    DRAW_DIALOG_INPUT
        MOV     SI,OFFSET FIND_TEXT
        MOV     BL,70H
        MOV     DH,7
        MOV     DL,13
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET SEARCH_PATTERN
        MOV     BL,07H
        MOV     DL,25
        CALL    DOSUI_WRITE_Z
        MOV     DH,9
        MOV     DL,24
        MOV     CX,43
        CALL    DRAW_DIALOG_INPUT
        MOV     SI,OFFSET REPLACE_TEXT
        MOV     BL,70H
        MOV     DH,10
        MOV     DL,13
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET CHANGE_OPTIONS_TEXT
        MOV     DH,14
        MOV     DL,16
        CALL    DOSUI_WRITE_Z
        MOV     DH,16
        MOV     DL,11
        MOV     CX,58
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET CHANGE_BUTTONS_TEXT
        MOV     DH,17
        MOV     DL,13
        CALL    DOSUI_WRITE_Z
        MOV     AL,'<'
        MOV     BL,7FH
        MOV     DH,17
        MOV     DL,13
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,'>'
        MOV     DL,31
        CALL    DOSUI_FILL_ROW
        CALL    SAVE_SEARCH_SETTINGS
        MOV     SI,OFFSET CHANGE_CONTROLS
        MOV     CX,0408H                ; Find and Verify is the default
        XOR     AL,AL
        CALL    RUN_DIALOG
        CALL    KEEP_OR_RESTORE_SEARCH
        CMP     AL,DLG_HELP
        JNE     SHORT PROMPT_REPLACE_ANSWER
        CALL    SHOW_KEYBOARD_HELP
        CALL    DRAW_SCREEN
        JMP     PROMPT_REPLACE_DRAW
PROMPT_REPLACE_ANSWER:
        CMP     AL,3
        JNE     SHORT PROMPT_REPLACE_DONE
        MOV     BYTE PTR REPLACE_ALL_REQUESTED,1
PROMPT_REPLACE_DONE:
        RET
PROMPT_REPLACE ENDP

PROMPT_FILE_NAME PROC NEAR
        CMP     BYTE PTR SAVE_AS_RETRY,0
        JNE     SHORT PROMPT_FILE_NAME_DRAW
        CALL    SAVE_AS_ENTER_HOME
PROMPT_FILE_NAME_DRAW:
        CALL    FILL_OPEN_LISTS         ; the real directories and drives
        CALL    DRAW_SCREEN
        MOV     BYTE PTR MENU_ACTIVE,MENU_FILE
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET FILE_NAME_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET SAVE_AS_TITLE_TEXT
        MOV     BL,70H
        MOV     DH,3
        MOV     DL,35
        CALL    DOSUI_WRITE_Z
        MOV     DH,4
        MOV     DL,32
        MOV     CX,27
        CALL    DRAW_DIALOG_INPUT
        MOV     SI,OFFSET FILE_NAME_TEXT
        MOV     DH,5
        MOV     DL,21
        CALL    DOSUI_WRITE_Z
        CMP     BYTE PTR SAVE_AS_RETRY,0
        JE      SHORT PROMPT_FILE_NAME_NO_INITIAL
        MOV     SI,OFFSET FILE_NAME
        MOV     BL,07H
        MOV     DL,33
        CALL    DOSUI_WRITE_Z
PROMPT_FILE_NAME_NO_INITIAL:
        MOV     SI,OFFSET OPEN_PATH
        MOV     BL,70H
        MOV     DH,7
        MOV     DL,21
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET SAVE_AS_DIRS_TEXT
        MOV     DH,8
        MOV     DL,34
        CALL    DOSUI_WRITE_Z
        CALL    DRAW_SAVE_AS_LIST
        MOV     DH,18
        MOV     DL,19
        MOV     CX,42
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET SAVE_AS_BUTTONS_TEXT
        MOV     DH,19
        MOV     DL,26
        CALL    DOSUI_WRITE_Z
        MOV     AL,'<'
        MOV     BL,7FH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,'>'
        MOV     DL,31
        CALL    DOSUI_FILL_ROW
        CMP     BYTE PTR SAVE_AS_RETRY,0
        JNE     SHORT PROMPT_FILE_NAME_READ
        CALL    BASE_NAME_INTO_FIELD    ; DOS 5 offers the file's own name
PROMPT_FILE_NAME_READ:
        MOV     WORD PTR SAVE_AS_DRIVE_SELECTED,0FFFFH
        MOV     WORD PTR SAVE_AS_DRIVE_TOP,0
        MOV     SI,OFFSET SAVE_AS_CONTROLS
        MOV     CX,0205H                ; OK is the default
        XOR     AL,AL
        CALL    RUN_DIALOG
        CMP     AL,DLG_HELP
        JNE     SHORT PROMPT_FILE_NAME_SETTLED
        CALL    SHOW_KEYBOARD_HELP
        MOV     BYTE PTR SAVE_AS_RETRY,1
        JMP     PROMPT_FILE_NAME_DRAW
PROMPT_FILE_NAME_SETTLED:
        OR      AL,AL
        JNZ     SHORT PROMPT_FILE_NAME_OK
        CALL    SAVE_AS_GO_HOME         ; Cancel: where it all began
        XOR     AL,AL
        JMP     SHORT PROMPT_FILE_NAME_DONE
PROMPT_FILE_NAME_OK:
        CALL    SAVE_AS_NAVIGATE        ; a drive or directory: go there
        JC      SHORT PROMPT_FILE_NAME_AGAIN
        CMP     BYTE PTR SAVE_AS_NAVIGATED,0
        JE      SHORT PROMPT_FILE_NAME_KEEP
        CALL    SAVE_AS_ABSOLUTE_NAME
PROMPT_FILE_NAME_KEEP:
        CALL    SAVE_AS_GO_HOME
        MOV     AL,1
PROMPT_FILE_NAME_DONE:
        MOV     BYTE PTR SAVE_AS_RETRY,0
        RET
PROMPT_FILE_NAME_AGAIN:
        MOV     BYTE PTR SAVE_AS_RETRY,1
        JMP     PROMPT_FILE_NAME_DRAW
PROMPT_FILE_NAME ENDP

; Save As lists and moves through the real directories and drives, as
; DOS 5 EDIT does (QEMU captures, 2026-11-11, plans V, V2): it opens in the
; directory of the file being edited, OK on a drive (X:) or a directory
; (NAME\, ..\) goes there and offers "*", a name with a missing directory
; gets "Path not found", a missing drive "Device unavailable", and Cancel
; leaves the current drive and directory as they were. A file saved after
; moving keeps its full path.
SAVE_AS_ENTER_HOME PROC NEAR
        MOV     AH,19H
        INT     21H
        ADD     AL,'A'
        MOV     SAVE_AS_HOME,AL
        MOV     WORD PTR SAVE_AS_HOME+1,'\:'
        MOV     SI,OFFSET SAVE_AS_HOME+3
        XOR     DL,DL
        MOV     AH,47H
        INT     21H
        JNC     SHORT SAVE_AS_HOME_KNOWN
        MOV     BYTE PTR SAVE_AS_HOME+3,0
SAVE_AS_HOME_KNOWN:
        MOV     BYTE PTR SAVE_AS_NAVIGATED,0
        MOV     SI,OFFSET DISPLAY_NAME  ; the file's own directory, if named
        XOR     BX,BX
SAVE_AS_HOME_SCAN:
        LODSB
        OR      AL,AL
        JZ      SHORT SAVE_AS_HOME_SCANNED
        CMP     AL,'\'
        JE      SHORT SAVE_AS_HOME_MARK
        CMP     AL,':'
        JNE     SAVE_AS_HOME_SCAN
SAVE_AS_HOME_MARK:
        MOV     BX,SI
        JMP     SAVE_AS_HOME_SCAN
SAVE_AS_HOME_SCANNED:
        OR      BX,BX
        JZ      SHORT SAVE_AS_HOME_DONE
        MOV     SI,OFFSET DISPLAY_NAME
        MOV     DI,OFFSET INI_SCRATCH_PATH
        MOV     CX,BX
        SUB     CX,SI
        REP     MOVSB
        MOV     BYTE PTR [DI],0
        CMP     BYTE PTR [DI-1],'\'     ; "DIR\" -> "DIR", but keep "\", "X:\"
        JNE     SHORT SAVE_AS_HOME_GO
        LEA     AX,[DI-1]
        CMP     AX,OFFSET INI_SCRATCH_PATH
        JE      SHORT SAVE_AS_HOME_GO
        CMP     BYTE PTR [DI-2],':'
        JE      SHORT SAVE_AS_HOME_GO
        MOV     BYTE PTR [DI-1],0
SAVE_AS_HOME_GO:
        MOV     SI,OFFSET INI_SCRATCH_PATH
        CALL    SAVE_AS_CHANGE_TO
        JC      SHORT SAVE_AS_HOME_DONE
        MOV     BYTE PTR SAVE_AS_NAVIGATED,1
SAVE_AS_HOME_DONE:
        RET
SAVE_AS_ENTER_HOME ENDP

; Go to the drive and directory named at DS:SI ("X:", "X:\DIR", "DIR",
; ".."). Carry set: the drive (AL = 1) or the directory (AL = 2) is missing.
SAVE_AS_CHANGE_TO PROC NEAR
        CMP     BYTE PTR [SI+1],':'
        JNE     SHORT SAVE_AS_CHANGE_DIRECTORY
        PUSH    SI
        MOV     DI,OFFSET OPEN_FCB
        MOV     AX,2900H
        INT     21H
        POP     SI
        CMP     AL,0FFH
        JNE     SHORT SAVE_AS_CHANGE_DRIVE
        MOV     AL,1
        STC
        RET
SAVE_AS_CHANGE_DRIVE:
        MOV     DL,[SI]
        AND     DL,0DFH
        SUB     DL,'A'
        MOV     AH,0EH
        INT     21H
        CMP     BYTE PTR [SI+2],0
        JE      SHORT SAVE_AS_CHANGED
SAVE_AS_CHANGE_DIRECTORY:
        MOV     DX,SI
        MOV     AH,3BH
        INT     21H
        JNC     SHORT SAVE_AS_CHANGED
        MOV     AL,2
        STC
        RET
SAVE_AS_CHANGED:
        CLC
        RET
SAVE_AS_CHANGE_TO ENDP

SAVE_AS_GO_HOME PROC NEAR
        MOV     SI,OFFSET SAVE_AS_HOME
        CALL    SAVE_AS_CHANGE_TO
        RET
SAVE_AS_GO_HOME ENDP

; After OK. Carry set: the dialog goes on (it moved to a drive or a
; directory, refused one, or the name is a pattern); carry clear: save.
SAVE_AS_NAVIGATE PROC NEAR
        MOV     SI,OFFSET FILE_NAME
        CMP     BYTE PTR [SI],0
        JNE     SHORT SAVE_AS_NAVIGATE_NAMED
        JMP     SAVE_AS_STAY
SAVE_AS_NAVIGATE_NAMED:
        XOR     BX,BX                   ; BX = after the last '\'
SAVE_AS_NAVIGATE_SCAN:
        LODSB
        OR      AL,AL
        JZ      SHORT SAVE_AS_NAVIGATE_SCANNED
        CMP     AL,'*'
        JE      SHORT SAVE_AS_STAY
        CMP     AL,'?'
        JE      SHORT SAVE_AS_STAY
        CMP     AL,'\'
        JNE     SAVE_AS_NAVIGATE_SCAN
        MOV     BX,SI
        JMP     SAVE_AS_NAVIGATE_SCAN
SAVE_AS_NAVIGATE_SCANNED:
        DEC     SI                      ; SI = the terminator
        CMP     BYTE PTR FILE_NAME+1,':'
        JNE     SHORT SAVE_AS_NOT_DRIVE
        CMP     BYTE PTR FILE_NAME+2,0
        JE      SHORT SAVE_AS_GO_THERE  ; "X:"
SAVE_AS_NOT_DRIVE:
        OR      BX,BX
        JZ      SHORT SAVE_AS_SAVE      ; a plain name
        CMP     BX,SI
        JNE     SHORT SAVE_AS_CHECK_PATH
        LEA     AX,[SI-1]               ; "NAME\": go there (keep "\", "X:\")
        CMP     AX,OFFSET FILE_NAME
        JE      SHORT SAVE_AS_GO_THERE
        CMP     BYTE PTR [SI-2],':'
        JE      SHORT SAVE_AS_GO_THERE
        MOV     BYTE PTR [SI-1],0
        CALL    SAVE_AS_GO_FIELD
        MOV     BYTE PTR [SI-1],'\'
        RET
SAVE_AS_GO_THERE:
        CALL    SAVE_AS_GO_FIELD
        RET
SAVE_AS_CHECK_PATH:                     ; "DIR\NAME": the directory must exist
        MOV     AL,[BX-1]
        PUSH    AX
        MOV     BYTE PTR [BX-1],0
        LEA     AX,[BX-1]
        CMP     AX,OFFSET FILE_NAME     ; "\NAME": the root
        JE      SHORT SAVE_AS_PATH_OK
        CMP     BYTE PTR [BX-2],':'     ; "X:\NAME"
        JE      SHORT SAVE_AS_PATH_OK
        MOV     DX,OFFSET FILE_NAME
        MOV     AX,4300H
        INT     21H
        JC      SHORT SAVE_AS_PATH_MISSING
        TEST    CL,10H
        JNZ     SHORT SAVE_AS_PATH_OK
SAVE_AS_PATH_MISSING:
        POP     AX
        MOV     [BX-1],AL
        CALL    SHOW_PATH_NOT_FOUND
        JMP     SHORT SAVE_AS_STAY
SAVE_AS_PATH_OK:
        POP     AX
        MOV     [BX-1],AL
SAVE_AS_SAVE:
        CLC
        RET
SAVE_AS_STAY:
        STC
        RET
SAVE_AS_NAVIGATE ENDP

; Go to the drive or directory in FILE_NAME; "*" replaces it on success.
; Always carry set: the dialog goes on.
SAVE_AS_GO_FIELD PROC NEAR
        PUSH    SI
SAVE_AS_GO_FIELD_TRY:
        MOV     SI,OFFSET FILE_NAME
        CALL    SAVE_AS_CHANGE_TO
        JNC     SHORT SAVE_AS_GO_FIELD_DONE
        CMP     AL,1
        JE      SHORT SAVE_AS_GO_FIELD_DEVICE
        CALL    SHOW_PATH_NOT_FOUND
        JMP     SHORT SAVE_AS_GO_FIELD_FAILED
SAVE_AS_GO_FIELD_DEVICE:
        CALL    SHOW_DEVICE_UNAVAILABLE
        JC      SAVE_AS_GO_FIELD_TRY    ; Retry
SAVE_AS_GO_FIELD_FAILED:
        POP     SI
        STC
        RET
SAVE_AS_GO_FIELD_DONE:
        MOV     WORD PTR FILE_NAME,'*'
        MOV     BYTE PTR SAVE_AS_NAVIGATED,1
        POP     SI
        STC
        RET
SAVE_AS_GO_FIELD ENDP

; FILE_NAME -> its full path from the current drive and directory.
SAVE_AS_ABSOLUTE_NAME PROC NEAR
        CMP     BYTE PTR FILE_NAME+1,':'
        JE      SHORT SAVE_AS_ABSOLUTE_DONE
        MOV     AH,19H
        INT     21H
        ADD     AL,'A'
        MOV     DI,OFFSET INI_SCRATCH_PATH
        MOV     [DI],AL
        MOV     BYTE PTR [DI+1],':'
        ADD     DI,2
        CMP     BYTE PTR FILE_NAME,'\'
        JE      SHORT SAVE_AS_ABSOLUTE_APPEND
        MOV     BYTE PTR [DI],'\'
        INC     DI
        MOV     SI,DI
        XOR     DL,DL
        MOV     AH,47H
        INT     21H
        JC      SHORT SAVE_AS_ABSOLUTE_DONE
SAVE_AS_ABSOLUTE_END:
        CMP     BYTE PTR [DI],0
        JE      SHORT SAVE_AS_ABSOLUTE_SEPARATOR
        INC     DI
        JMP     SAVE_AS_ABSOLUTE_END
SAVE_AS_ABSOLUTE_SEPARATOR:
        CMP     BYTE PTR [DI-1],'\'
        JE      SHORT SAVE_AS_ABSOLUTE_APPEND
        MOV     BYTE PTR [DI],'\'
        INC     DI
SAVE_AS_ABSOLUTE_APPEND:
        MOV     SI,OFFSET FILE_NAME
SAVE_AS_ABSOLUTE_COPY:
        LODSB
        CMP     DI,OFFSET INI_SCRATCH_PATH+63
        JB      SHORT SAVE_AS_ABSOLUTE_STORE
        XOR     AL,AL
SAVE_AS_ABSOLUTE_STORE:
        MOV     [DI],AL
        INC     DI
        OR      AL,AL
        JNZ     SAVE_AS_ABSOLUTE_COPY
        MOV     SI,OFFSET INI_SCRATCH_PATH
        MOV     DI,OFFSET FILE_NAME
        MOV     CX,64
        REP     MOVSB
SAVE_AS_ABSOLUTE_DONE:
        RET
SAVE_AS_ABSOLUTE_NAME ENDP

DRAW_SAVE_AS_LIST PROC NEAR
        MOV     SI,OFFSET SAVE_AS_LIST_BOX
        CALL    DRAW_LIST_BOX
        RET
DRAW_SAVE_AS_LIST ENDP

CONFIRM_OVERWRITE PROC NEAR
        MOV     BYTE PTR CONFIRM_FOCUS,0
CONFIRM_OVERWRITE_DRAW:
        MOV     SI,OFFSET OVERWRITE_TEXT
        MOV     DL,24
        CALL    DRAW_FILE_CONFIRMATION
        CALL    READ_FILE_CONFIRMATION
        CMP     AL,4
        JNE     SHORT CONFIRM_OVERWRITE_ANSWER
        CALL    SHOW_KEYBOARD_HELP
        JMP     CONFIRM_OVERWRITE_DRAW
CONFIRM_OVERWRITE_ANSWER:
        CMP     AL,1
        JE      SHORT CONFIRM_OVERWRITE_DONE
        XOR     AL,AL
CONFIRM_OVERWRITE_DONE:
        RET
CONFIRM_OVERWRITE ENDP

; Run the four-button file confirmation drawn by DRAW_FILE_CONFIRMATION.
; DOS 5 moves the focus with Tab, Shift+Tab and the arrows (the focused
; button has bright brackets and holds the cursor) and Enter presses the
; focused button. Y and N answer directly, Esc cancels and F1 is Help.
; AL returns 1 Yes, 2 No, 3 Cancel, 4 Help.
READ_FILE_CONFIRMATION PROC NEAR
READ_CONFIRM_KEY:
        CALL    DOSUI_READ_EVENT
        JNC     SHORT READ_CONFIRM_HAVE_KEY
        CALL    FILE_CONFIRM_HIT
        OR      AL,AL
        JZ      READ_CONFIRM_KEY
        RET
READ_CONFIRM_HAVE_KEY:
        OR      AL,AL
        JZ      SHORT READ_CONFIRM_EXTENDED
        CMP     AL,13
        JE      SHORT READ_CONFIRM_ENTER
        CMP     AL,27
        JE      SHORT READ_CONFIRM_CANCEL
        CMP     AL,9
        JE      SHORT READ_CONFIRM_NEXT
        OR      AL,20H
        CMP     AL,'y'
        JE      SHORT READ_CONFIRM_YES
        CMP     AL,'n'
        JE      SHORT READ_CONFIRM_NO
        JMP     READ_CONFIRM_KEY
READ_CONFIRM_EXTENDED:
        CMP     AH,0FH                  ; Shift+Tab
        JE      SHORT READ_CONFIRM_PREVIOUS
        CMP     AH,4BH                  ; Left
        JE      SHORT READ_CONFIRM_PREVIOUS
        CMP     AH,48H                  ; Up
        JE      SHORT READ_CONFIRM_PREVIOUS
        CMP     AH,4DH                  ; Right
        JE      SHORT READ_CONFIRM_NEXT
        CMP     AH,50H                  ; Down
        JE      SHORT READ_CONFIRM_NEXT
        CMP     AH,3BH                  ; F1
        JNE     READ_CONFIRM_KEY
        MOV     AL,4
        RET
READ_CONFIRM_NEXT:
        INC     BYTE PTR CONFIRM_FOCUS
        JMP     SHORT READ_CONFIRM_MOVED
READ_CONFIRM_PREVIOUS:
        DEC     BYTE PTR CONFIRM_FOCUS
READ_CONFIRM_MOVED:
        AND     BYTE PTR CONFIRM_FOCUS,3
        CALL    DRAW_CONFIRM_FOCUS
        JMP     READ_CONFIRM_KEY
READ_CONFIRM_ENTER:
        MOV     AL,CONFIRM_FOCUS
        INC     AL
        RET
READ_CONFIRM_CANCEL:
        MOV     AL,3
        RET
READ_CONFIRM_YES:
        MOV     AL,1
        RET
READ_CONFIRM_NO:
        MOV     AL,2
        RET
READ_FILE_CONFIRMATION ENDP

; Paint the four confirmation buttons' brackets, bright on the focused one,
; and park the cursor on the focused button's label.
DRAW_CONFIRM_FOCUS PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        XOR     SI,SI
        MOV     CX,1
        MOV     DH,14
DRAW_CONFIRM_FOCUS_NEXT:
        MOV     AL,CONFIRM_FOCUS
        XOR     AH,AH
        SHL     AX,1
        MOV     BL,70H
        CMP     AX,SI
        JNE     SHORT DRAW_CONFIRM_FOCUS_ATTR
        MOV     BL,7FH
DRAW_CONFIRM_FOCUS_ATTR:
        MOV     DL,CONFIRM_BRACKETS[SI]
        MOV     AL,'<'
        CALL    DOSUI_FILL_ROW
        MOV     DL,CONFIRM_BRACKETS[SI+1]
        MOV     AL,'>'
        CALL    DOSUI_FILL_ROW
        ADD     SI,2
        CMP     SI,8
        JB      DRAW_CONFIRM_FOCUS_NEXT
        MOV     BL,CONFIRM_FOCUS
        XOR     BH,BH
        MOV     DL,CONFIRM_CURSORS[BX]
        CALL    DOSUI_SET_CURSOR
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
DRAW_CONFIRM_FOCUS ENDP

; DS:SI message, DL message column. The common DOS 5 file confirmation frame
; keeps the Yes button focused and leaves the cursor visible on its accelerator.
DRAW_FILE_CONFIRMATION PROC NEAR
        PUSH    DX
        PUSH    SI
        CALL    DRAW_SCREEN
        MOV     BYTE PTR MENU_ACTIVE,MENU_FILE
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET FILE_CONFIRM_RECT
        CALL    DOSUI_MENU_BOX
        POP     SI
        POP     DX
        MOV     BL,70H
        MOV     DH,11
        CALL    DOSUI_WRITE_Z
        MOV     DH,13
        MOV     DL,17
        MOV     CX,46
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET FILE_CONFIRM_BUTTONS_TEXT
        MOV     DH,14
        MOV     DL,20
        CALL    DOSUI_WRITE_Z
        CALL    DRAW_CONFIRM_FOCUS
        CALL    DOSUI_SHOW_CURSOR
        RET
DRAW_FILE_CONFIRMATION ENDP

FIND_FROM_CURSOR PROC NEAR
        CMP     BYTE PTR SEARCH_PATTERN,0
        JE      FIND_FROM_CURSOR_FAIL
        MOV     SI,CURSOR_OFFSET
FIND_CANDIDATE:
        CMP     SI,BUFFER_LENGTH
        JAE     FIND_FROM_CURSOR_FAIL
        MOV     DI,OFFSET SEARCH_PATTERN
        MOV     BX,SI
FIND_COMPARE:
        MOV     AL,[DI]
        OR      AL,AL
        JZ      FIND_FROM_CURSOR_FOUND
        CMP     BX,BUFFER_LENGTH
        JAE     FIND_CANDIDATE_NEXT
        MOV     AH,BUFFER[BX]
        CMP     BYTE PTR MATCH_CASE,0   ; Match Upper/Lowercase
        JNE     SHORT FIND_BUFFER_FOLDED
        CMP     AL,'a'
        JB      SHORT FIND_SEARCH_FOLDED
        CMP     AL,'z'
        JA      SHORT FIND_SEARCH_FOLDED
        SUB     AL,20H
FIND_SEARCH_FOLDED:
        CMP     AH,'a'
        JB      SHORT FIND_BUFFER_FOLDED
        CMP     AH,'z'
        JA      SHORT FIND_BUFFER_FOLDED
        SUB     AH,20H
FIND_BUFFER_FOLDED:
        CMP     AL,AH
        JNE     FIND_CANDIDATE_NEXT
        INC     DI
        INC     BX
        JMP     FIND_COMPARE
FIND_CANDIDATE_NEXT:
        INC     SI
        JMP     FIND_CANDIDATE
FIND_FROM_CURSOR_FOUND:
        CMP     BYTE PTR WHOLE_WORD,0   ; Whole Word: no word byte around it
        JE      SHORT FIND_FROM_CURSOR_ACCEPT
        OR      SI,SI
        JZ      SHORT FIND_WHOLE_AFTER
        MOV     AL,BUFFER[SI-1]
        CALL    IS_WORD_BYTE
        JC      FIND_CANDIDATE_NEXT
FIND_WHOLE_AFTER:
        CMP     BX,BUFFER_LENGTH
        JAE     SHORT FIND_FROM_CURSOR_ACCEPT
        MOV     AL,BUFFER[BX]
        CALL    IS_WORD_BYTE
        JC      FIND_CANDIDATE_NEXT
FIND_FROM_CURSOR_ACCEPT:
        MOV     CURSOR_OFFSET,SI
        MOV     SEARCH_HIGHLIGHT_START,SI
        MOV     SEARCH_HIGHLIGHT_END,BX
        CLC
        RET
FIND_FROM_CURSOR_FAIL:
        MOV     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        MOV     WORD PTR SEARCH_HIGHLIGHT_END,0FFFFH
        STC
        RET
FIND_FROM_CURSOR ENDP

; Replace every non-overlapping case-insensitive match.  Capacity is proven
; for the complete result before the first mutation, so the operation either
; changes the whole buffer or leaves it untouched.
REPLACE_ALL PROC NEAR
        XOR     CX,CX
        MOV     SI,OFFSET SEARCH_PATTERN
REPLACE_ALL_COUNT_SEARCH:
        CMP     BYTE PTR [SI],0
        JE      SHORT REPLACE_ALL_COUNT_REPLACEMENT
        INC     SI
        INC     CX
        JMP     REPLACE_ALL_COUNT_SEARCH
REPLACE_ALL_COUNT_REPLACEMENT:
        OR      CX,CX
        JNZ     SHORT REPLACE_ALL_HAVE_SEARCH
        JMP     REPLACE_ALL_FAIL
REPLACE_ALL_HAVE_SEARCH:
        MOV     REPLACE_SEARCH_LENGTH,CX
        XOR     DX,DX
        MOV     SI,OFFSET REPLACE_PATTERN
REPLACE_ALL_COUNT_REPLACEMENT_NEXT:
        CMP     BYTE PTR [SI],0
        JE      SHORT REPLACE_ALL_FIND_MATCHES
        INC     SI
        INC     DX
        JMP     REPLACE_ALL_COUNT_REPLACEMENT_NEXT
REPLACE_ALL_FIND_MATCHES:
        MOV     REPLACE_REPLACEMENT_LENGTH,DX
        CALL    CURSOR_GLOBAL
        MOV     REPLACE_SAVED_CURSOR,AX
        MOV     REPLACE_SAVED_CURSOR+2,DX
        CALL    CALCULATE_POSITION      ; a cursor past its line's end keeps
        MOV     AX,CURRENT_COLUMN       ; its distance from the line's text
        MOV     REPLACE_OLD_COLUMN,AX
        MOV     AX,VIRTUAL_COLUMN
        MOV     REPLACE_OLD_VIRTUAL,AX
        MOV     AL,VIRTUAL_COLUMN_VALID
        MOV     REPLACE_WAS_VIRTUAL,AL
        XOR     AX,AX                   ; count the matches over the text
        MOV     REPLACE_MATCH_COUNT,AX
        MOV     REPLACE_MATCH_COUNT+2,AX
        XOR     DX,DX
REPLACE_ALL_FIND_NEXT:
        CALL    FIND_FROM_GLOBAL
        JC      SHORT REPLACE_ALL_PREFLIGHT
        ADD     WORD PTR REPLACE_MATCH_COUNT,1
        ADC     WORD PTR REPLACE_MATCH_COUNT+2,0
        CALL    CURSOR_GLOBAL
        ADD     AX,REPLACE_SEARCH_LENGTH
        ADC     DX,0
        JMP     REPLACE_ALL_FIND_NEXT
REPLACE_ALL_PREFLIGHT:
        MOV     AX,REPLACE_MATCH_COUNT
        OR      AX,REPLACE_MATCH_COUNT+2
        JZ      SHORT REPLACE_ALL_FAIL
        MOV     CX,REPLACE_REPLACEMENT_LENGTH
        SUB     CX,REPLACE_SEARCH_LENGTH
        JBE     SHORT REPLACE_ALL_APPLY  ; no growth
        MOV     AX,REPLACE_MATCH_COUNT  ; the growth must fit, all of it
        MOV     DX,REPLACE_MATCH_COUNT+2
        CALL    MUL32_16
        MOV     BX,AX
        MOV     CX,DX
        CALL    DOC_ROOM
        CMP     DX,CX
        JNE     REPLACE_ALL_ROOM
        CMP     AX,BX
REPLACE_ALL_ROOM:
        JAE     SHORT REPLACE_ALL_APPLY
        MOV     BYTE PTR MEMORY_FULL,1  ; "Out of memory", nothing changed
        JMP     SHORT REPLACE_ALL_RESTORE
REPLACE_ALL_APPLY:
        XOR     AX,AX
        XOR     DX,DX
REPLACE_ALL_APPLY_NEXT:
        CALL    FIND_FROM_GLOBAL
        JC      SHORT REPLACE_ALL_FAIL
        CALL    MAKE_VIEW_ROOM          ; room for the change
        CALL    REPLACE_TRACK_CURSOR
        CALL    REPLACE_CURRENT
        JC      SHORT REPLACE_ALL_FAIL
        SUB     WORD PTR REPLACE_MATCH_COUNT,1
        SBB     WORD PTR REPLACE_MATCH_COUNT+2,0
        MOV     AX,REPLACE_MATCH_COUNT
        OR      AX,REPLACE_MATCH_COUNT+2
        JZ      REPLACE_ALL_APPLIED
        CALL    CURSOR_GLOBAL
        JMP     REPLACE_ALL_APPLY_NEXT
REPLACE_ALL_APPLIED:
        CALL    REPLACE_ALL_RESTORE_CURSOR
        CALL    REPLACE_ALL_VIRTUAL
        MOV     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        MOV     WORD PTR SEARCH_HIGHLIGHT_END,0FFFFH
        CLC
        RET
REPLACE_ALL_RESTORE:
        CALL    REPLACE_ALL_RESTORE_CURSOR
        CLC
        RET
REPLACE_ALL_FAIL:
        CALL    REPLACE_ALL_RESTORE_CURSOR
        STC
        RET
REPLACE_ALL ENDP

; Change All is about to replace the match at the cursor. The place it
; returns to (REPLACE_SAVED_CURSOR) moves with the text as in real DOS 5
; EDIT (QEMU captures, 2026-11-12, receipt edit-dos5-last-gaps-2026-11-12):
; a place before the match stays; one on the match's last character or
; after it moves by the difference in length; one on an earlier character
; of the match goes to the start of the match's line.
REPLACE_TRACK_CURSOR PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        CALL    CURSOR_GLOBAL           ; DX:AX = the match
        MOV     BX,REPLACE_SAVED_CURSOR
        MOV     CX,REPLACE_SAVED_CURSOR+2
        CMP     CX,DX
        JNE     SHORT REPLACE_TRACK_FROM
        CMP     BX,AX
REPLACE_TRACK_FROM:
        JB      SHORT REPLACE_TRACK_DONE        ; before the match
        PUSH    AX
        PUSH    DX
        ADD     AX,REPLACE_SEARCH_LENGTH
        ADC     DX,0
        SUB     AX,1                    ; its last character
        SBB     DX,0
        CMP     CX,DX
        JNE     SHORT REPLACE_TRACK_LAST
        CMP     BX,AX
REPLACE_TRACK_LAST:
        POP     DX
        POP     AX
        JB      SHORT REPLACE_TRACK_INSIDE
        ADD     BX,REPLACE_REPLACEMENT_LENGTH
        ADC     CX,0
        SUB     BX,REPLACE_SEARCH_LENGTH
        SBB     CX,0
        JMP     SHORT REPLACE_TRACK_STORE
REPLACE_TRACK_INSIDE:
        MOV     BX,CURSOR_OFFSET        ; the line's start
        CALL    LINE_START_OF
        MOV     CX,CURSOR_OFFSET
        SUB     CX,BX
        SUB     AX,CX
        SBB     DX,0
        MOV     BX,AX
        MOV     CX,DX
REPLACE_TRACK_STORE:
        MOV     REPLACE_SAVED_CURSOR,BX
        MOV     REPLACE_SAVED_CURSOR+2,CX
REPLACE_TRACK_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
REPLACE_TRACK_CURSOR ENDP

; After Change All: a cursor that stood past its line's end stands as far
; past it again (the line's changes before it counted in).
REPLACE_ALL_VIRTUAL PROC NEAR
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,0
        CMP     BYTE PTR REPLACE_WAS_VIRTUAL,0
        JE      SHORT REPLACE_ALL_VIRTUAL_DONE
        CALL    CALCULATE_POSITION
        MOV     AX,REPLACE_OLD_VIRTUAL
        ADD     AX,CURRENT_COLUMN
        SUB     AX,REPLACE_OLD_COLUMN
        CMP     AX,CURRENT_COLUMN
        JBE     SHORT REPLACE_ALL_VIRTUAL_DONE
        MOV     VIRTUAL_COLUMN,AX
        MOV     BYTE PTR VIRTUAL_COLUMN_VALID,1
REPLACE_ALL_VIRTUAL_DONE:
        RET
REPLACE_ALL_VIRTUAL ENDP

REPLACE_ALL_RESTORE_CURSOR PROC NEAR
        MOV     AX,REPLACE_SAVED_CURSOR
        MOV     DX,REPLACE_SAVED_CURSOR+2
        PUSH    AX
        PUSH    DX
        CALL    DOC_LENGTH
        POP     CX
        POP     BX
        CMP     CX,DX
        JNE     REPLACE_ALL_CURSOR_ORDERED
        CMP     BX,AX
REPLACE_ALL_CURSOR_ORDERED:
        JA      REPLACE_ALL_CURSOR_SET
        MOV     AX,BX
        MOV     DX,CX
REPLACE_ALL_CURSOR_SET:
        JMP     SET_CURSOR_GLOBAL
REPLACE_ALL_RESTORE_CURSOR ENDP

REPLACE_CURRENT PROC NEAR
        XOR     CX,CX
        MOV     SI,OFFSET SEARCH_PATTERN
REPLACE_COUNT_SEARCH:
        CMP     BYTE PTR [SI],0
        JE      REPLACE_COUNT_REPLACEMENT
        INC     SI
        INC     CX
        JMP     REPLACE_COUNT_SEARCH
REPLACE_COUNT_REPLACEMENT:
        XOR     DX,DX
        MOV     SI,OFFSET REPLACE_PATTERN
REPLACE_COUNT_NEXT:
        CMP     BYTE PTR [SI],0
        JE      REPLACE_CAPACITY
        INC     SI
        INC     DX
        JMP     REPLACE_COUNT_NEXT
REPLACE_CAPACITY:
        MOV     AX,DX
        SUB     AX,CX
        JBE     REPLACE_CAPACITY_OK
        PUSH    AX
        CALL    VIEW_ROOM
        POP     BX
        CMP     BX,AX
        JA      REPLACE_CURRENT_FAIL
REPLACE_CAPACITY_OK:                    ; the match out, the replacement in,
        MOV     BX,CURSOR_OFFSET        ; each in one move of the text after it
        CALL    DELETE_SPAN_AT
        MOV     CX,DX
        CALL    INSERT_BLANKS_AT
        PUSH    ES
        PUSH    SS
        POP     ES
        MOV     SI,OFFSET REPLACE_PATTERN
        MOV     DI,BUFFER_BASE
        ADD     DI,BX
        REP     MOVSB
        POP     ES
        ADD     CURSOR_OFFSET,DX
        MOV     BYTE PTR MODIFIED,1
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        CLC
        RET
REPLACE_CURRENT_FAIL:
        STC
        RET
REPLACE_CURRENT ENDP

; Copy and Cut without a selection do nothing, as in DOS 5. Carry: no
; memory for the copy ("Out of memory"); the selection stays.
COPY_BLOCK PROC NEAR
        CALL    BLOCK_SPANS
        JC      SHORT COPY_BLOCK_SELECTED
        RET
COPY_BLOCK_SELECTED:
        CALL    MARK_IS_OUTSIDE
        JNC     COPY_BLOCK_HERE
        JMP     COPY_BLOCK_GLOBAL
COPY_BLOCK_HERE:
        MOV     SI,BLOCK_MARK
        MOV     DI,CURSOR_OFFSET
        CMP     SI,DI
        JBE     COPY_BLOCK_ORDERED
        XCHG    SI,DI
COPY_BLOCK_ORDERED:
        MOV     CX,DI
        SUB     CX,SI
        MOV     AX,CX
        XOR     DX,DX
        CALL    CLIP_RESERVE
        JC      COPY_BLOCK_FAIL
        MOV     CLIP_LEN,CX
        MOV     CLIP_LEN+2,DX
        ADD     SI,BUFFER_BASE
        PUSH    ES
        CALL    CLIP_BASE
        CALL    ARENA_ES_DI
        CLD
        PUSH    DS
        PUSH    SS
        POP     DS
        REP     MOVSB
        POP     DS
        POP     ES
        CLC
        RET
COPY_BLOCK_FAIL:
        STC
        RET
COPY_BLOCK ENDP

; Carry: BLOCK_MARK and the cursor enclose text (a selection, or Ctrl+Y's
; line).
BLOCK_SPANS PROC NEAR
        PUSH    AX
        MOV     AX,BLOCK_MARK
        CMP     AX,0FFFFH
        JE      SHORT BLOCK_SPANS_NONE
        CMP     AX,CURSOR_OFFSET
        JE      SHORT BLOCK_SPANS_NONE
        POP     AX
        STC
        RET
BLOCK_SPANS_NONE:
        POP     AX
        CLC
        RET
BLOCK_SPANS ENDP

CUT_BLOCK PROC NEAR
        CALL    BLOCK_SPANS
        JNC     CUT_BLOCK_DONE
        CALL    MARK_IS_OUTSIDE         ; beyond the view: in the block
        JNC     CUT_BLOCK_HERE
        CALL    COPY_BLOCK_GLOBAL
        JC      CUT_BLOCK_FAIL
        CALL    CLEAR_BLOCK_GLOBAL
        JMP     SHORT CUT_BLOCK_DONE
CUT_BLOCK_HERE:
        CALL    COPY_BLOCK
        JC      CUT_BLOCK_FAIL
        CMP     WORD PTR CLIP_LEN,0
        JE      CUT_BLOCK_DONE
        MOV     DI,BLOCK_MARK
        MOV     AX,CURSOR_OFFSET
        CMP     DI,AX
        JBE     CUT_BLOCK_LOW_READY
        MOV     DI,AX
CUT_BLOCK_LOW_READY:
        MOV     CURSOR_OFFSET,DI
        MOV     BX,DI
        MOV     SI,BUFFER_BASE
        ADD     SI,BX
        ADD     SI,CLIP_LEN
        MOV     DI,BUFFER_BASE
        ADD     DI,BX
        MOV     CX,BUFFER_LENGTH
        SUB     CX,BX
        SUB     CX,CLIP_LEN
        CLD
        VIEW_SEGS
        REP     MOVSB
        CODE_SEGS
        MOV     AX,CLIP_LEN
        SUB     BUFFER_LENGTH,AX
        MOV     BYTE PTR MODIFIED,1
CUT_BLOCK_DONE:
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
        CLC
        RET
CUT_BLOCK_FAIL:
        STC
        RET
CUT_BLOCK ENDP

CLEAR_BLOCK PROC NEAR
        MOV     SI,BLOCK_MARK
        CMP     SI,0FFFFH
        JE      CLEAR_BLOCK_FAIL
        CMP     SI,CURSOR_OFFSET
        JE      CLEAR_BLOCK_FAIL
        CALL    MARK_IS_OUTSIDE         ; beyond the view: in the block
        JNC     CLEAR_BLOCK_HERE
        CALL    CLEAR_BLOCK_GLOBAL
        CLC
        RET
CLEAR_BLOCK_HERE:
        MOV     DI,CURSOR_OFFSET
        CMP     SI,DI
        JBE     SHORT CLEAR_BLOCK_ORDERED
        XCHG    SI,DI
CLEAR_BLOCK_ORDERED:
        MOV     CX,DI
        SUB     CX,SI
        JCXZ    CLEAR_BLOCK_FAIL
        MOV     CURSOR_OFFSET,SI
        MOV     BX,SI
        MOV     SI,BUFFER_BASE
        ADD     SI,BX
        ADD     SI,CX
        MOV     DI,BUFFER_BASE
        ADD     DI,BX
        MOV     DX,BUFFER_LENGTH
        SUB     DX,BX
        SUB     DX,CX
        PUSH    CX
        MOV     CX,DX
        CLD
        VIEW_SEGS
        REP     MOVSB
        CODE_SEGS
        POP     CX
        SUB     BUFFER_LENGTH,CX
        MOV     BYTE PTR MODIFIED,1
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
        CLC
        RET
CLEAR_BLOCK_FAIL:
        STC
        RET
CLEAR_BLOCK ENDP

PASTE_BLOCK PROC NEAR
        MOV     AX,CLIP_LEN
        OR      AX,CLIP_LEN+2
        JZ      PASTE_BLOCK_DONE
        CMP     WORD PTR CLIP_LEN+2,0
        JNE     PASTE_BLOCK_WIDE
        CALL    VIEW_ROOM
        CMP     AX,CLIP_LEN
        JAE     PASTE_BLOCK_HERE
PASTE_BLOCK_WIDE:                       ; more than the view takes: in the block
        JMP     PASTE_BLOCK_GLOBAL
PASTE_BLOCK_HERE:
        MOV     BX,CURSOR_OFFSET
        MOV     CX,BUFFER_LENGTH
        SUB     CX,BX
        JCXZ    PASTE_BLOCK_COPY
        MOV     SI,BUFFER_BASE
        ADD     SI,BUFFER_LENGTH
        DEC     SI
        MOV     DI,SI
        ADD     DI,CLIP_LEN
        VIEW_SEGS
        STD
        REP     MOVSB
        CODE_SEGS
PASTE_BLOCK_COPY:
        CLD
        CALL    CLIP_BASE
        CALL    ARENA_ADDRESS
        MOV     SI,DX
        MOV     BX,AX
        PUSH    ES
        PUSH    SS
        POP     ES
        MOV     DI,BUFFER_BASE
        ADD     DI,CURSOR_OFFSET
        MOV     CX,CLIP_LEN
        PUSH    DS
        MOV     DS,BX
        REP     MOVSB
        POP     DS
        POP     ES
        MOV     AX,CLIP_LEN
        ADD     BUFFER_LENGTH,AX
        MOV     BYTE PTR MODIFIED,1
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        MOV     BYTE PTR SHIFT_SELECTION,0
PASTE_BLOCK_DONE:
        CLC
        RET
PASTE_BLOCK_FAIL:
        CLD
        STC
        RET
PASTE_BLOCK ENDP

; Menus are a modal keyboard focus scope.  AL selects the initial menu and the
; returned command is zero when Escape closes the scope without an action.
SHOW_MENU_SYSTEM PROC NEAR
        MOV     BYTE PTR DRAWN_VALID,0
        CALL    SET_ACTIVE_MENU
SHOW_MENU_REDRAW:
        CALL    DRAW_SCREEN
        CALL    DOSUI_HIDE_CURSOR
        CALL    DRAW_ACTIVE_HEADER
        CMP     BYTE PTR MENU_BAR_ONLY,0
        JE      SHORT SHOW_MENU_DROPPED
        CALL    DRAW_MENU_BAR_SELECTION
        JMP     SHORT SHOW_MENU_KEY
SHOW_MENU_DROPPED:
        CALL    DRAW_ACTIVE_MENU
        CALL    DRAW_MENU_STATUS
SHOW_MENU_KEY:
        CALL    DOSUI_READ_EVENT
        JNC     SHORT SHOW_MENU_HAVE_KEY
        CMP     BYTE PTR MENU_BAR_ONLY,0
        JE      SHORT SHOW_MENU_DROPPED_MOUSE
        MOV     BYTE PTR MENU_BAR_ONLY,0  ; selected bar: a header click drops
        OR      DH,DH                     ; that menu, any other click leaves
        JNZ     SHORT SHOW_MENU_BAR_MOUSE_LEAVE
        CALL    MENU_FROM_BAR_COLUMN
        CMP     AL,0FFH
        JE      SHORT SHOW_MENU_BAR_MOUSE_LEAVE
        CALL    SET_ACTIVE_MENU
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_BAR_MOUSE_LEAVE:
        XOR     AL,AL
        JMP     SHOW_MENU_DONE
SHOW_MENU_DROPPED_MOUSE:
        JMP     SHOW_MENU_MOUSE
SHOW_MENU_HAVE_KEY:
        CMP     BYTE PTR MENU_BAR_ONLY,0
        JE      SHORT SHOW_MENU_DROPPED_KEY
        CALL    MENU_BAR_KEY
        CMP     AL,0FFH
        JNE     SHORT SHOW_MENU_BAR_HANDLED
        JMP     SHOW_MENU_KEY
SHOW_MENU_BAR_HANDLED:
        CMP     AL,0FEH
        JNE     SHORT SHOW_MENU_BAR_DONE
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_BAR_DONE:
        JMP     SHOW_MENU_DONE
SHOW_MENU_DROPPED_KEY:
        OR      AL,AL
        JZ      SHORT SHOW_MENU_EXTENDED
        CMP     AL,27
        JNE     SHORT SHOW_MENU_NOT_CANCEL
        XOR     AL,AL
        JMP     SHOW_MENU_DONE
SHOW_MENU_NOT_CANCEL:
        CMP     AL,13
        JNE     SHORT SHOW_MENU_NOT_ENTER
        CALL    MENU_COMMAND_FROM_SELECTION
        OR      AL,AL
        JZ      SHOW_MENU_KEY
        JMP     SHOW_MENU_DONE
SHOW_MENU_NOT_ENTER:
        CMP     AL,9                    ; Tab advances across menu headers.
        JNE     SHORT SHOW_MENU_NOT_TAB
        CALL    MENU_SELECT_NEXT_HEADER
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_TAB:
        CALL    MENU_ACCELERATOR
        OR      AL,AL
        JZ      SHOW_MENU_KEY
        JMP     SHOW_MENU_DONE

SHOW_MENU_EXTENDED:
        CMP     AH,0FH                  ; Some BIOS paths report Tab as scan-only.
        JNE     SHORT SHOW_MENU_NOT_EXTENDED_TAB
        CALL    MENU_SELECT_NEXT_HEADER
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_EXTENDED_TAB:
        CMP     AH,48H                  ; Up
        JNE     SHORT SHOW_MENU_NOT_UP
        CALL    MENU_SELECT_PREVIOUS
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_UP:
        CMP     AH,50H                  ; Down
        JNE     SHORT SHOW_MENU_NOT_DOWN
        CALL    MENU_SELECT_NEXT
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_DOWN:
        CMP     AH,4BH                  ; Left menu
        JNE     SHORT SHOW_MENU_NOT_LEFT
        CALL    MENU_SELECT_PREVIOUS_HEADER
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_LEFT:
        CMP     AH,4DH                  ; Right menu
        JNE     SHORT SHOW_MENU_NOT_RIGHT
        CALL    MENU_SELECT_NEXT_HEADER
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_RIGHT:
        CMP     AH,47H                  ; Home
        JNE     SHORT SHOW_MENU_NOT_HOME
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0FFH
        CALL    MENU_SELECT_NEXT
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_HOME:
        CMP     AH,4FH                  ; End
        JNE     SHORT SHOW_MENU_NOT_END
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        CALL    MENU_SELECT_PREVIOUS
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_END:                      ; Alt+F, E, S, O, H do not switch
                                        ; menus once one is dropped (DOS 5);
                                        ; like any Alt+letter they choose
                                        ; the dropped menu's item
SHOW_MENU_NOT_ALT_H:
        CMP     AH,38H                  ; Alt closes an active menu scope
        JNE     SHORT SHOW_MENU_NOT_ALT
        XOR     AL,AL
        JMP     SHOW_MENU_DONE
SHOW_MENU_NOT_ALT:
        CMP     AH,3BH                  ; F1: help on menus, then the menu
        JNE     SHORT SHOW_MENU_NOT_F1
        MOV     AL,HELP_TOPIC_MENUS
        CALL    DIALOG_HELP
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_NOT_F1:
        CALL    ALT_LETTER              ; Alt+letter: that item, as in DOS 5
        JC      SHORT SHOW_MENU_NOT_ALT_LETTER
        CALL    MENU_ACCELERATOR
        OR      AL,AL
        JNZ     SHORT SHOW_MENU_ALT_LETTER_DONE
        JMP     SHOW_MENU_KEY
SHOW_MENU_ALT_LETTER_DONE:
        JMP     SHOW_MENU_DONE
SHOW_MENU_NOT_ALT_LETTER:
        CMP     AH,3DH                  ; F3
        JNE     SHORT SHOW_MENU_NOT_F3
        JMP     SHORT SHOW_MENU_F3
SHOW_MENU_NOT_F3:
        JMP     SHOW_MENU_KEY
SHOW_MENU_F3:
        MOV     AL,COMMAND_FIND_NEXT
        JMP     SHORT SHOW_MENU_DONE
SHOW_MENU_CLOSE:
        XOR     AL,AL
SHOW_MENU_DONE:
        MOV     BYTE PTR MENU_BAR_ONLY,0
        CALL    DOSUI_SHOW_CURSOR
        RET

; A click while a menu is open: another header switches to its menu, the
; open header or any cell outside the drop-down closes the scope, an item
; row selects and executes exactly as Enter would, and the status-bar
; F1=Help shortcut answers like F1.
SHOW_MENU_MOUSE:
        OR      DH,DH
        JNZ     SHORT SHOW_MENU_MOUSE_NOT_BAR
        CALL    MENU_FROM_BAR_COLUMN
        CMP     AL,0FFH
        JNE     SHORT SHOW_MENU_MOUSE_HEADER
        JMP     SHOW_MENU_KEY
SHOW_MENU_MOUSE_HEADER:
        CMP     AL,MENU_ACTIVE
        JNE     SHORT SHOW_MENU_MOUSE_SWITCH
        XOR     AL,AL
        JMP     SHOW_MENU_DONE
SHOW_MENU_MOUSE_SWITCH:
        CALL    SET_ACTIVE_MENU
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_MOUSE_NOT_BAR:
        CMP     DH,STATUS_HIT_ROW
        JNE     SHORT SHOW_MENU_MOUSE_NOT_STATUS
        CMP     DL,8
        JA      SHORT SHOW_MENU_MOUSE_AGAIN
        MOV     AL,COMMAND_GETTING_STARTED
        JMP     SHOW_MENU_DONE
SHOW_MENU_MOUSE_AGAIN:
        JMP     SHOW_MENU_KEY
SHOW_MENU_MOUSE_NOT_STATUS:
        CALL    MENU_ITEM_FROM_CELL
        JC      SHORT SHOW_MENU_MOUSE_OUTSIDE
        CMP     AL,0FFH                 ; separator row
        JE      SHORT SHOW_MENU_MOUSE_AGAIN
        MOV     BYTE PTR DOSUI_MENU_SELECTED,AL
        CALL    MENU_COMMAND_FROM_SELECTION
        OR      AL,AL
        JNZ     SHORT SHOW_MENU_MOUSE_COMMAND
        JMP     SHOW_MENU_REDRAW
SHOW_MENU_MOUSE_COMMAND:
        JMP     SHOW_MENU_DONE
SHOW_MENU_MOUSE_OUTSIDE:
        XOR     AL,AL
        JMP     SHOW_MENU_DONE
SHOW_MENU_SYSTEM ENDP

; A key while the menu bar is selected after a lone Alt tap, before any menu
; drops down. AL returns FFh to ignore the key, FEh to redraw, or else the
; command (zero to leave the menus).
MENU_BAR_KEY PROC NEAR
        OR      AL,AL
        JZ      SHORT MENU_BAR_EXTENDED
        CMP     AL,27
        JNE     SHORT MENU_BAR_NOT_ESCAPE
        XOR     AL,AL
        RET
MENU_BAR_NOT_ESCAPE:
        CMP     AL,13
        JNE     SHORT MENU_BAR_NOT_ENTER
        MOV     BYTE PTR MENU_BAR_ONLY,0
        MOV     AL,0FEH
        RET
MENU_BAR_NOT_ENTER:
        CMP     AL,'a'
        JB      SHORT MENU_BAR_LETTER
        CMP     AL,'z'
        JA      SHORT MENU_BAR_LETTER
        SUB     AL,20H
MENU_BAR_LETTER:
        MOV     AH,MENU_FILE
        CMP     AL,'F'
        JE      SHORT MENU_BAR_OPEN
        MOV     AH,MENU_EDIT
        CMP     AL,'E'
        JE      SHORT MENU_BAR_OPEN
        MOV     AH,MENU_SEARCH
        CMP     AL,'S'
        JE      SHORT MENU_BAR_OPEN
        MOV     AH,MENU_OPTIONS
        CMP     AL,'O'
        JE      SHORT MENU_BAR_OPEN
        MOV     AH,MENU_HELP
        CMP     AL,'H'
        JE      SHORT MENU_BAR_OPEN
MENU_BAR_IGNORE:
        MOV     AL,0FFH
        RET
MENU_BAR_EXTENDED:
        CMP     AH,38H                  ; Alt again leaves the bar
        JE      SHORT MENU_BAR_LEAVE
        CMP     AH,4BH
        JE      SHORT MENU_BAR_PREVIOUS
        CMP     AH,4DH
        JE      SHORT MENU_BAR_NEXT
        CMP     AH,48H                  ; Up and Down drop the menu
        JE      SHORT MENU_BAR_DROP
        CMP     AH,50H
        JE      SHORT MENU_BAR_DROP
        CMP     AH,3BH                  ; F1
        JNE     SHORT MENU_BAR_ALT_LETTER
        MOV     AL,COMMAND_GETTING_STARTED
        RET
MENU_BAR_ALT_LETTER:
        MOV     AL,MENU_FILE
        CMP     AH,21H                  ; Alt+F/E/S/O/H open directly
        JE      SHORT MENU_BAR_OPEN_AL
        MOV     AL,MENU_EDIT
        CMP     AH,12H
        JE      SHORT MENU_BAR_OPEN_AL
        MOV     AL,MENU_SEARCH
        CMP     AH,1FH
        JE      SHORT MENU_BAR_OPEN_AL
        MOV     AL,MENU_OPTIONS
        CMP     AH,18H
        JE      SHORT MENU_BAR_OPEN_AL
        MOV     AL,MENU_HELP
        CMP     AH,23H
        JNE     SHORT MENU_BAR_IGNORE
MENU_BAR_OPEN_AL:
        MOV     AH,AL
MENU_BAR_OPEN:
        MOV     AL,AH
        CALL    SET_ACTIVE_MENU
MENU_BAR_DROP:
        MOV     BYTE PTR MENU_BAR_ONLY,0
        MOV     AL,0FEH
        RET
MENU_BAR_PREVIOUS:
        CALL    MENU_SELECT_PREVIOUS_HEADER
        MOV     AL,0FEH
        RET
MENU_BAR_NEXT:
        CALL    MENU_SELECT_NEXT_HEADER
        MOV     AL,0FEH
        RET
MENU_BAR_LEAVE:
        XOR     AL,AL
        RET
MENU_BAR_KEY ENDP

; The selected-bar look of DOS 5 EDIT: every header shows its access letter
; (bright on the bar, bright white in the selected header) and the status
; line explains the keys. The cursor stays hidden.
DRAW_MENU_BAR_SELECTION PROC NEAR
        XOR     BX,BX
DRAW_MENU_BAR_NEXT:
        MOV     DL,MENU_BAR_LETTER_COLUMNS[BX]
        MOV     SI,DX
        AND     SI,00FFH
        MOV     AL,MENU_TEXT[SI]
        MOV     AH,7FH
        CMP     BL,MENU_ACTIVE
        JNE     SHORT DRAW_MENU_BAR_ATTR
        MOV     AH,0FH
DRAW_MENU_BAR_ATTR:
        PUSH    BX
        MOV     BL,AH
        MOV     DH,0
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     BX
        INC     BX
        CMP     BX,MENU_HELP
        JBE     DRAW_MENU_BAR_NEXT
        MOV     AL,' '
        MOV     BL,3FH
        MOV     DH,STATUS_ROW
        XOR     DL,DL
        MOV     CX,62
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET MENU_BAR_STATUS_TEXT
        CALL    DOSUI_WRITE_Z
        RET
DRAW_MENU_BAR_SELECTION ENDP

SET_ACTIVE_MENU PROC NEAR
        MOV     MENU_ACTIVE,AL
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0FFH
        CALL    MENU_SELECT_NEXT
        RET
SET_ACTIVE_MENU ENDP

; Return a bit mask for the commands that are meaningful in the current state.
GET_ACTIVE_MENU_MASK PROC NEAR
        PUSH    BX
        CMP     BYTE PTR MENU_ACTIVE,MENU_FILE
        JNE     SHORT GET_MASK_NOT_FILE
        MOV     AL,33H                  ; New/Open/Print/Exit
        CMP     BYTE PTR SAVE_DISABLED,0
        JNE     SHORT GET_MASK_DONE
        OR      AL,0CH                  ; Save and Save As
        JMP     SHORT GET_MASK_DONE
GET_MASK_NOT_FILE:
        CMP     BYTE PTR MENU_ACTIVE,MENU_EDIT
        JNE     SHORT GET_MASK_NOT_EDIT
        XOR     AL,AL
        CMP     WORD PTR BLOCK_MARK,0FFFFH
        JE      SHORT GET_MASK_EDIT_PASTE
        MOV     BX,BLOCK_MARK
        CMP     BX,CURSOR_OFFSET
        JE      SHORT GET_MASK_EDIT_PASTE
        OR      AL,0BH                  ; Cut, Copy, and Clear
GET_MASK_EDIT_PASTE:
        MOV     BX,CLIP_LEN
        OR      BX,CLIP_LEN+2
        JZ      SHORT GET_MASK_DONE
        OR      AL,04H                  ; Paste
        JMP     SHORT GET_MASK_DONE
GET_MASK_NOT_EDIT:
        CMP     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        JNE     SHORT GET_MASK_NOT_SEARCH
        MOV     AL,07H                  ; Find, Repeat Last Find, and Change
        JMP     SHORT GET_MASK_DONE
GET_MASK_NOT_SEARCH:
        CMP     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        JNE     SHORT GET_MASK_HELP
        MOV     AL,03H
        JMP     SHORT GET_MASK_DONE
GET_MASK_HELP:
        MOV     AL,07H
GET_MASK_DONE:
        POP     BX
        RET
GET_ACTIVE_MENU_MASK ENDP

MENU_SELECT_NEXT PROC NEAR
        PUSH    AX
        CMP     BYTE PTR MENU_ACTIVE,MENU_FILE
        JNE     SHORT MENU_NEXT_NOT_FILE
        INC     BYTE PTR DOSUI_MENU_SELECTED
        CMP     BYTE PTR DOSUI_MENU_SELECTED,6
        JB      SHORT MENU_NEXT_DONE
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        JMP     MENU_NEXT_DONE
MENU_NEXT_NOT_FILE:
        CMP     BYTE PTR MENU_ACTIVE,MENU_EDIT
        JNE     SHORT MENU_NEXT_NOT_EDIT
        INC     BYTE PTR DOSUI_MENU_SELECTED
        CMP     BYTE PTR DOSUI_MENU_SELECTED,4
        JB      SHORT MENU_NEXT_DONE
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        JMP     MENU_NEXT_DONE
MENU_NEXT_NOT_EDIT:
        CMP     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        JNE     SHORT MENU_NEXT_NOT_SEARCH
        INC     BYTE PTR DOSUI_MENU_SELECTED
        CMP     BYTE PTR DOSUI_MENU_SELECTED,3
        JB      SHORT MENU_NEXT_DONE
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        JMP     MENU_NEXT_DONE
MENU_NEXT_NOT_SEARCH:
        CMP     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        JNE     SHORT MENU_NEXT_HELP
        XOR     BYTE PTR DOSUI_MENU_SELECTED,1
        AND     BYTE PTR DOSUI_MENU_SELECTED,1
        JMP     MENU_NEXT_DONE
MENU_NEXT_HELP:
        INC     BYTE PTR DOSUI_MENU_SELECTED
        CMP     BYTE PTR DOSUI_MENU_SELECTED,3
        JB      SHORT MENU_NEXT_DONE
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
MENU_NEXT_DONE:
        POP     AX
        RET
MENU_SELECT_NEXT ENDP

MENU_SELECT_PREVIOUS PROC NEAR
        PUSH    AX
        CMP     BYTE PTR MENU_ACTIVE,MENU_FILE
        JNE     SHORT MENU_PREVIOUS_NOT_FILE
        CMP     BYTE PTR DOSUI_MENU_SELECTED,0
        JNE     SHORT MENU_PREVIOUS_FILE_DECREMENT
        MOV     BYTE PTR DOSUI_MENU_SELECTED,5
        JMP     MENU_PREVIOUS_DONE
MENU_PREVIOUS_FILE_DECREMENT:
        DEC     BYTE PTR DOSUI_MENU_SELECTED
        JMP     MENU_PREVIOUS_DONE
MENU_PREVIOUS_NOT_FILE:
        CMP     BYTE PTR MENU_ACTIVE,MENU_EDIT
        JNE     SHORT MENU_PREVIOUS_NOT_EDIT
        CMP     BYTE PTR DOSUI_MENU_SELECTED,0
        JNE     SHORT MENU_PREVIOUS_EDIT_DECREMENT
        MOV     BYTE PTR DOSUI_MENU_SELECTED,3
        JMP     SHORT MENU_PREVIOUS_DONE
MENU_PREVIOUS_EDIT_DECREMENT:
        DEC     BYTE PTR DOSUI_MENU_SELECTED
        JMP     SHORT MENU_PREVIOUS_DONE
MENU_PREVIOUS_NOT_EDIT:
        CMP     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        JNE     SHORT MENU_PREVIOUS_NOT_SEARCH
        CMP     BYTE PTR DOSUI_MENU_SELECTED,0
        JNE     SHORT MENU_PREVIOUS_SEARCH_DECREMENT
        MOV     BYTE PTR DOSUI_MENU_SELECTED,2
        JMP     SHORT MENU_PREVIOUS_DONE
MENU_PREVIOUS_SEARCH_DECREMENT:
        DEC     BYTE PTR DOSUI_MENU_SELECTED
        JMP     SHORT MENU_PREVIOUS_DONE
MENU_PREVIOUS_NOT_SEARCH:
        CMP     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        JNE     SHORT MENU_PREVIOUS_HELP
        XOR     BYTE PTR DOSUI_MENU_SELECTED,1
        AND     BYTE PTR DOSUI_MENU_SELECTED,1
        JMP     SHORT MENU_PREVIOUS_DONE
MENU_PREVIOUS_HELP:
        CMP     BYTE PTR DOSUI_MENU_SELECTED,0
        JNE     SHORT MENU_PREVIOUS_HELP_DECREMENT
        MOV     BYTE PTR DOSUI_MENU_SELECTED,3
MENU_PREVIOUS_HELP_DECREMENT:
        DEC     BYTE PTR DOSUI_MENU_SELECTED
MENU_PREVIOUS_DONE:
        POP     AX
        RET
MENU_SELECT_PREVIOUS ENDP

MENU_SELECT_NEXT_HEADER PROC NEAR
        MOV     AL,MENU_ACTIVE
        INC     AL
        CMP     AL,MENU_HELP
        JBE     SHORT MENU_NEXT_HEADER_READY
        MOV     AL,MENU_FILE
MENU_NEXT_HEADER_READY:
        CALL    SET_ACTIVE_MENU
        RET
MENU_SELECT_NEXT_HEADER ENDP

MENU_SELECT_PREVIOUS_HEADER PROC NEAR
        MOV     AL,MENU_ACTIVE
        OR      AL,AL
        JNZ     SHORT MENU_PREVIOUS_HEADER_DECREMENT
        MOV     AL,MENU_HELP+1
MENU_PREVIOUS_HEADER_DECREMENT:
        DEC     AL
        CALL    SET_ACTIVE_MENU
        RET
MENU_SELECT_PREVIOUS_HEADER ENDP

MENU_COMMAND_FROM_SELECTION PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        CALL    GET_ACTIVE_MENU_MASK
        MOV     AH,1
        MOV     CL,DOSUI_MENU_SELECTED
        SHL     AH,CL
        TEST    AL,AH
        JZ      SHORT MENU_COMMAND_DISABLED
        XOR     AX,AX
        MOV     AL,MENU_ACTIVE
        MOV     DL,6
        MUL     DL
        MOV     BX,AX
        XOR     AX,AX
        MOV     AL,DOSUI_MENU_SELECTED
        ADD     BX,AX
        MOV     AL,MENU_COMMANDS[BX]
        JMP     SHORT MENU_COMMAND_DONE
MENU_COMMAND_DISABLED:
        XOR     AL,AL
MENU_COMMAND_DONE:
        POP     DX
        POP     CX
        POP     BX
        RET
MENU_COMMAND_FROM_SELECTION ENDP

; Map a menu-bar cell column in DL to a menu index in AL, or FFh outside
; every header.  The spans cover the padded header cells that
; DRAW_ACTIVE_HEADER highlights, matching MENU_TEXT's layout.
MENU_FROM_BAR_COLUMN PROC NEAR
        MOV     AL,MENU_FILE
        CMP     DL,2
        JB      SHORT MENU_FROM_BAR_NONE
        CMP     DL,7
        JBE     SHORT MENU_FROM_BAR_DONE
        MOV     AL,MENU_EDIT
        CMP     DL,13
        JBE     SHORT MENU_FROM_BAR_DONE
        MOV     AL,MENU_SEARCH
        CMP     DL,21
        JBE     SHORT MENU_FROM_BAR_DONE
        MOV     AL,MENU_OPTIONS
        CMP     DL,30
        JBE     SHORT MENU_FROM_BAR_DONE
        MOV     AL,MENU_HELP
        CMP     DL,73
        JB      SHORT MENU_FROM_BAR_NONE
        CMP     DL,78
        JBE     SHORT MENU_FROM_BAR_DONE
MENU_FROM_BAR_NONE:
        MOV     AL,0FFH
MENU_FROM_BAR_DONE:
        RET
MENU_FROM_BAR_COLUMN ENDP

; Map a click cell (DH row, DL column) to an item index of the open
; drop-down.  CF is set outside the frame interior; AL is FFh on a
; separator row.  The frame descriptors and row tables mirror the
; DRAW_*_MENU geometry.
MENU_ITEM_FROM_CELL PROC NEAR
        PUSH    BX
        PUSH    SI
        XOR     BX,BX
        MOV     BL,MENU_ACTIVE
        SHL     BX,1
        MOV     SI,MENU_HIT_RECTS[BX]
        MOV     AL,[SI+DOSUI_RECT_TOP]
        INC     AL
        CMP     DH,AL
        JB      SHORT MENU_ITEM_FROM_CELL_OUTSIDE
        MOV     AL,[SI+DOSUI_RECT_BOTTOM]
        DEC     AL
        CMP     DH,AL
        JA      SHORT MENU_ITEM_FROM_CELL_OUTSIDE
        MOV     AL,[SI+DOSUI_RECT_LEFT]
        INC     AL
        CMP     DL,AL
        JB      SHORT MENU_ITEM_FROM_CELL_OUTSIDE
        MOV     AL,[SI+DOSUI_RECT_RIGHT]
        DEC     AL
        CMP     DL,AL
        JA      SHORT MENU_ITEM_FROM_CELL_OUTSIDE
        MOV     SI,MENU_HIT_ROWS[BX]
        MOV     BL,DH
        SUB     BL,2
        XOR     BH,BH
        MOV     AL,[SI+BX]
        CLC
        JMP     SHORT MENU_ITEM_FROM_CELL_DONE
MENU_ITEM_FROM_CELL_OUTSIDE:
        STC
MENU_ITEM_FROM_CELL_DONE:
        POP     SI
        POP     BX
        RET
MENU_ITEM_FROM_CELL ENDP

; Classify a click on the DOS 5 file-confirmation frame.  AL: 1 Yes, 2 No,
; 3 Cancel, 4 Help, 0 outside every control.  The status-bar F1=Help and
; Esc=Cancel shortcuts answer like their buttons.
FILE_CONFIRM_HIT PROC NEAR
        CMP     DH,14
        JNE     SHORT FILE_CONFIRM_HIT_STATUS
        MOV     AL,1
        CMP     DL,20
        JB      SHORT FILE_CONFIRM_HIT_NONE
        CMP     DL,26
        JBE     SHORT FILE_CONFIRM_HIT_DONE
        MOV     AL,2
        CMP     DL,30
        JB      SHORT FILE_CONFIRM_HIT_NONE
        CMP     DL,37
        JBE     SHORT FILE_CONFIRM_HIT_DONE
        MOV     AL,3
        CMP     DL,41
        JB      SHORT FILE_CONFIRM_HIT_NONE
        CMP     DL,48
        JBE     SHORT FILE_CONFIRM_HIT_DONE
        MOV     AL,4
        CMP     DL,52
        JB      SHORT FILE_CONFIRM_HIT_NONE
        CMP     DL,59
        JBE     SHORT FILE_CONFIRM_HIT_DONE
FILE_CONFIRM_HIT_NONE:
        XOR     AL,AL
FILE_CONFIRM_HIT_DONE:
        RET
FILE_CONFIRM_HIT_STATUS:
        CMP     DH,STATUS_HIT_ROW
        JNE     SHORT FILE_CONFIRM_HIT_NONE
        MOV     AL,4
        CMP     DL,1
        JB      SHORT FILE_CONFIRM_HIT_NONE
        CMP     DL,7
        JBE     SHORT FILE_CONFIRM_HIT_DONE
        MOV     AL,3
        CMP     DL,27
        JB      SHORT FILE_CONFIRM_HIT_NONE
        CMP     DL,36
        JBE     SHORT FILE_CONFIRM_HIT_DONE
        JMP     SHORT FILE_CONFIRM_HIT_NONE
FILE_CONFIRM_HIT ENDP

; An item accelerator executes immediately, as in the DOS 5 menu contract.
MENU_ACCELERATOR PROC NEAR
        PUSH    AX
        MOV     AL,DOSUI_MENU_SELECTED  ; kept when no item has the letter
        MOV     MENU_ACCELERATOR_SELECTED,AL
        POP     AX
        CMP     AL,'a'
        JB      SHORT MENU_ACCELERATOR_UPPER
        CMP     AL,'z'
        JA      SHORT MENU_ACCELERATOR_UPPER
        SUB     AL,20H
MENU_ACCELERATOR_UPPER:
        CMP     BYTE PTR MENU_ACTIVE,MENU_FILE
        JNE     SHORT MENU_ACCELERATOR_NOT_FILE
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        CMP     AL,'N'
        JE      SHORT MENU_ACCELERATOR_FILE_GO
        MOV     BYTE PTR DOSUI_MENU_SELECTED,1
        CMP     AL,'O'
        JE      SHORT MENU_ACCELERATOR_FILE_GO
        MOV     BYTE PTR DOSUI_MENU_SELECTED,4
        CMP     AL,'P'
        JNE     SHORT MENU_ACCELERATOR_FILE_OTHER
MENU_ACCELERATOR_FILE_GO:
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_FILE_OTHER:
        CMP     AL,'S'
        JE      SHORT MENU_ACCELERATOR_FILE_SAVE
        CMP     AL,'A'
        JE      SHORT MENU_ACCELERATOR_FILE_SAVE_AS
        CMP     AL,'X'
        JE      SHORT MENU_ACCELERATOR_FILE_EXIT
        JMP     MENU_ACCELERATOR_NONE
MENU_ACCELERATOR_FILE_EXIT:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,5
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_FILE_SAVE:
        CMP     BYTE PTR SAVE_DISABLED,0
        JE      SHORT MENU_ACCELERATOR_FILE_SAVE_ALLOWED
        ; DOS 5 still dispatches the Save accelerator for a read-only file so
        ; the attempted write can report its Path/File access error dialog.
        MOV     AL,COMMAND_SAVE
        RET
MENU_ACCELERATOR_FILE_SAVE_AS:
        CMP     BYTE PTR SAVE_DISABLED,0
        JE      SHORT MENU_ACCELERATOR_FILE_SAVE_AS_ALLOWED
        JMP     MENU_ACCELERATOR_NONE
MENU_ACCELERATOR_FILE_SAVE_AS_ALLOWED:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,3
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_FILE_SAVE_ALLOWED:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,2
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_NOT_FILE:
        CMP     BYTE PTR MENU_ACTIVE,MENU_EDIT
        JNE     SHORT MENU_ACCELERATOR_NOT_EDIT
        CMP     AL,'C'
        JE      SHORT MENU_ACCELERATOR_EDIT_COPY
        CMP     AL,'T'
        JE      SHORT MENU_ACCELERATOR_EDIT_CUT
        CMP     AL,'P'
        JE      SHORT MENU_ACCELERATOR_EDIT_PASTE
        CMP     AL,'L'
        JE      SHORT MENU_ACCELERATOR_EDIT_CLEAR
        JMP     MENU_ACCELERATOR_NONE
MENU_ACCELERATOR_EDIT_CLEAR:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,3
        JMP     MENU_ACCELERATOR_IF_ENABLED
MENU_ACCELERATOR_EDIT_PASTE:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,2
        JMP     MENU_ACCELERATOR_IF_ENABLED
MENU_ACCELERATOR_EDIT_CUT:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        JMP     MENU_ACCELERATOR_IF_ENABLED
MENU_ACCELERATOR_EDIT_COPY:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,1
        JMP     MENU_ACCELERATOR_IF_ENABLED
MENU_ACCELERATOR_NOT_EDIT:
        CMP     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        JNE     SHORT MENU_ACCELERATOR_NOT_SEARCH
        CMP     AL,'F'
        JE      SHORT MENU_ACCELERATOR_SEARCH_FIND
        CMP     AL,'R'
        JE      SHORT MENU_ACCELERATOR_SEARCH_NEXT
        CMP     AL,'C'
        JNE     MENU_ACCELERATOR_NONE
MENU_ACCELERATOR_SEARCH_CHANGE:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,2
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_SEARCH_NEXT:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,1
        JMP     SHORT MENU_ACCELERATOR_IF_ENABLED
MENU_ACCELERATOR_SEARCH_FIND:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_NOT_SEARCH:
        CMP     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        JNE     SHORT MENU_ACCELERATOR_HELP
        CMP     AL,'D'
        JE      SHORT MENU_ACCELERATOR_OPTIONS_DISPLAY
        CMP     AL,'P'
        JNE     MENU_ACCELERATOR_NONE
        MOV     BYTE PTR DOSUI_MENU_SELECTED,1
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_OPTIONS_DISPLAY:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        JMP     MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_HELP:
        CMP     AL,'G'
        JE      SHORT MENU_ACCELERATOR_HELP_START
        CMP     AL,'K'
        JE      SHORT MENU_ACCELERATOR_HELP_KEYBOARD
        CMP     AL,'A'
        JNE     SHORT MENU_ACCELERATOR_NONE
        MOV     BYTE PTR DOSUI_MENU_SELECTED,2
        JMP     SHORT MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_HELP_KEYBOARD:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,1
        JMP     SHORT MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_HELP_START:
        MOV     BYTE PTR DOSUI_MENU_SELECTED,0
        JMP     SHORT MENU_ACCELERATOR_COMMAND
MENU_ACCELERATOR_IF_ENABLED:
        CALL    GET_ACTIVE_MENU_MASK
        MOV     AH,1
        PUSH    CX
        MOV     CL,DOSUI_MENU_SELECTED
        SHL     AH,CL
        TEST    AL,AH
        POP     CX
        JZ      SHORT MENU_ACCELERATOR_NONE
MENU_ACCELERATOR_COMMAND:
        CALL    MENU_COMMAND_FROM_SELECTION
        RET
MENU_ACCELERATOR_NONE:
        MOV     AL,MENU_ACCELERATOR_SELECTED
        MOV     DOSUI_MENU_SELECTED,AL
        XOR     AL,AL
        RET
MENU_ACCELERATOR ENDP

; AH = an Alt+letter scan code: AL = its letter (carry clear); carry set
; for any other key.
ALT_LETTER PROC NEAR
        CMP     AH,10H
        JB      SHORT ALT_LETTER_NONE
        CMP     AH,32H
        JA      SHORT ALT_LETTER_NONE
        PUSH    BX
        MOV     BL,AH
        XOR     BH,BH
        MOV     AL,ALT_LETTERS[BX-10H]
        POP     BX
        OR      AL,AL
        JZ      SHORT ALT_LETTER_NONE
        CLC
        RET
ALT_LETTER_NONE:
        STC
        RET
ALT_LETTER ENDP

DRAW_ACTIVE_HEADER PROC NEAR
        CMP     BYTE PTR MENU_ACTIVE,MENU_FILE
        JNE     SHORT DRAW_HEADER_NOT_FILE
        MOV     SI,OFFSET FILE_HEADER_TEXT
        MOV     DL,2
        JMP     SHORT DRAW_HEADER_READY
DRAW_HEADER_NOT_FILE:
        CMP     BYTE PTR MENU_ACTIVE,MENU_EDIT
        JNE     SHORT DRAW_HEADER_NOT_EDIT
        MOV     SI,OFFSET EDIT_HEADER_TEXT
        MOV     DL,8
        JMP     SHORT DRAW_HEADER_READY
DRAW_HEADER_NOT_EDIT:
        CMP     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        JNE     SHORT DRAW_HEADER_NOT_SEARCH
        MOV     SI,OFFSET SEARCH_HEADER_TEXT
        MOV     DL,14
        JMP     SHORT DRAW_HEADER_READY
DRAW_HEADER_NOT_SEARCH:
        CMP     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        JNE     SHORT DRAW_HEADER_HELP
        MOV     SI,OFFSET OPTIONS_HEADER_TEXT
        MOV     DL,22
        JMP     SHORT DRAW_HEADER_READY
DRAW_HEADER_HELP:
        MOV     SI,OFFSET HELP_HEADER_TEXT
        MOV     DL,73
DRAW_HEADER_READY:
        MOV     DH,0
        MOV     BL,07H
        CALL    DOSUI_WRITE_Z
        RET
DRAW_ACTIVE_HEADER ENDP

DRAW_ACTIVE_MENU PROC NEAR
        CMP     BYTE PTR MENU_ACTIVE,MENU_FILE
        JNE     SHORT DRAW_ACTIVE_NOT_FILE
        JMP     DRAW_FILE_MENU
DRAW_ACTIVE_NOT_FILE:
        CMP     BYTE PTR MENU_ACTIVE,MENU_EDIT
        JNE     SHORT DRAW_ACTIVE_NOT_EDIT
        JMP     DRAW_EDIT_MENU
DRAW_ACTIVE_NOT_EDIT:
        CMP     BYTE PTR MENU_ACTIVE,MENU_SEARCH
        JNE     SHORT DRAW_ACTIVE_NOT_SEARCH
        JMP     DRAW_SEARCH_MENU
DRAW_ACTIVE_NOT_SEARCH:
        CMP     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        JNE     SHORT DRAW_ACTIVE_HELP
        JMP     DRAW_OPTIONS_MENU
DRAW_ACTIVE_HELP:
        JMP     DRAW_HELP_MENU
DRAW_ACTIVE_MENU ENDP

DRAW_FILE_MENU PROC NEAR
        MOV     SI,OFFSET FILE_MENU_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BYTE PTR DOSUI_MENU_ITEM_WIDTH,16
        MOV     SI,OFFSET FILE_MENU_NEW_TEXT
        MOV     AX,0100H
        MOV     DH,2
        MOV     DL,2
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET FILE_MENU_OPEN_TEXT
        MOV     AX,0101H
        MOV     DH,3
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET FILE_MENU_SAVE_TEXT
        MOV     AX,0102H
        MOV     DH,4
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET FILE_MENU_SAVE_AS_TEXT
        MOV     AX,0603H
        MOV     DH,5
        CALL    DOSUI_MENU_ITEM
        MOV     DH,6
        MOV     DL,1
        MOV     CX,18
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET FILE_MENU_PRINT_TEXT
        MOV     AX,0104H
        MOV     DH,7
        MOV     DL,2
        CALL    DOSUI_MENU_ITEM
        MOV     DH,8
        MOV     DL,1
        MOV     CX,18
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET FILE_MENU_EXIT_TEXT
        MOV     AX,0205H
        MOV     DH,9
        MOV     DL,2
        CALL    DOSUI_MENU_ITEM
        RET
DRAW_FILE_MENU ENDP

DRAW_EDIT_MENU PROC NEAR
        MOV     SI,OFFSET EDIT_MENU_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BYTE PTR DOSUI_MENU_ITEM_WIDTH,22
        MOV     SI,OFFSET EDIT_MENU_CUT_TEXT
        MOV     AX,0300H
        MOV     DH,2
        MOV     DL,8
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET EDIT_MENU_COPY_TEXT
        MOV     AX,0101H
        MOV     DH,3
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET EDIT_MENU_PASTE_TEXT
        MOV     AX,0102H
        MOV     DH,4
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET EDIT_MENU_CLEAR_TEXT
        MOV     AX,0203H
        MOV     DH,5
        CALL    DOSUI_MENU_ITEM
        RET
DRAW_EDIT_MENU ENDP

DRAW_SEARCH_MENU PROC NEAR
        MOV     SI,OFFSET SEARCH_MENU_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BYTE PTR DOSUI_MENU_ITEM_WIDTH,26
        MOV     SI,OFFSET SEARCH_MENU_FIND_TEXT
        MOV     AX,0100H
        MOV     DH,2
        MOV     DL,14
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET SEARCH_MENU_NEXT_TEXT
        MOV     AX,0101H
        MOV     DH,3
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET SEARCH_MENU_REPLACE_TEXT
        MOV     AX,0102H
        MOV     DH,4
        CALL    DOSUI_MENU_ITEM
        RET
DRAW_SEARCH_MENU ENDP

DRAW_OPTIONS_MENU PROC NEAR
        MOV     SI,OFFSET OPTIONS_MENU_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BYTE PTR DOSUI_MENU_ITEM_WIDTH,17
        MOV     SI,OFFSET OPTIONS_MENU_DISPLAY_TEXT
        MOV     AX,0100H
        MOV     DH,2
        MOV     DL,22
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET OPTIONS_MENU_HELP_PATH_TEXT
        MOV     AX,0601H
        MOV     DH,3
        CALL    DOSUI_MENU_ITEM
        RET
DRAW_OPTIONS_MENU ENDP

DRAW_HELP_MENU PROC NEAR
        MOV     SI,OFFSET HELP_MENU_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BYTE PTR DOSUI_MENU_ITEM_WIDTH,17
        MOV     SI,OFFSET HELP_MENU_START_TEXT
        MOV     AX,0100H
        MOV     DH,2
        MOV     DL,60
        CALL    DOSUI_MENU_ITEM
        MOV     SI,OFFSET HELP_MENU_KEYBOARD_TEXT
        MOV     AX,0101H
        MOV     DH,3
        CALL    DOSUI_MENU_ITEM
        MOV     DH,4
        MOV     DL,59
        MOV     CX,19
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET HELP_MENU_ABOUT_TEXT
        MOV     AX,0102H
        MOV     DH,5
        MOV     DL,60
        CALL    DOSUI_MENU_ITEM
        RET
DRAW_HELP_MENU ENDP

DRAW_MENU_STATUS PROC NEAR
        XOR     AX,AX
        MOV     AL,MENU_ACTIVE
        MOV     DL,6
        MUL     DL
        XOR     BX,BX
        MOV     BL,DOSUI_MENU_SELECTED
        ADD     AX,BX
        SHL     AX,1
        MOV     BX,AX
        MOV     SI,MENU_STATUS_POINTERS[BX]
        MOV     AL,' '
        MOV     BL,3FH
        MOV     DH,STATUS_ROW
        XOR     DL,DL
        MOV     CX,61
        CMP     BYTE PTR MENU_ACTIVE,MENU_HELP
        JNE     SHORT DRAW_MENU_STATUS_FILL
        MOV     CX,80
DRAW_MENU_STATUS_FILL:
        CALL    DOSUI_FILL_ROW
        PUSH    SI
        MOV     SI,OFFSET MENU_STATUS_PREFIX
        MOV     DH,STATUS_ROW
        XOR     DL,DL
        MOV     BL,3FH
        CALL    DOSUI_WRITE_Z
        MOV     AL,0B3H
        MOV     DL,9
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     SI
        MOV     DL,11
        CALL    DOSUI_WRITE_Z
        RET
DRAW_MENU_STATUS ENDP


; Options Help Path, as DOS 5 EDIT shows it (captured under QEMU,
; 2026-11-11): a "Help Path" dialog with the field "Location (path) of
; EDIT.HLP file:" holding the stored path selected, and OK (the default),
; Cancel and Help. Tab and Shift+Tab cycle field, OK, Cancel, Help; Esc and
; Cancel keep the old path; OK stores the text as typed (DOS 5 takes "C:\*"
; or a missing directory without a word) in QBASIC.INI's help path, written
; at exit. CK-DOS EDIT's help is built in, so the path only travels with
; QBASIC.INI; DOS 5 reads EDIT.HLP through it and reports "File EDIT.HLP not
; found." when it is not there.
SHOW_HELP_PATH PROC NEAR
        MOV     SI,OFFSET INI_HELP_PATH
        MOV     DI,OFFSET HELP_PATH_EDIT
        PUSH    DS
        POP     ES
        MOV     CX,HELP_PATH_FIELD_WIDTH
SHOW_HELP_PATH_COPY:
        LODSB
        OR      AL,AL
        JZ      SHORT SHOW_HELP_PATH_COPIED
        STOSB
        LOOP    SHOW_HELP_PATH_COPY
SHOW_HELP_PATH_COPIED:
        MOV     BYTE PTR [DI],0
SHOW_HELP_PATH_DRAW:
        MOV     BYTE PTR MENU_ACTIVE,MENU_OPTIONS
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET HELP_PATH_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET HELP_PATH_TITLE_TEXT
        MOV     BL,70H
        MOV     DH,7
        MOV     DL,34
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET HELP_PATH_LABEL_TEXT
        MOV     DH,9
        MOV     DL,23
        CALL    DOSUI_WRITE_Z
        MOV     DH,11
        MOV     DL,14
        MOV     CX,HELP_PATH_FIELD_WIDTH+2
        CALL    DRAW_DIALOG_INPUT
        MOV     SI,OFFSET HELP_PATH_EDIT
        MOV     BL,07H
        MOV     DH,12
        MOV     DL,15
        CALL    DOSUI_WRITE_Z
        MOV     DH,14
        MOV     DL,12
        MOV     CX,55
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET HELP_PATH_BUTTONS_TEXT
        MOV     BL,70H
        MOV     DH,15
        MOV     DL,22
        CALL    DOSUI_WRITE_Z
        MOV     SI,OFFSET HELP_PATH_CONTROLS
        MOV     CX,0104H                ; four controls, OK is the default
        XOR     AL,AL
        CALL    RUN_DIALOG
        CMP     AL,DLG_HELP
        JNE     SHORT SHOW_HELP_PATH_SETTLED
        CALL    SHOW_KEYBOARD_HELP
        CALL    DRAW_SCREEN
        JMP     SHOW_HELP_PATH_DRAW
SHOW_HELP_PATH_SETTLED:
        CMP     AL,1
        JNE     SHORT SHOW_HELP_PATH_DONE
        ; OK: the help path becomes the text and its length counts the NUL
        MOV     DI,OFFSET INI_HELP_PATH
        PUSH    DS
        POP     ES
        XOR     AL,AL
        MOV     CX,INI_HELP_PATH_MAX
        REP     STOSB
        MOV     SI,OFFSET HELP_PATH_EDIT
        MOV     DI,OFFSET INI_HELP_PATH
        MOV     CX,1
SHOW_HELP_PATH_STORE:
        LODSB
        OR      AL,AL
        JZ      SHORT SHOW_HELP_PATH_STORED
        STOSB
        INC     CX
        JMP     SHOW_HELP_PATH_STORE
SHOW_HELP_PATH_STORED:
        MOV     INI_HELP_PATH_LENGTH,CX
        MOV     BYTE PTR INI_DIRTY,1    ; QBASIC.INI is written at exit
SHOW_HELP_PATH_DONE:
        RET
SHOW_HELP_PATH ENDP

; ---- Message boxes ------------------------------------------------------
; DOS 5's message box (QEMU captures OE, DF, AB, CF, 2026-11-11: "File not
; found", "Path not found", "Bad file name", "Disk full", "Match not
; found", "Change complete"): rows 9-15 of the dialog rows, the message's
; length plus six columns wide (25 at least) and centred, the message on
; row 11, a rule on row 13, <  OK  > (the default) and < Help > on row 14.
; Tab, Shift+Tab and the arrows move the focus, Enter or Space presses the
; focused button, Esc closes, other keys do nothing; Help shows the dialog
; help and comes back. MESSAGE_BOX_SCREEN draws the editor below first,
; with the menu header AL lit (0FFh: none); MESSAGE_BOX keeps the screen.
MESSAGE_BOX_SCREEN PROC NEAR
        MOV     MESSAGE_HEADER,AL
        MOV     BYTE PTR MESSAGE_REDRAWS,1
        JMP     SHORT MESSAGE_BOX_START
MESSAGE_BOX_SCREEN ENDP

MESSAGE_BOX PROC NEAR
        MOV     BYTE PTR MESSAGE_HEADER,0FFH
        MOV     BYTE PTR MESSAGE_REDRAWS,0
MESSAGE_BOX_START:
        MOV     MESSAGE_TEXT,SI
        MOV     BYTE PTR MESSAGE_FOCUS,0
MESSAGE_BOX_DRAW:
        CMP     BYTE PTR MESSAGE_REDRAWS,0
        JE      SHORT MESSAGE_BOX_FRAME
        CALL    DRAW_SCREEN
        MOV     AL,MESSAGE_HEADER
        CMP     AL,0FFH
        JE      SHORT MESSAGE_BOX_FRAME
        MOV     MENU_ACTIVE,AL
        CALL    DRAW_ACTIVE_HEADER
MESSAGE_BOX_FRAME:
        CALL    DRAW_DIALOG_STATUS      ; and the dialog rows
        MOV     SI,MESSAGE_TEXT
        CALL    STRING_LENGTH
        MOV     AL,CL
        ADD     AL,6
        CMP     AL,25
        JAE     SHORT MESSAGE_BOX_WIDE
        MOV     AL,25
MESSAGE_BOX_WIDE:
        MOV     MESSAGE_WIDTH,AL
        MOV     AH,80
        SUB     AH,AL
        SHR     AH,1
        MOV     MESSAGE_RECT+1,AH       ; left
        ADD     AH,AL
        DEC     AH
        MOV     MESSAGE_RECT+3,AH       ; right
        SUB     AL,2                    ; the text centred inside
        SUB     AL,CL
        SHR     AL,1
        INC     AL
        ADD     AL,MESSAGE_RECT+1
        MOV     MESSAGE_TEXT_COLUMN,AL
        MOV     AL,MESSAGE_WIDTH        ; the 19-cell button row centred
        SUB     AL,21
        SHR     AL,1
        INC     AL
        ADD     AL,MESSAGE_RECT+1
        MOV     MESSAGE_BUTTON_COLUMN,AL
        MOV     SI,OFFSET MESSAGE_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,MESSAGE_TEXT
        MOV     BL,70H
        MOV     DH,11
        MOV     DL,MESSAGE_TEXT_COLUMN
        CALL    DOSUI_WRITE_Z
        MOV     DH,13
        MOV     DL,MESSAGE_RECT+1
        XOR     CX,CX
        MOV     CL,MESSAGE_WIDTH
        CALL    DOSUI_MENU_SEPARATOR
MESSAGE_BOX_FOCUS:
        MOV     SI,OFFSET FILE_ERROR_BUTTONS_TEXT
        MOV     BL,70H
        MOV     DH,14
        MOV     DL,MESSAGE_BUTTON_COLUMN
        CALL    DOSUI_WRITE_Z
        MOV     AH,3                    ; OK: cursor on "O"
        CMP     BYTE PTR MESSAGE_FOCUS,0
        JE      SHORT MESSAGE_BOX_BRACKETS
        ADD     DL,11
        MOV     AH,2                    ; Help: cursor on "H"
MESSAGE_BOX_BRACKETS:
        CALL    BRIGHT_BRACKETS
        ADD     DL,AH
        CALL    DOSUI_SET_CURSOR
        CALL    DOSUI_SHOW_CURSOR
MESSAGE_BOX_KEY:
        CALL    DOSUI_READ_EVENT
        JC      SHORT MESSAGE_BOX_MOUSE
        CMP     AL,27
        JE      SHORT MESSAGE_BOX_DONE
        CMP     AL,13
        JE      SHORT MESSAGE_BOX_ENTER
        CMP     AL,' '
        JE      SHORT MESSAGE_BOX_ENTER
        CMP     AL,9
        JE      SHORT MESSAGE_BOX_TOGGLE
        OR      AL,AL
        JNZ     MESSAGE_BOX_KEY
        CMP     AH,3BH                  ; F1
        JE      SHORT MESSAGE_BOX_HELP
        CALL    IS_FOCUS_KEY
        JNC     MESSAGE_BOX_KEY
MESSAGE_BOX_TOGGLE:
        XOR     BYTE PTR MESSAGE_FOCUS,1
        JMP     MESSAGE_BOX_FOCUS
MESSAGE_BOX_ENTER:
        CMP     BYTE PTR MESSAGE_FOCUS,0
        JE      SHORT MESSAGE_BOX_DONE
MESSAGE_BOX_HELP:
        MOV     AL,HELP_TOPIC_DIALOGS
        CALL    DIALOG_HELP
        MOV     BYTE PTR MESSAGE_REDRAWS,1
        JMP     MESSAGE_BOX_DRAW
MESSAGE_BOX_MOUSE:
        CMP     DH,14
        JNE     MESSAGE_BOX_KEY
        MOV     AL,MESSAGE_BUTTON_COLUMN
        CALL    BUTTON_HIT              ; <  OK  >
        JNC     SHORT MESSAGE_BOX_DONE
        ADD     AL,11
        CALL    BUTTON_HIT              ; < Help >
        JNC     SHORT MESSAGE_BOX_HELP
        JMP     MESSAGE_BOX_KEY
MESSAGE_BOX_DONE:
        RET
MESSAGE_BOX ENDP

; Carry clear when DL lies on the eight-cell button starting at column AL.
BUTTON_HIT PROC NEAR
        CMP     DL,AL
        JB      SHORT BUTTON_HIT_MISS
        PUSH    AX
        ADD     AL,7
        CMP     AL,DL
        POP     AX
        JB      SHORT BUTTON_HIT_MISS
        CLC
        RET
BUTTON_HIT_MISS:
        STC
        RET
BUTTON_HIT ENDP

; The focused button's brackets in bright white: "<" at row DH, column DL,
; and ">" seven columns on. DL is kept.
BRIGHT_BRACKETS PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        MOV     AL,'<'
        MOV     BL,7FH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        ADD     DL,7
        MOV     AL,'>'
        CALL    DOSUI_FILL_ROW
        SUB     DL,7
        POP     CX
        POP     BX
        POP     AX
        RET
BRIGHT_BRACKETS ENDP

; Carry set for the extended keys that move a dialog's focus: Shift+Tab and
; the four arrows.
IS_FOCUS_KEY PROC NEAR
        CMP     AH,0FH
        JE      SHORT IS_FOCUS_KEY_YES
        CMP     AH,48H
        JE      SHORT IS_FOCUS_KEY_YES
        CMP     AH,50H
        JE      SHORT IS_FOCUS_KEY_YES
        CMP     AH,4BH
        JE      SHORT IS_FOCUS_KEY_YES
        CMP     AH,4DH
        JE      SHORT IS_FOCUS_KEY_YES
        CLC
        RET
IS_FOCUS_KEY_YES:
        STC
        RET
IS_FOCUS_KEY ENDP

SHOW_NOT_FOUND PROC NEAR
        MOV     SI,OFFSET NOT_FOUND_TEXT
        MOV     AL,MENU_SEARCH
        JMP     MESSAGE_BOX_SCREEN
SHOW_NOT_FOUND ENDP

SHOW_CHANGE_COMPLETE PROC NEAR
        MOV     SI,OFFSET CHANGE_COMPLETE_TEXT
        MOV     AL,MENU_SEARCH
        JMP     MESSAGE_BOX_SCREEN
SHOW_CHANGE_COMPLETE ENDP

; The failed file operation's message (FILE_ERROR) over the editor, or
; over the dialog still on the screen (SHOW_FILE_ERROR_OVER).
SHOW_FILE_ERROR PROC NEAR
        CALL    FILE_ERROR_MESSAGE
        MOV     AL,0FFH
        JMP     MESSAGE_BOX_SCREEN
SHOW_FILE_ERROR ENDP

SHOW_FILE_ERROR_OVER PROC NEAR
        CALL    FILE_ERROR_MESSAGE
        JMP     MESSAGE_BOX
SHOW_FILE_ERROR_OVER ENDP

SHOW_PATH_NOT_FOUND PROC NEAR
        MOV     WORD PTR FILE_ERROR,3
        JMP     SHOW_FILE_ERROR_OVER
SHOW_PATH_NOT_FOUND ENDP

; SI = DOS 5's wording for FILE_ERROR: a DOS error code, or one of the
; editor's own (ERROR_DISK_FULL, ERROR_BAD_NAME, ERROR_MEMORY).
FILE_ERROR_MESSAGE PROC NEAR
        PUSH    AX
        PUSH    BX
        MOV     AX,FILE_ERROR
        MOV     BX,OFFSET FILE_ERROR_TABLE
FILE_ERROR_MESSAGE_NEXT:
        MOV     SI,[BX+2]
        CMP     WORD PTR [BX],0
        JE      SHORT FILE_ERROR_MESSAGE_DONE
        CMP     AX,[BX]
        JE      SHORT FILE_ERROR_MESSAGE_DONE
        ADD     BX,4
        JMP     FILE_ERROR_MESSAGE_NEXT
FILE_ERROR_MESSAGE_DONE:
        POP     BX
        POP     AX
        RET
FILE_ERROR_MESSAGE ENDP

; FILE_ERROR = AX, a failed DOS call's error; one the critical-error
; handler failed (83) is replaced by DOS's extended error.
NOTE_DOS_ERROR PROC NEAR
        CMP     AX,83
        JNE     SHORT NOTE_DOS_ERROR_STORE
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    BP
        PUSH    DS
        PUSH    ES
        MOV     AH,59H
        XOR     BX,BX
        INT     21H
        POP     ES
        POP     DS
        POP     BP
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
NOTE_DOS_ERROR_STORE:
        MOV     FILE_ERROR,AX
        RET
NOTE_DOS_ERROR ENDP

; DOS asks what to do about a disk or device error: fail the call, so the
; editor reports it in its own box (DOS 5 EDIT never shows DOS's prompt).
CRITICAL_ERROR_HANDLER PROC FAR
        MOV     AL,3
        IRET
CRITICAL_ERROR_HANDLER ENDP

; A file named on the command line that cannot be opened: DOS 5 EDIT prints
; the message where the cursor is, shows "Press any key to continue" on the
; bottom row with the cursor hidden, and after a key puts that row back and
; returns to DOS with the cursor after the message (QEMU captures OS2-OS4,
; 2026-11-11: "Path not found", "Device unavailable", "Bad file name").
STARTUP_ABORT PROC NEAR
        CALL    FILE_ERROR_MESSAGE
        MOV     AH,0FH
        INT     10H                     ; BH = the active page
STARTUP_ABORT_PRINT:
        LODSB
        OR      AL,AL
        JZ      SHORT STARTUP_ABORT_PRINTED
        MOV     AH,0EH
        INT     10H
        JMP     STARTUP_ABORT_PRINT
STARTUP_ABORT_PRINTED:
        MOV     AH,03H
        INT     10H
        PUSH    CX                      ; the shape and the place to return to
        PUSH    DX
        PUSH    ES
        MOV     AX,40H
        MOV     ES,AX
        MOV     DH,ES:[84H]             ; the bottom row
        POP     ES
        OR      DH,DH
        JNZ     SHORT STARTUP_ABORT_ROW
        MOV     DH,24
STARTUP_ABORT_ROW:
        MOV     STARTUP_ABORT_LAST,DH
        XOR     DL,DL                   ; keep the row in the scratch space
        MOV     DI,OFFSET SCRATCH
        MOV     CX,80
STARTUP_ABORT_SAVE:
        MOV     AH,02H
        INT     10H
        MOV     AH,08H
        INT     10H
        STOSW
        INC     DL
        LOOP    STARTUP_ABORT_SAVE
        XOR     DL,DL
        MOV     AH,02H
        INT     10H
        MOV     AX,0920H
        MOV     BL,07H
        MOV     CX,80
        INT     10H
        MOV     SI,OFFSET PRESS_ANY_KEY_TEXT
STARTUP_ABORT_PROMPT:
        LODSB
        OR      AL,AL
        JZ      SHORT STARTUP_ABORT_WAIT
        MOV     AH,0EH
        INT     10H
        JMP     STARTUP_ABORT_PROMPT
STARTUP_ABORT_WAIT:
        MOV     AH,01H
        MOV     CX,2707H                ; hidden, as DOS 5 leaves it
        INT     10H
        XOR     AH,AH
        INT     16H
        MOV     DH,STARTUP_ABORT_LAST
        XOR     DL,DL
        MOV     SI,OFFSET SCRATCH
        MOV     CX,80
STARTUP_ABORT_RESTORE:
        PUSH    CX
        MOV     AH,02H
        INT     10H
        LODSW
        MOV     BL,AH
        MOV     AH,09H
        MOV     CX,1
        INT     10H
        INC     DL
        POP     CX
        LOOP    STARTUP_ABORT_RESTORE
        POP     DX
        POP     CX
        MOV     AH,02H
        INT     10H
        MOV     AH,01H
        INT     10H
        CALL    FREE_MEMORY
        MOV     AX,4C00H
        INT     21H
STARTUP_ABORT ENDP

; Carry: FILE_NAME (SI) is no name DOS 5 EDIT takes ("Bad file name"): one
; of " + , ; = [ ] | < > or a control character in it, or nothing after
; its drive and directory.
CHECK_FILE_NAME PROC NEAR
        PUSH    AX
        PUSH    DI
        XOR     AH,AH                   ; characters since the last "\" or ":"
CHECK_FILE_NAME_NEXT:
        LODSB
        OR      AL,AL
        JZ      SHORT CHECK_FILE_NAME_END
        INC     AH
        CMP     AL,' '
        JBE     SHORT CHECK_FILE_NAME_BAD
        CMP     AL,'\'
        JE      SHORT CHECK_FILE_NAME_PART
        CMP     AL,':'
        JE      SHORT CHECK_FILE_NAME_PART
        MOV     DI,OFFSET BAD_NAME_CHARACTERS
        PUSH    CX
        MOV     CX,BAD_NAME_COUNT
        PUSH    ES
        PUSH    DS
        POP     ES
        REPNE   SCASB
        POP     ES
        POP     CX
        JE      SHORT CHECK_FILE_NAME_BAD
        JMP     CHECK_FILE_NAME_NEXT
CHECK_FILE_NAME_PART:
        XOR     AH,AH
        JMP     CHECK_FILE_NAME_NEXT
CHECK_FILE_NAME_END:
        OR      AH,AH
        JZ      SHORT CHECK_FILE_NAME_BAD
        POP     DI
        POP     AX
        CLC
        RET
CHECK_FILE_NAME_BAD:
        MOV     WORD PTR FILE_ERROR,ERROR_BAD_NAME
        POP     DI
        POP     AX
        STC
        RET
CHECK_FILE_NAME ENDP

; Carry: the name at SI starts with a drive DOS does not have.
CHECK_FILE_DRIVE PROC NEAR
        CMP     BYTE PTR [SI+1],':'
        JNE     SHORT CHECK_FILE_DRIVE_OK
        PUSH    AX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        PUSH    DS
        POP     ES
        MOV     DI,OFFSET OPEN_FCB
        MOV     AX,2900H
        INT     21H
        POP     ES
        POP     DI
        POP     SI
        CMP     AL,0FFH
        POP     AX
        JNE     SHORT CHECK_FILE_DRIVE_OK
        MOV     WORD PTR FILE_ERROR,15
        STC
        RET
CHECK_FILE_DRIVE_OK:
        CLC
        RET
CHECK_FILE_DRIVE ENDP

; Help/About: DOS 5's box (rows 8-16, columns 13-65, three centred lines,
; <  OK  > on row 15 with the cursor on its "O"; Enter, Space, Esc or a
; click on OK close it, other keys do nothing), with CK-DOS's own lines.
SHOW_ABOUT PROC NEAR
        CALL    DRAW_SCREEN
        MOV     BYTE PTR MENU_ACTIVE,MENU_HELP
        CALL    DRAW_ACTIVE_HEADER
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET ABOUT_RECT
        CALL    DOSUI_MENU_BOX
        MOV     BL,70H
        MOV     SI,OFFSET ABOUT_LINE1
        MOV     DH,10
        CALL    WRITE_ABOUT_LINE
        MOV     SI,OFFSET ABOUT_LINE2
        INC     DH
        CALL    WRITE_ABOUT_LINE
        MOV     SI,OFFSET WELCOME_COPYRIGHT_TEXT
        INC     DH
        CALL    WRITE_ABOUT_LINE
        MOV     DH,14
        MOV     DL,13
        MOV     CX,53
        CALL    DOSUI_MENU_SEPARATOR
        MOV     SI,OFFSET ABOUT_BUTTON_TEXT
        MOV     DH,15
        MOV     DL,35
        CALL    DOSUI_WRITE_Z
        CALL    BRIGHT_BRACKETS
        MOV     DL,38
        CALL    DOSUI_SET_CURSOR
        CALL    DOSUI_SHOW_CURSOR
SHOW_ABOUT_KEY:
        CALL    DOSUI_READ_EVENT
        JC      SHORT SHOW_ABOUT_MOUSE
        CMP     AL,27
        JE      SHORT SHOW_ABOUT_DONE
        CMP     AL,13
        JE      SHORT SHOW_ABOUT_DONE
        CMP     AL,' '
        JE      SHORT SHOW_ABOUT_DONE
        OR      AL,AL
        JNZ     SHOW_ABOUT_KEY
        CMP     AH,3BH                  ; F1: help, then the box again
        JNE     SHOW_ABOUT_KEY
        MOV     AL,HELP_TOPIC_DIALOGS
        CALL    DIALOG_HELP
        JMP     SHOW_ABOUT
SHOW_ABOUT_MOUSE:
        CMP     DH,15
        JNE     SHOW_ABOUT_KEY
        MOV     AL,35
        CALL    BUTTON_HIT
        JC      SHOW_ABOUT_KEY
SHOW_ABOUT_DONE:
        RET
SHOW_ABOUT ENDP

; SI centred between the About box's frame columns, on row DH.
WRITE_ABOUT_LINE PROC NEAR
        CALL    STRING_LENGTH
        MOV     DL,51
        SUB     DL,CL
        SHR     DL,1
        ADD     DL,14
        JMP     DOSUI_WRITE_Z
WRITE_ABOUT_LINE ENDP

; ---- Help -----------------------------------------------------------------
; Help works the way DOS 5 EDIT's does (QEMU captures AB and HK, 2026-11-11;
; H1, H2 and M2, 2026-11-12), with CK-DOS's own topics: a window at the top
; of the screen titled "HELP: <topic>", as tall as the topic (the screen
; less five rows at most), and the editor's window below it under its title
; rule, both on screen at once. One of them is active: its title is lit and
; it has the scroll bars (the editor only with room for them); the status
; line is the active window's. F6 and Shift+F6 change windows, so does a
; click into the other one; Alt+Plus and Alt+Minus make the active window a
; row taller or shorter, and dragging the editor's title rule moves the
; border. Esc in either window closes Help. In Help, every topic but the
; first starts with a row of links and a rule; Tab and Shift+Tab move
; between links, a letter to the next link it starts, Enter follows one,
; Alt+F1 goes back, Ctrl+F1 and Shift+Ctrl+F1 step through the topics, F1
; and Shift+F1 open Getting Started, the arrows and PgUp/PgDn scroll, and
; the menus work as in the editor. A dialog's Help (DIALOG_HELP) shows a
; topic in a box of its own instead. Topic lines: 01h switches bold on and
; off, 11h and 10h enclose a link, whose text names its topic ("Back" goes
; back).

; Open Help on topic AL and make it the active window: a fresh history when
; Help was closed, the shown topic onto it when it was open.
HELP_OPEN_AT PROC NEAR
        CMP     BYTE PTR HELP_OPEN,0
        JE      SHORT HELP_OPEN_NEW
        CALL    HELP_PUSH_HISTORY
        JMP     SHORT HELP_OPEN_SELECT
HELP_OPEN_NEW:
        MOV     BYTE PTR HELP_DEPTH,0
        MOV     BYTE PTR HELP_OPEN,1
HELP_OPEN_SELECT:
        MOV     BYTE PTR HELP_ACTIVE,1
        MOV     BYTE PTR MOUSE_REPEAT,0
        CALL    HELP_SELECT_TOPIC
        CALL    HELP_FIT_ROWS
        RET
HELP_OPEN_AT ENDP

HELP_CLOSE PROC NEAR
        MOV     BYTE PTR HELP_OPEN,0
        MOV     BYTE PTR HELP_ACTIVE,0
        RET
HELP_CLOSE ENDP

; The shown topic onto the history (the oldest goes when it is full).
HELP_PUSH_HISTORY PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        PUSH    DS
        POP     ES
        XOR     BX,BX
        MOV     BL,HELP_DEPTH
        CMP     BL,HELP_HISTORY_MAX
        JB      SHORT HELP_PUSH_ROOM
        MOV     SI,OFFSET HELP_HISTORY+1
        MOV     DI,OFFSET HELP_HISTORY
        MOV     CX,HELP_HISTORY_MAX-1
        REP     MOVSB
        DEC     BX
        DEC     BYTE PTR HELP_DEPTH
HELP_PUSH_ROOM:
        MOV     AH,HELP_TOPIC
        MOV     HELP_HISTORY[BX],AH
        INC     BYTE PTR HELP_DEPTH
        POP     ES
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        POP     AX
        RET
HELP_PUSH_HISTORY ENDP

; Help's window as tall as its topic, the screen less five rows at most.
HELP_FIT_ROWS PROC NEAR
        PUSH    AX
        MOV     AL,0FFH
        CMP     WORD PTR HELP_LINES,0FFH
        JAE     SHORT HELP_FIT_HAVE_LINES
        MOV     AL,BYTE PTR HELP_LINES
HELP_FIT_HAVE_LINES:
        CALL    HELP_SET_ROWS
        POP     AX
        RET
HELP_FIT_ROWS ENDP

; Help's window AL rows tall, at least one and leaving the editor one.
HELP_SET_ROWS PROC NEAR
        PUSH    AX
        PUSH    BX
        MOV     BL,DOSUI_ROWS
        SUB     BL,5
        CMP     AL,BL
        JBE     SHORT HELP_SET_NOT_ABOVE
        MOV     AL,BL
HELP_SET_NOT_ABOVE:
        OR      AL,AL
        JNZ     SHORT HELP_SET_STORE
        INC     AL
HELP_SET_STORE:
        MOV     HELP_WINDOW_ROWS,AL
        POP     BX
        POP     AX
        RET
HELP_SET_ROWS ENDP

; Alt+Plus and Alt+Minus: Help's window AL rows taller (signed).
HELP_RESIZE PROC NEAR
        PUSH    AX
        ADD     AL,HELP_WINDOW_ROWS
        JNS     SHORT HELP_RESIZE_SET
        XOR     AL,AL
HELP_RESIZE_SET:
        CALL    HELP_SET_ROWS
        POP     AX
        RET
HELP_RESIZE ENDP

; Help, the active window, until something leaves it. AL returns what
; EDIT_LOOP does next: HELP_EXIT_REDRAW (Help closed, or the editor became
; active), HELP_EXIT_MOUSE (a click at DH:DL, screen rows, for the editor's
; window or the menu bar) or HELP_EXIT_KEY (a menu key, in HELP_KEY_STASH).
HELP_VIEW PROC NEAR
        CMP     BYTE PTR HELP_PENDING_CLICK,0   ; the click that activated it
        JE      SHORT HELP_VIEW_DRAW
        MOV     BYTE PTR HELP_PENDING_CLICK,0
        MOV     DX,HELP_CLICK_CELL
        JMP     HELP_VIEW_AREA_CLICK
HELP_VIEW_OPEN:
        CALL    HELP_SELECT_TOPIC
        CALL    HELP_FIT_ROWS
HELP_VIEW_DRAW:
        CALL    DRAW_SCREEN
HELP_VIEW_KEY:
        XOR     AL,AL                   ; clicks in screen rows
        CALL    SET_ROW_OFFSET
        CALL    DOSUI_READ_EVENT
        JNC     SHORT HELP_VIEW_HAVE_KEY
        JMP     HELP_VIEW_MOUSE
HELP_VIEW_HAVE_KEY:
        CMP     AL,27
        JNE     SHORT HELP_VIEW_NOT_ESCAPE
        CALL    HELP_CLOSE
        JMP     HELP_VIEW_LEAVE
HELP_VIEW_NOT_ESCAPE:
        CMP     AL,9
        JNE     SHORT HELP_VIEW_NOT_TAB
        JMP     HELP_VIEW_NEXT_LINK
HELP_VIEW_NOT_TAB:
        CMP     AL,13
        JNE     SHORT HELP_VIEW_NOT_ENTER
        JMP     HELP_VIEW_FOLLOW
HELP_VIEW_NOT_ENTER:
        OR      AL,AL
        JZ      SHORT HELP_VIEW_EXTENDED
        CALL    HELP_LINK_BY_LETTER
        JMP     HELP_VIEW_DRAW
HELP_VIEW_EXTENDED:
        CMP     AH,40H                  ; F6, Shift+F6: the editor's window
        JE      SHORT HELP_VIEW_TO_EDITOR
        CMP     AH,59H
        JE      SHORT HELP_VIEW_TO_EDITOR
        CALL    IS_MENU_KEY             ; Alt, Alt+a menu's letter: the menus
        JNC     SHORT HELP_VIEW_NOT_MENU
        MOV     HELP_KEY_STASH,AX
        MOV     AL,HELP_EXIT_KEY
        RET
HELP_VIEW_TO_EDITOR:
        MOV     BYTE PTR HELP_ACTIVE,0
HELP_VIEW_LEAVE:
        MOV     AL,HELP_EXIT_REDRAW
        RET
HELP_VIEW_NOT_MENU:
        MOV     AL,1
        CMP     AH,4EH                  ; Alt+Plus: Help a row taller
        JE      SHORT HELP_VIEW_RESIZE
        MOV     AL,0FFH
        CMP     AH,4AH                  ; Alt+Minus: a row shorter
        JNE     SHORT HELP_VIEW_NOT_RESIZE
HELP_VIEW_RESIZE:
        CALL    HELP_RESIZE
        JMP     HELP_VIEW_DRAW
HELP_VIEW_NOT_RESIZE:
        CMP     AH,0FH                  ; Shift+Tab
        JE      SHORT HELP_VIEW_PREVIOUS_LINK
        CMP     AH,3BH                  ; F1, Shift+F1: Getting Started
        JE      SHORT HELP_VIEW_STARTED
        CMP     AH,54H
        JE      SHORT HELP_VIEW_STARTED
        CMP     AH,68H                  ; Alt+F1: back
        JNE     SHORT HELP_VIEW_NOT_BACK
        JMP     HELP_VIEW_BACK
HELP_VIEW_NOT_BACK:
        CMP     AH,5EH                  ; Ctrl+F1 (with Shift: back a topic)
        JE      SHORT HELP_VIEW_STEP
        MOV     BX,1
        CMP     AH,50H                  ; Down
        JE      SHORT HELP_VIEW_SCROLL
        CMP     AH,51H                  ; PgDn
        JE      SHORT HELP_VIEW_PAGE
        NEG     BX
        CMP     AH,48H                  ; Up
        JE      SHORT HELP_VIEW_SCROLL
        CMP     AH,49H                  ; PgUp
        JE      SHORT HELP_VIEW_PAGE
        JMP     HELP_VIEW_KEY
HELP_VIEW_PAGE:
        MOV     AL,HELP_WINDOW_ROWS
        CBW
        IMUL    BX
        MOV     BX,AX
HELP_VIEW_SCROLL:
        CALL    HELP_SCROLL
        JMP     HELP_VIEW_DRAW
HELP_VIEW_NEXT_LINK:
        MOV     AL,1
        JMP     SHORT HELP_VIEW_MOVE_LINK
HELP_VIEW_PREVIOUS_LINK:
        MOV     AL,0FFH
HELP_VIEW_MOVE_LINK:
        CALL    HELP_MOVE_LINK
        JMP     HELP_VIEW_DRAW
HELP_VIEW_STARTED:
        MOV     AL,HELP_TOPIC_STARTED
        JMP     SHORT HELP_VIEW_PUSH
HELP_VIEW_STEP:
        PUSH    AX
        MOV     AH,02H
        INT     16H
        TEST    AL,03H
        POP     AX
        MOV     AL,HELP_TOPIC
        JNZ     SHORT HELP_VIEW_STEP_BACK
        INC     AL
        CMP     AL,HELP_TOPIC_COUNT
        JB      SHORT HELP_VIEW_PUSH
        XOR     AL,AL
        JMP     SHORT HELP_VIEW_PUSH
HELP_VIEW_STEP_BACK:
        DEC     AL
        JNS     SHORT HELP_VIEW_PUSH
        MOV     AL,HELP_TOPIC_COUNT-1
HELP_VIEW_PUSH:                         ; AL = the topic to open
        CALL    HELP_PUSH_HISTORY
        JMP     HELP_VIEW_OPEN
HELP_VIEW_BACK:
        CMP     BYTE PTR HELP_DEPTH,0
        JNE     SHORT HELP_VIEW_BACK_ONE
        JMP     HELP_VIEW_KEY
HELP_VIEW_BACK_ONE:
        DEC     BYTE PTR HELP_DEPTH
        XOR     BX,BX
        MOV     BL,HELP_DEPTH
        MOV     AL,HELP_HISTORY[BX]
        JMP     HELP_VIEW_OPEN
HELP_VIEW_FOLLOW:
        CALL    HELP_FOCUSED_TARGET
        CMP     AL,HELP_LINK_BACK
        JE      HELP_VIEW_BACK
        CMP     AL,HELP_TOPIC_COUNT
        JB      HELP_VIEW_PUSH
        JMP     HELP_VIEW_KEY
HELP_VIEW_MOUSE:                        ; DH:DL in screen rows
        OR      DH,DH                   ; the menu bar: as in the editor
        JZ      SHORT HELP_VIEW_PASS_CLICK
        CMP     DH,STATUS_ROW           ; the status line names keys
        JNE     SHORT HELP_VIEW_NOT_STATUS
        CALL    HELP_STATUS_KEY
        JC      SHORT HELP_VIEW_AREA_MISS
        JMP     HELP_VIEW_HAVE_KEY
HELP_VIEW_NOT_STATUS:
        MOV     AL,HELP_WINDOW_ROWS
        INC     AL
        CMP     DH,AL
        JBE     SHORT HELP_VIEW_AREA_CLICK
        MOV     BYTE PTR HELP_ACTIVE,0  ; the editor's window becomes active
HELP_VIEW_PASS_CLICK:
        MOV     AL,HELP_EXIT_MOUSE
        RET
HELP_VIEW_AREA_CLICK:                   ; Help's own window: a link opens
        CMP     DH,2
        JB      SHORT HELP_VIEW_AREA_MISS
        CALL    HELP_LINK_AT
        JC      SHORT HELP_VIEW_AREA_MISS
        MOV     HELP_LINK,AL
        JMP     HELP_VIEW_FOLLOW
HELP_VIEW_AREA_MISS:
        JMP     HELP_VIEW_DRAW
HELP_VIEW ENDP

; CF set: AX is a key that opens the menus (a lone Alt, Alt+F/E/S/O/H).
IS_MENU_KEY PROC NEAR
        OR      AL,AL
        JNZ     SHORT IS_MENU_KEY_NO
        CMP     AH,38H
        JE      SHORT IS_MENU_KEY_YES
        CMP     AH,21H
        JE      SHORT IS_MENU_KEY_YES
        CMP     AH,12H
        JE      SHORT IS_MENU_KEY_YES
        CMP     AH,1FH
        JE      SHORT IS_MENU_KEY_YES
        CMP     AH,18H
        JE      SHORT IS_MENU_KEY_YES
        CMP     AH,23H
        JE      SHORT IS_MENU_KEY_YES
IS_MENU_KEY_NO:
        CLC
        RET
IS_MENU_KEY_YES:
        STC
        RET
IS_MENU_KEY ENDP

; A click on Help's status line at column DL: AX = the key its item names
; (CF set between the items).
HELP_STATUS_KEY PROC NEAR
        PUSH    SI
        MOV     SI,OFFSET HELP_STATUS_ITEMS
HELP_STATUS_KEY_NEXT:
        CMP     BYTE PTR [SI],0
        JE      SHORT HELP_STATUS_KEY_MISS
        CMP     DL,[SI]
        JB      SHORT HELP_STATUS_KEY_SKIP
        CMP     DL,[SI+1]
        JA      SHORT HELP_STATUS_KEY_SKIP
        MOV     AX,[SI+2]
        POP     SI
        CLC
        RET
HELP_STATUS_KEY_SKIP:
        ADD     SI,4
        JMP     HELP_STATUS_KEY_NEXT
HELP_STATUS_KEY_MISS:
        POP     SI
        STC
        RET
HELP_STATUS_KEY ENDP

; A topic's state while DIALOG_HELP borrows the viewer: HELP_TOPIC up to
; HELP_LINKS' end, HELP_STATE_BYTES of them, kept in HELP_STATE_COPY.
HELP_STATE_SAVE PROC NEAR
        PUSH    SI
        PUSH    DI
        MOV     SI,OFFSET HELP_TOPIC
        MOV     DI,OFFSET HELP_STATE_COPY
        CALL    HELP_STATE_MOVE
        POP     DI
        POP     SI
        RET
HELP_STATE_SAVE ENDP

HELP_STATE_RESTORE PROC NEAR
        PUSH    SI
        PUSH    DI
        MOV     SI,OFFSET HELP_STATE_COPY
        MOV     DI,OFFSET HELP_TOPIC
        CALL    HELP_STATE_MOVE
        POP     DI
        POP     SI
        RET
HELP_STATE_RESTORE ENDP

HELP_STATE_MOVE PROC NEAR
        PUSH    CX
        PUSH    ES
        PUSH    DS
        POP     ES
        MOV     CX,HELP_STATE_BYTES
        CLD
        REP     MOVSB
        POP     ES
        POP     CX
        RET
HELP_STATE_MOVE ENDP

; Make AL the current topic: its first line on top, its first link that
; leads elsewhere focused (HELP_LINK = FFh when it has none).
HELP_SELECT_TOPIC PROC NEAR
        MOV     HELP_TOPIC,AL
        MOV     WORD PTR HELP_TOP,0
        CALL    HELP_SCAN_LINKS
        MOV     BYTE PTR HELP_LINK,0FFH
        CMP     BYTE PTR HELP_LINK_COUNT,0
        JE      SHORT HELP_SELECT_DONE
        MOV     BYTE PTR HELP_LINK,0
HELP_SELECT_DONE:
        RET
HELP_SELECT_TOPIC ENDP

; SI = line BX of the current topic (CF set past its end): a topic but the
; first starts with the row of links and a rule (HELP_RULE_LINE).
HELP_LINE PROC NEAR
        PUSH    AX
        PUSH    BX
        CMP     BYTE PTR HELP_TOPIC,0
        JE      SHORT HELP_LINE_BODY
        MOV     SI,OFFSET HELP_NAVIGATION_LINE
        OR      BX,BX
        JZ      SHORT HELP_LINE_FOUND
        MOV     SI,OFFSET HELP_RULE_LINE
        DEC     BX
        JZ      SHORT HELP_LINE_FOUND
        DEC     BX
HELP_LINE_BODY:                         ; from the topics' block: a copy
        PUSH    DI
        PUSH    DS
        PUSH    ES
        PUSH    CS
        POP     ES
        XOR     AX,AX
        MOV     AL,HELP_TOPIC
        SHL     AX,1
        MOV     SI,AX
        MOV     DS,CS:HELP_SEGMENT
        MOV     SI,[SI]
HELP_LINE_SKIP:
        CMP     BYTE PTR [SI],0FFH
        JE      SHORT HELP_LINE_PAST
        OR      BX,BX
        JZ      SHORT HELP_LINE_COPY
HELP_LINE_TO_END:
        LODSB
        OR      AL,AL
        JNZ     HELP_LINE_TO_END
        DEC     BX
        JMP     HELP_LINE_SKIP
HELP_LINE_COPY:
        MOV     DI,OFFSET HELP_LINE_BUFFER
HELP_LINE_COPY_NEXT:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     HELP_LINE_COPY_NEXT
        POP     ES
        POP     DS
        POP     DI
        MOV     SI,OFFSET HELP_LINE_BUFFER
        JMP     SHORT HELP_LINE_FOUND
HELP_LINE_PAST:
        POP     ES
        POP     DS
        POP     DI
        POP     BX
        POP     AX
        STC
        RET
HELP_LINE_FOUND:
        POP     BX
        POP     AX
        CLC
        RET
HELP_LINE ENDP

; HELP_LINES = the current topic's line count; HELP_LINKS lists its links
; that lead elsewhere (line, column, length, target), HELP_LINK_COUNT many.
HELP_SCAN_LINKS PROC NEAR
        XOR     BX,BX
        MOV     BYTE PTR HELP_LINK_COUNT,0
        MOV     DI,OFFSET HELP_LINKS
HELP_SCAN_LINE:
        CALL    HELP_LINE
        JC      SHORT HELP_SCAN_DONE
        XOR     DL,DL                   ; DL = column within the line
HELP_SCAN_CHARACTER:
        LODSB
        OR      AL,AL
        JZ      SHORT HELP_SCAN_NEXT_LINE
        CMP     AL,1
        JE      HELP_SCAN_CHARACTER
        INC     DL
        CMP     AL,11H
        JNE     HELP_SCAN_CHARACTER
        CALL    HELP_LINK_TARGET        ; AL = target, CL = text length
        CMP     AL,HELP_TOPIC
        JE      SHORT HELP_SCAN_SKIP
        CMP     BYTE PTR HELP_LINK_COUNT,HELP_LINK_MAX
        JAE     SHORT HELP_SCAN_SKIP
        MOV     [DI],BL                 ; line
        MOV     [DI+1],DL               ; column of the text
        MOV     [DI+2],CL               ; length
        MOV     [DI+3],AL               ; target
        ADD     DI,4
        INC     BYTE PTR HELP_LINK_COUNT
HELP_SCAN_SKIP:
        XOR     CH,CH
        ADD     SI,CX
        ADD     DL,CL
        JMP     HELP_SCAN_CHARACTER
HELP_SCAN_NEXT_LINE:
        INC     BX
        JMP     HELP_SCAN_LINE
HELP_SCAN_DONE:
        MOV     HELP_LINES,BX
        RET
HELP_SCAN_LINKS ENDP

; SI = a link's text (after its 11h): AL = the topic its text names
; (HELP_LINK_BACK for "Back", 0FFh for none), CL = the text's length.
HELP_LINK_TARGET PROC NEAR
        PUSH    BX
        PUSH    DX
        PUSH    DI
        XOR     CX,CX
HELP_LINK_LENGTH:
        MOV     BX,CX
        CMP     BYTE PTR [SI+BX],10H
        JE      SHORT HELP_LINK_MEASURED
        INC     CX
        JMP     HELP_LINK_LENGTH
HELP_LINK_MEASURED:
        MOV     AL,HELP_LINK_BACK
        MOV     DI,OFFSET HELP_BACK_TEXT
        CALL    HELP_SAME_TEXT
        JE      SHORT HELP_LINK_TARGET_DONE
        XOR     AX,AX
        MOV     BX,OFFSET HELP_TOPICS
HELP_LINK_TOPIC:
        MOV     DI,[BX]
        CALL    HELP_SAME_TEXT
        JE      SHORT HELP_LINK_TARGET_DONE
        ADD     BX,2
        INC     AL
        CMP     AL,HELP_TOPIC_COUNT
        JB      HELP_LINK_TOPIC
        MOV     AL,0FFH
HELP_LINK_TARGET_DONE:
        POP     DI
        POP     DX
        POP     BX
        RET
HELP_LINK_TARGET ENDP

; ZF: the CX bytes at SI equal the zero-terminated string at DI.
HELP_SAME_TEXT PROC NEAR
        PUSH    CX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        PUSH    DS
        POP     ES
        REPE    CMPSB
        JNE     SHORT HELP_SAME_DONE
        CMP     BYTE PTR [DI],0
HELP_SAME_DONE:
        POP     ES
        POP     DI
        POP     SI
        POP     CX
        RET
HELP_SAME_TEXT ENDP

; AL = the focused link's target (0FFh: none).
HELP_FOCUSED_TARGET PROC NEAR
        MOV     AL,0FFH
        CMP     BYTE PTR HELP_LINK,0FFH
        JE      SHORT HELP_FOCUSED_DONE
        CALL    HELP_FOCUSED_ENTRY
        MOV     AL,[BX+3]
HELP_FOCUSED_DONE:
        RET
HELP_FOCUSED_TARGET ENDP

; BX = the focused link's HELP_LINKS entry.
HELP_FOCUSED_ENTRY PROC NEAR
        XOR     BX,BX
        MOV     BL,HELP_LINK
        SHL     BX,1
        SHL     BX,1
        ADD     BX,OFFSET HELP_LINKS
        RET
HELP_FOCUSED_ENTRY ENDP

; AL = +1 or -1: the next or previous link, around the end; the view
; follows it.
HELP_MOVE_LINK PROC NEAR
        MOV     AH,HELP_LINK_COUNT
        OR      AH,AH
        JZ      SHORT HELP_MOVE_DONE
        ADD     AL,HELP_LINK
        JNS     SHORT HELP_MOVE_NOT_BELOW
        MOV     AL,AH
        DEC     AL
HELP_MOVE_NOT_BELOW:
        CMP     AL,AH
        JB      SHORT HELP_MOVE_SET
        XOR     AL,AL
HELP_MOVE_SET:
        MOV     HELP_LINK,AL
        CALL    HELP_SHOW_FOCUS
HELP_MOVE_DONE:
        RET
HELP_MOVE_LINK ENDP

; A letter: the next link after the focused one whose text starts with it.
HELP_LINK_BY_LETTER PROC NEAR
        AND     AL,0DFH
        MOV     DL,AL
        MOV     CL,HELP_LINK_COUNT
        OR      CL,CL
        JZ      SHORT HELP_LETTER_DONE
        MOV     DH,HELP_LINK
HELP_LETTER_NEXT:
        INC     DH
        CMP     DH,HELP_LINK_COUNT
        JB      SHORT HELP_LETTER_TRY
        XOR     DH,DH
HELP_LETTER_TRY:
        XOR     BX,BX
        MOV     BL,DH
        SHL     BX,1
        SHL     BX,1
        PUSH    BX
        MOV     BX,WORD PTR HELP_LINKS[BX] ; BL = line, BH = column
        XOR     AX,AX
        MOV     AL,BH
        PUSH    AX
        XOR     BH,BH
        CALL    HELP_LINE
        POP     AX
        POP     BX
        CALL    HELP_SKIP_TO_COLUMN     ; SI = the cell at column AL
        MOV     AL,[SI]
        AND     AL,0DFH
        CMP     AL,DL
        JE      SHORT HELP_LETTER_FOUND
        DEC     CL
        JNZ     HELP_LETTER_NEXT
HELP_LETTER_DONE:
        RET
HELP_LETTER_FOUND:
        MOV     HELP_LINK,DH
        JMP     HELP_SHOW_FOCUS
HELP_LINK_BY_LETTER ENDP

; SI = a line: SI moves to its byte drawn in column AL (bold switches take
; no column).
HELP_SKIP_TO_COLUMN PROC NEAR
        PUSH    CX
        MOV     CL,AL
HELP_SKIP_NEXT:
        CMP     BYTE PTR [SI],1
        JNE     SHORT HELP_SKIP_COUNTED
        INC     SI
        JMP     HELP_SKIP_NEXT
HELP_SKIP_COUNTED:
        OR      CL,CL
        JZ      SHORT HELP_SKIP_DONE
        INC     SI
        DEC     CL
        JMP     HELP_SKIP_NEXT
HELP_SKIP_DONE:
        POP     CX
        RET
HELP_SKIP_TO_COLUMN ENDP

; The view scrolls so the focused link's line shows.
HELP_SHOW_FOCUS PROC NEAR
        CMP     BYTE PTR HELP_LINK,0FFH
        JE      SHORT HELP_SHOW_DONE
        CALL    HELP_FOCUSED_ENTRY
        XOR     AX,AX
        MOV     AL,[BX]
        CMP     AX,HELP_TOP
        JB      SHORT HELP_SHOW_SET
        XOR     CX,CX
        MOV     CL,HELP_WINDOW_ROWS
        MOV     DX,HELP_TOP
        ADD     DX,CX
        CMP     AX,DX
        JB      SHORT HELP_SHOW_DONE
        SUB     AX,CX
        INC     AX
HELP_SHOW_SET:
        MOV     HELP_TOP,AX
HELP_SHOW_DONE:
        RET
HELP_SHOW_FOCUS ENDP

; BX lines down (negative: up), within the topic.
HELP_SCROLL PROC NEAR
        MOV     AX,HELP_TOP
        ADD     AX,BX
        JNS     SHORT HELP_SCROLL_NOT_ABOVE
        XOR     AX,AX
HELP_SCROLL_NOT_ABOVE:
        XOR     CX,CX
        MOV     CL,HELP_WINDOW_ROWS
        MOV     DX,HELP_LINES
        SUB     DX,CX
        JNC     SHORT HELP_SCROLL_HAVE_LAST
        XOR     DX,DX
HELP_SCROLL_HAVE_LAST:
        CMP     AX,DX
        JBE     SHORT HELP_SCROLL_SET
        MOV     AX,DX
HELP_SCROLL_SET:
        MOV     HELP_TOP,AX
        RET
HELP_SCROLL ENDP

; Carry clear: the click at DH:DL is on link AL.
HELP_LINK_AT PROC NEAR
        XOR     CX,CX
        MOV     CL,HELP_LINK_COUNT
        JCXZ    HELP_LINK_AT_MISS
        MOV     BX,OFFSET HELP_LINKS
        XOR     AX,AX
        MOV     AL,DH
        SUB     AL,2
        ADD     AX,HELP_TOP             ; the line clicked
        MOV     AH,0
HELP_LINK_AT_NEXT:
        CMP     AL,[BX]
        JNE     SHORT HELP_LINK_AT_SKIP
        MOV     DH,[BX+1]               ; 11h mark to 10h mark around the text
        CMP     DL,DH
        JB      SHORT HELP_LINK_AT_SKIP
        ADD     DH,[BX+2]
        INC     DH
        CMP     DL,DH
        JA      SHORT HELP_LINK_AT_SKIP
        MOV     AL,AH
        CLC
        RET
HELP_LINK_AT_SKIP:
        ADD     BX,4
        INC     AH
        LOOP    HELP_LINK_AT_NEXT
HELP_LINK_AT_MISS:
        STC
        RET
HELP_LINK_AT ENDP

; Help's window over the rows above the editor's (DRAW_SCREEN has drawn the
; editor's window below it), the menu bar, and the active window's status
; line. The row offset is 0 afterwards.
HELP_DRAW_WINDOW PROC NEAR
        XOR     AL,AL
        CALL    SET_ROW_OFFSET
        CMP     BYTE PTR HELP_ACTIVE,0
        JE      SHORT HELP_DRAW_FOCUS_KEPT
        CALL    HELP_SHOW_FOCUS
HELP_DRAW_FOCUS_KEPT:
        MOV     AL,' '                  ; the menu bar stays on top
        MOV     BL,70H
        XOR     DX,DX
        MOV     CX,80
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET MENU_TEXT
        CALL    DOSUI_WRITE_Z
        MOV     DH,STATUS_ROW
        CMP     BYTE PTR HELP_ACTIVE,0
        JNE     SHORT HELP_DRAW_HELP_STATUS
        CALL    DRAW_EDITOR_STATUS
        JMP     SHORT HELP_DRAW_FRAME
HELP_DRAW_HELP_STATUS:
        MOV     AL,' '
        MOV     BL,3FH
        XOR     DL,DL
        MOV     CX,80
        CALL    DOSUI_FILL_ROW
        MOV     SI,OFFSET HELP_STATUS_TEXT
        CALL    DOSUI_WRITE_Z
HELP_DRAW_FRAME:
        MOV     AL,0C4H                 ; the window's top: its title centred,
        MOV     BL,07H                  ; lit while Help is active
        MOV     DH,1
        XOR     DL,DL
        MOV     CX,80
        CALL    DOSUI_FILL_ROW
        MOV     AL,0DAH
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0BFH
        MOV     DL,79
        CALL    DOSUI_FILL_ROW
        CALL    HELP_MAKE_TITLE         ; SI, CX
        MOV     DL,79
        SUB     DL,CL
        SHR     DL,1
        MOV     BL,70H
        CMP     BYTE PTR HELP_ACTIVE,0
        JNE     SHORT HELP_DRAW_TITLE
        MOV     BL,07H
HELP_DRAW_TITLE:
        CALL    DOSUI_WRITE_Z
        MOV     DH,2
        MOV     BX,HELP_TOP
HELP_DRAW_ROW:
        MOV     AL,HELP_WINDOW_ROWS
        INC     AL
        CMP     DH,AL
        JA      SHORT HELP_DRAW_ROWS_DONE
        PUSH    BX
        MOV     AL,0B3H
        MOV     BL,07H
        XOR     DL,DL
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,' '
        INC     DL
        MOV     CX,78
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B3H                 ; the right edge: a scroll bar below
        MOV     DL,79                   ; while Help is active
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        POP     BX
        CALL    HELP_LINE
        JC      SHORT HELP_DRAW_NEXT_ROW
        MOV     AL,07H
        CALL    HELP_DRAW_LINE
HELP_DRAW_NEXT_ROW:
        INC     BX
        INC     DH
        JMP     HELP_DRAW_ROW
HELP_DRAW_ROWS_DONE:
        CMP     BYTE PTR HELP_ACTIVE,0
        JE      SHORT HELP_DRAW_DONE
        CALL    HELP_DRAW_SCROLL_BAR
HELP_DRAW_DONE:
        RET
HELP_DRAW_WINDOW ENDP

; The window's right edge: an arrow at either end, the shaded track and its
; thumb, which follows the focused link's line (the top line without one).
HELP_DRAW_SCROLL_BAR PROC NEAR
        MOV     DL,79
        MOV     CX,1
        MOV     BL,70H
        MOV     DH,2
        MOV     AL,18H
        CALL    DOSUI_FILL_ROW
        MOV     DH,HELP_WINDOW_ROWS
        INC     DH
        CMP     DH,3
        JB      SHORT HELP_BAR_DONE
        MOV     AL,19H
        CALL    DOSUI_FILL_ROW
        MOV     AL,0B0H
HELP_BAR_TRACK:
        DEC     DH
        CMP     DH,3
        JB      SHORT HELP_BAR_THUMB
        CALL    DOSUI_FILL_ROW
        JMP     HELP_BAR_TRACK
HELP_BAR_THUMB:
        MOV     AX,HELP_TOP
        CMP     BYTE PTR HELP_LINK,0FFH
        JE      SHORT HELP_BAR_HAVE_LINE
        CALL    HELP_FOCUSED_ENTRY
        XOR     AX,AX
        MOV     AL,[BX]
HELP_BAR_HAVE_LINE:
        XOR     CX,CX                   ; 3 + line * (rows - 3) / (lines - 1)
        MOV     CL,HELP_WINDOW_ROWS
        SUB     CX,3
        JBE     SHORT HELP_BAR_DONE
        MUL     CX
        MOV     CX,HELP_LINES
        DEC     CX
        JZ      SHORT HELP_BAR_DONE
        DIV     CX
        ADD     AL,3
        MOV     DH,AL
        MOV     DL,79
        MOV     AL,' '
        MOV     BL,07H
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
HELP_BAR_DONE:
        RET
HELP_DRAW_SCROLL_BAR ENDP

; The cursor on the focused link's first letter, else on the window's
; first text cell.
HELP_PLACE_CURSOR PROC NEAR
        MOV     DX,0201H
        CMP     BYTE PTR HELP_LINK,0FFH
        JE      SHORT HELP_PLACE_SET
        CALL    HELP_FOCUSED_ENTRY
        XOR     AX,AX
        MOV     AL,[BX]
        SUB     AX,HELP_TOP
        ADD     AL,2
        MOV     DH,AL
        MOV     DL,[BX+1]
        INC     DL                      ; past the window's frame
HELP_PLACE_SET:
        CALL    DOSUI_SET_CURSOR
        CALL    DOSUI_SHOW_CURSOR
        RET
HELP_PLACE_CURSOR ENDP

; SI = the line to draw at row DH from column 1 (DIALOG_HELP: from DL),
; AL = its plain attribute: 01h toggles bold (the attribute's foreground
; made bright white), a link's 11h/10h marks are bright green, a link to the topic
; shown is drawn bold, the rule line is a run of C4h.
HELP_DRAW_LINE PROC NEAR
        MOV     DL,1
HELP_DRAW_LINE_AT:
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     BL,AL
        MOV     BH,AL                   ; BH = the plain attribute
        MOV     CX,1
        CMP     BYTE PTR [SI],2
        JNE     SHORT HELP_DRAW_CHARACTER
        MOV     AL,0C4H
        MOV     CX,78
        CALL    DOSUI_FILL_ROW
        JMP     SHORT HELP_DRAW_LINE_DONE
HELP_DRAW_CHARACTER:
        LODSB
        OR      AL,AL
        JZ      SHORT HELP_DRAW_LINE_DONE
        CMP     AL,1
        JNE     SHORT HELP_DRAW_NOT_BOLD
        XOR     BL,BH                   ; 0 when plain
        JZ      SHORT HELP_DRAW_PLAIN
        MOV     BL,BH
        JMP     HELP_DRAW_CHARACTER
HELP_DRAW_PLAIN:
        MOV     BL,BH
        OR      BL,0FH
        JMP     HELP_DRAW_CHARACTER
HELP_DRAW_NOT_BOLD:
        CMP     AL,11H
        JNE     SHORT HELP_DRAW_NOT_LINK
        PUSH    CX
        CALL    HELP_LINK_TARGET
        POP     CX
        CMP     AL,HELP_TOPIC
        MOV     AL,11H
        JNE     SHORT HELP_DRAW_LINK_MARK
        MOV     BL,BH
        OR      BL,0FH                  ; the topic shown: bold, 10h mark ends it
        JMP     SHORT HELP_DRAW_PUT
HELP_DRAW_NOT_LINK:
        CMP     AL,10H
        JNE     SHORT HELP_DRAW_PUT
        CMP     BL,BH
        JNE     SHORT HELP_DRAW_LINK_END
HELP_DRAW_LINK_MARK:
        PUSH    BX
        AND     BL,0F0H
        OR      BL,0AH
        CALL    DOSUI_FILL_ROW
        POP     BX
        INC     DL
        JMP     HELP_DRAW_CHARACTER
HELP_DRAW_LINK_END:
        CALL    DOSUI_FILL_ROW
        MOV     BL,BH
        INC     DL
        JMP     HELP_DRAW_CHARACTER
HELP_DRAW_PUT:
        CALL    DOSUI_FILL_ROW
        INC     DL
        JMP     HELP_DRAW_CHARACTER
HELP_DRAW_LINE_DONE:
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        RET
HELP_DRAW_LINE ENDP

; SI = " HELP: <current topic> " in HELP_TITLE, CX its length.
HELP_MAKE_TITLE PROC NEAR
        PUSH    AX
        PUSH    DI
        PUSH    ES
        PUSH    DS
        POP     ES
        MOV     SI,OFFSET HELP_TITLE_PREFIX
        MOV     DI,OFFSET HELP_TITLE
        CALL    HELP_COPY_Z
        XOR     AX,AX
        MOV     AL,HELP_TOPIC
        SHL     AX,1
        MOV     SI,AX
        MOV     SI,HELP_TOPICS[SI]
        CALL    HELP_COPY_Z
        MOV     AX,' '
        STOSW
        POP     ES
        POP     DI
        POP     AX
        MOV     SI,OFFSET HELP_TITLE
        JMP     STRING_LENGTH
HELP_MAKE_TITLE ENDP

HELP_COPY_Z PROC NEAR
        LODSB
        OR      AL,AL
        JZ      SHORT HELP_COPY_Z_DONE
        STOSB
        JMP     HELP_COPY_Z
HELP_COPY_Z_DONE:
        RET
HELP_COPY_Z ENDP

; A dialog's Help (DOS 5: a box at columns 5-72, as tall as its topic and
; centred, "HELP: <topic>" in its top edge, the text framed inside,
; <  OK  > below it; Enter, Esc and a click on OK close it, Tab moves the
; cursor between the text and OK, the arrows and PgUp/PgDn scroll). AL =
; the topic; the screen under the box is the caller's to redraw.
DIALOG_HELP PROC NEAR
        CALL    BOX_ROWS_DIALOG
        CALL    HELP_STATE_SAVE         ; Help's window keeps its topic
        CALL    HELP_SELECT_TOPIC
        MOV     BYTE PTR DIALOG_HELP_FOCUS,0
        MOV     AX,HELP_LINES           ; the topic's lines past its links
        SUB     AX,2
        CMP     AX,DIALOG_HELP_ROWS_MAX
        JBE     SHORT DIALOG_HELP_HAVE_ROWS
        MOV     AX,DIALOG_HELP_ROWS_MAX
DIALOG_HELP_HAVE_ROWS:
        MOV     HELP_WINDOW_ROWS,AL
        MOV     AH,25-6                 ; centred: (25 - (rows + 6)) / 2
        SUB     AH,AL
        SHR     AH,1
        MOV     DIALOG_HELP_RECT,AH
        ADD     AH,AL
        ADD     AH,5
        MOV     DIALOG_HELP_RECT+2,AH
        MOV     WORD PTR HELP_TOP,2
DIALOG_HELP_DRAW:
        MOV     SI,OFFSET DIALOG_HELP_RECT
        CALL    DOSUI_MENU_BOX
        CALL    HELP_MAKE_TITLE
        MOV     DL,78
        SUB     DL,CL
        SHR     DL,1
        MOV     DH,DIALOG_HELP_RECT
        MOV     BL,70H
        CALL    DOSUI_WRITE_Z
        INC     DH                      ; the text's frame
        MOV     AL,0C4H
        MOV     DL,8
        MOV     CX,62
        CALL    DOSUI_FILL_ROW
        MOV     AL,0DAH
        MOV     DL,7
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0BFH
        MOV     DL,70
        CALL    DOSUI_FILL_ROW
        MOV     BX,HELP_TOP
        MOV     CL,HELP_WINDOW_ROWS
DIALOG_HELP_ROW:
        INC     DH
        PUSH    CX
        MOV     AL,0B3H
        MOV     DL,7
        MOV     CX,1
        PUSH    BX
        MOV     BL,70H
        CALL    DOSUI_FILL_ROW
        MOV     DL,70
        CALL    DOSUI_FILL_ROW
        POP     BX
        CALL    HELP_LINE
        JC      SHORT DIALOG_HELP_NEXT_ROW
        MOV     AL,70H
        MOV     DL,9
        CALL    HELP_DRAW_LINE_AT
DIALOG_HELP_NEXT_ROW:
        POP     CX
        INC     BX
        DEC     CL
        JNZ     DIALOG_HELP_ROW
        INC     DH
        MOV     BL,70H
        MOV     AL,0C4H
        MOV     DL,8
        MOV     CX,62
        CALL    DOSUI_FILL_ROW
        MOV     AL,0C0H
        MOV     DL,7
        MOV     CX,1
        CALL    DOSUI_FILL_ROW
        MOV     AL,0D9H
        MOV     DL,70
        CALL    DOSUI_FILL_ROW
        INC     DH
        MOV     DL,5
        MOV     CX,68
        CALL    DOSUI_MENU_SEPARATOR
        INC     DH
        MOV     SI,OFFSET ABOUT_BUTTON_TEXT
        MOV     DL,35
        CALL    DOSUI_WRITE_Z
        CALL    BRIGHT_BRACKETS
        MOV     DL,38                   ; OK focused: its "O"
        CMP     BYTE PTR DIALOG_HELP_FOCUS,0
        JNE     SHORT DIALOG_HELP_CURSOR
        MOV     DH,DIALOG_HELP_RECT     ; the text focused: its first line
        ADD     DH,2
        MOV     DL,10
DIALOG_HELP_CURSOR:
        CALL    DOSUI_SET_CURSOR
        CALL    DOSUI_SHOW_CURSOR
DIALOG_HELP_KEY:
        CALL    DOSUI_READ_EVENT
        JC      SHORT DIALOG_HELP_MOUSE
        CMP     AL,27
        JE      SHORT DIALOG_HELP_DONE
        CMP     AL,13
        JE      SHORT DIALOG_HELP_DONE
        CMP     AL,9
        JE      SHORT DIALOG_HELP_TOGGLE
        OR      AL,AL
        JNZ     DIALOG_HELP_KEY
        CMP     AH,0FH
        JE      SHORT DIALOG_HELP_TOGGLE
        MOV     BX,1
        CMP     AH,50H
        JE      SHORT DIALOG_HELP_SCROLL
        CMP     AH,51H
        JE      SHORT DIALOG_HELP_PAGE
        NEG     BX
        CMP     AH,48H
        JE      SHORT DIALOG_HELP_SCROLL
        CMP     AH,49H
        JNE     DIALOG_HELP_KEY
DIALOG_HELP_PAGE:
        MOV     AL,HELP_WINDOW_ROWS
        CBW
        IMUL    BX
        MOV     BX,AX
DIALOG_HELP_SCROLL:
        MOV     AX,HELP_TOP             ; lines 2 .. lines - rows
        ADD     AX,BX
        CMP     AX,2
        JGE     SHORT DIALOG_HELP_NOT_ABOVE
        MOV     AX,2
DIALOG_HELP_NOT_ABOVE:
        XOR     CX,CX
        MOV     CL,HELP_WINDOW_ROWS
        MOV     DX,HELP_LINES
        SUB     DX,CX
        CMP     AX,DX
        JLE     SHORT DIALOG_HELP_SCROLLED
        MOV     AX,DX
DIALOG_HELP_SCROLLED:
        CMP     AX,2
        JGE     SHORT DIALOG_HELP_SET_TOP
        MOV     AX,2
DIALOG_HELP_SET_TOP:
        MOV     HELP_TOP,AX
        JMP     DIALOG_HELP_DRAW
DIALOG_HELP_TOGGLE:
        XOR     BYTE PTR DIALOG_HELP_FOCUS,1
        JMP     DIALOG_HELP_DRAW
DIALOG_HELP_MOUSE:
        MOV     AL,DIALOG_HELP_RECT+2
        DEC     AL
        CMP     DH,AL
        JNE     DIALOG_HELP_KEY
        MOV     AL,35
        CALL    BUTTON_HIT
        JC      DIALOG_HELP_KEY
DIALOG_HELP_DONE:
        CALL    HELP_STATE_RESTORE
        CALL    DOSUI_SHOW_CURSOR
        JMP     BOX_ROWS_RESTORE
DIALOG_HELP ENDP

; A dialog's Help button and F1 in a dialog: the dialog-box topic.
SHOW_KEYBOARD_HELP PROC NEAR
        MOV     AL,HELP_TOPIC_DIALOGS
        JMP     DIALOG_HELP
SHOW_KEYBOARD_HELP ENDP

; DOS 5's "Device unavailable" box for a drive that does not exist:
; <Retry> (the default), <Cancel>, < Help >, Tab and the arrows moving the
; focus, Esc cancelling. Carry set: Retry.
SHOW_DEVICE_UNAVAILABLE PROC NEAR
        MOV     BYTE PTR DEVICE_ERROR_FOCUS,0
SHOW_DEVICE_DRAW:
        CALL    DRAW_DIALOG_STATUS
        MOV     SI,OFFSET DEVICE_ERROR_RECT
        CALL    DOSUI_MENU_BOX
        MOV     SI,OFFSET DEVICE_ERROR_TEXT
        MOV     BL,70H
        MOV     DH,11
        MOV     DL,31
        CALL    DOSUI_WRITE_Z
        MOV     DH,13
        MOV     DL,22
        MOV     CX,35
        CALL    DOSUI_MENU_SEPARATOR
SHOW_DEVICE_FOCUS:
        MOV     SI,OFFSET DEVICE_ERROR_BUTTONS_TEXT
        MOV     BL,70H
        MOV     DH,14
        MOV     DL,25
        CALL    DOSUI_WRITE_Z
        XOR     BX,BX
        MOV     BL,DEVICE_ERROR_FOCUS
        SHL     BX,1
        MOV     AX,WORD PTR DEVICE_ERROR_BRACKETS[BX]
        PUSH    AX
        MOV     DL,AL                   ; the focused button's brackets
        MOV     AL,'<'
        MOV     BL,7FH
        MOV     CX,1
        MOV     DH,14
        CALL    DOSUI_FILL_ROW
        POP     AX
        MOV     DL,AH
        MOV     AL,'>'
        CALL    DOSUI_FILL_ROW
        XOR     BX,BX
        MOV     BL,DEVICE_ERROR_FOCUS
        MOV     DL,DEVICE_ERROR_CURSORS[BX]
        CALL    DOSUI_SET_CURSOR
        CALL    DOSUI_SHOW_CURSOR
SHOW_DEVICE_KEY:
        CALL    DOSUI_READ_EVENT
        JC      SHOW_DEVICE_KEY
        CMP     AL,27
        JE      SHORT SHOW_DEVICE_CANCEL
        CMP     AL,13
        JE      SHORT SHOW_DEVICE_ENTER
        CMP     AL,9
        JE      SHORT SHOW_DEVICE_NEXT
        CMP     AH,4DH                  ; Right
        JE      SHORT SHOW_DEVICE_NEXT
        CMP     AH,0FH                  ; Shift+Tab
        JE      SHORT SHOW_DEVICE_PREVIOUS
        CMP     AH,4BH                  ; Left
        JNE     SHOW_DEVICE_KEY
SHOW_DEVICE_PREVIOUS:
        DEC     BYTE PTR DEVICE_ERROR_FOCUS
        JNS     SHORT SHOW_DEVICE_MOVED
        MOV     BYTE PTR DEVICE_ERROR_FOCUS,2
        JMP     SHORT SHOW_DEVICE_MOVED
SHOW_DEVICE_NEXT:
        INC     BYTE PTR DEVICE_ERROR_FOCUS
        CMP     BYTE PTR DEVICE_ERROR_FOCUS,3
        JB      SHORT SHOW_DEVICE_MOVED
        MOV     BYTE PTR DEVICE_ERROR_FOCUS,0
SHOW_DEVICE_MOVED:
        JMP     SHOW_DEVICE_FOCUS
SHOW_DEVICE_ENTER:
        CMP     BYTE PTR DEVICE_ERROR_FOCUS,1
        JE      SHORT SHOW_DEVICE_CANCEL
        JA      SHORT SHOW_DEVICE_HELP
        STC
        RET
SHOW_DEVICE_HELP:
        CALL    SHOW_KEYBOARD_HELP
        JMP     SHOW_DEVICE_DRAW
SHOW_DEVICE_CANCEL:
        CLC
        RET
SHOW_DEVICE_UNAVAILABLE ENDP

; A file Open could not read: its message over the editor.
SHOW_STARTUP_ERROR PROC NEAR
        MOV     BYTE PTR STARTUP_ERROR,0
        JMP     SHOW_FILE_ERROR
SHOW_STARTUP_ERROR ENDP

SHOW_SAVED PROC NEAR
        CALL    BOX_ROWS_DIALOG
        MOV     SI,OFFSET SAVED_RECT
        CALL    DOSUI_DRAW_MODAL
        MOV     SI,OFFSET SAVED_TEXT
        MOV     BL,2FH
        MOV     DH,11
        MOV     DL,31
        CALL    DOSUI_WRITE_Z
        JMP     BOX_ROWS_RESTORE
SHOW_SAVED ENDP

; A box of its own over whatever is up: centred like a dialog, or (help) at
; the top as before; BOX_ROWS_RESTORE puts back the rows that were in use
; and returns for the box, AX and FLAGS kept.
BOX_ROWS_DIALOG PROC NEAR
        PUSH    AX
        MOV     AL,DOSUI_ROW_OFFSET
        MOV     BOX_SAVED_OFFSET,AL
        MOV     AL,DIALOG_ROW_OFFSET
        CALL    SET_ROW_OFFSET
        POP     AX
        RET
BOX_ROWS_DIALOG ENDP

BOX_ROWS_TOP PROC NEAR
        PUSH    AX
        MOV     AL,DOSUI_ROW_OFFSET
        MOV     BOX_SAVED_OFFSET,AL
        XOR     AL,AL
        CALL    SET_ROW_OFFSET
        POP     AX
        RET
BOX_ROWS_TOP ENDP

BOX_ROWS_RESTORE PROC NEAR
        PUSHF
        PUSH    AX
        MOV     AL,BOX_SAVED_OFFSET
        CALL    SET_ROW_OFFSET
        POP     AX
        POPF
        RET
BOX_ROWS_RESTORE ENDP

CONFIRM_EXIT PROC NEAR
        MOV     BYTE PTR CONFIRM_FOCUS,0
CONFIRM_EXIT_DRAW:
        MOV     SI,OFFSET EXIT_TEXT
        MOV     DL,21
        CALL    DRAW_FILE_CONFIRMATION
        CALL    READ_FILE_CONFIRMATION
        CMP     AL,4
        JNE     SHORT CONFIRM_EXIT_ANSWER
        CALL    SHOW_KEYBOARD_HELP
        JMP     CONFIRM_EXIT_DRAW
CONFIRM_EXIT_ANSWER:
        CMP     AL,3
        JNE     SHORT CONFIRM_EXIT_DONE
        XOR     AL,AL
CONFIRM_EXIT_DONE:
        RET
CONFIRM_EXIT ENDP

; ---- The document's memory -------------------------------------------
; The text lives in one DOS memory block taken at START, as large as DOS
; has (ARENA_SEG, ARENA_SIZE bytes), so a file may fill all of conventional
; memory. Every editing routine works on the view: whole lines of the text
; copied into the program's own segment at BUFFER_BASE (BUFFER_LENGTH bytes,
; at most BUFFER_MAX), with 16-bit offsets as before. The block holds the
; rest: the text before the view at its start (PREFIX_LEN bytes, VIEW_LINE
; line ends), the text after the view below its top (SUFFIX_LEN bytes,
; SUFFIX_LINES line ends), and the clipboard at its very top (CLIP_LEN of
; CLIP_CAP bytes). The text always fits the block as a whole, so the view can
; be written back at any time ("flushed"): the document is then the block's
; two runs, and the whole-text commands (Save, Print, Find past the view,
; Change, the clipboard beyond the view, indentation of a large selection,
; jumps) work there and build a view around the cursor again. Between two
; commands NORMALIZE_VIEW makes sure the view holds enough lines around the
; cursor for the next one (a page above the window, two below the cursor) and
; room to type into. A line may be up to LINE_LIMIT bytes long; a file with a
; longer line, or larger than the block, is not loaded ("Out of memory", as
; real DOS 5 EDIT answers both).

; DX:AX = an offset in the document block: AX returns its paragraph, DX the
; rest (0-15).
ARENA_ADDRESS PROC NEAR
        PUSH    BX
        PUSH    CX
        MOV     BX,AX
        AND     BX,0FH
        MOV     CL,4
        SHR     AX,CL
        MOV     CL,12
        SHL     DX,CL
        OR      AX,DX
        ADD     AX,ARENA_SEG
        MOV     DX,BX
        POP     CX
        POP     BX
        RET
ARENA_ADDRESS ENDP

; DX:AX = an offset in the document block: ES:DI addresses it.
ARENA_ES_DI PROC NEAR
        PUSH    AX
        PUSH    DX
        CALL    ARENA_ADDRESS
        MOV     ES,AX
        MOV     DI,DX
        POP     DX
        POP     AX
        RET
ARENA_ES_DI ENDP

; Move MOVE_LEN bytes of the block from MOVE_SRC to MOVE_DST (offsets, all
; double words); the two ranges may overlap either way. MOVE_* are used up.
ARENA_MOVE PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        MOV     AX,MOVE_DST
        MOV     DX,MOVE_DST+2
        CMP     DX,MOVE_SRC+2
        JNE     ARENA_MOVE_ORDERED
        CMP     AX,MOVE_SRC
ARENA_MOVE_ORDERED:
        JA      ARENA_MOVE_BACKWARD     ; to higher offsets: from the end
ARENA_MOVE_FORWARD:
        CALL    ARENA_MOVE_CHUNK
        OR      CX,CX
        JNZ     ARENA_MOVE_FORWARD_CHUNK
        JMP     ARENA_MOVE_DONE
ARENA_MOVE_FORWARD_CHUNK:
        MOV     AX,MOVE_SRC
        MOV     DX,MOVE_SRC+2
        CALL    ARENA_ADDRESS
        MOV     BX,AX
        MOV     SI,DX
        MOV     AX,MOVE_DST
        MOV     DX,MOVE_DST+2
        CALL    ARENA_ES_DI
        PUSH    CX
        PUSH    DS
        MOV     DS,BX
        REP     MOVSB
        POP     DS
        POP     CX
        ADD     MOVE_SRC,CX
        ADC     WORD PTR MOVE_SRC+2,0
        ADD     MOVE_DST,CX
        ADC     WORD PTR MOVE_DST+2,0
        SUB     MOVE_LEN,CX
        SBB     WORD PTR MOVE_LEN+2,0
        JMP     ARENA_MOVE_FORWARD
ARENA_MOVE_BACKWARD:
        CALL    ARENA_MOVE_CHUNK
        OR      CX,CX
        JNZ     ARENA_MOVE_BACK_CHUNK
        JMP     ARENA_MOVE_DONE
ARENA_MOVE_BACK_CHUNK:
        SUB     MOVE_LEN,CX
        SBB     WORD PTR MOVE_LEN+2,0
        MOV     AX,MOVE_SRC
        MOV     DX,MOVE_SRC+2
        ADD     AX,MOVE_LEN
        ADC     DX,MOVE_LEN+2
        CALL    ARENA_ADDRESS
        MOV     BX,AX
        MOV     SI,DX
        ADD     SI,CX
        DEC     SI
        MOV     AX,MOVE_DST
        MOV     DX,MOVE_DST+2
        ADD     AX,MOVE_LEN
        ADC     DX,MOVE_LEN+2
        CALL    ARENA_ES_DI
        ADD     DI,CX
        DEC     DI
        PUSH    DS
        MOV     DS,BX
        STD
        REP     MOVSB
        CLD
        POP     DS
        JMP     ARENA_MOVE_BACKWARD
ARENA_MOVE_DONE:
        POP     ES
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
ARENA_MOVE ENDP

; CX = the next piece of MOVE_LEN: at most 8000h bytes, 0 when none is left.
ARENA_MOVE_CHUNK PROC NEAR
        MOV     CX,8000H
        CMP     WORD PTR MOVE_LEN+2,0
        JNE     ARENA_MOVE_CHUNK_DONE
        CMP     MOVE_LEN,CX
        JAE     ARENA_MOVE_CHUNK_DONE
        MOV     CX,MOVE_LEN
ARENA_MOVE_CHUNK_DONE:
        RET
ARENA_MOVE_CHUNK ENDP

; DX:AX = a block offset, COUNT_LEN = a length: DX:AX returns the line
; ends (LF) in those bytes. COUNT_LEN is used up.
ARENA_COUNT_LF PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DI
        PUSH    ES
        PUSH    BP
        XOR     BX,BX
        XOR     BP,BP
ARENA_COUNT_NEXT:
        MOV     CX,8000H
        CMP     WORD PTR COUNT_LEN+2,0
        JNE     ARENA_COUNT_CHUNK
        CMP     COUNT_LEN,CX
        JAE     ARENA_COUNT_CHUNK
        MOV     CX,COUNT_LEN
        JCXZ    ARENA_COUNT_DONE
ARENA_COUNT_CHUNK:
        CALL    ARENA_ES_DI
        SUB     COUNT_LEN,CX
        SBB     WORD PTR COUNT_LEN+2,0
        ADD     AX,CX
        ADC     DX,0
        PUSH    AX
        MOV     AL,10
ARENA_COUNT_SCAN:
        JCXZ    ARENA_COUNT_SCANNED
        REPNE   SCASB
        JNE     ARENA_COUNT_SCANNED
        ADD     BX,1
        ADC     BP,0
        JMP     ARENA_COUNT_SCAN
ARENA_COUNT_SCANNED:
        POP     AX
        JMP     ARENA_COUNT_NEXT
ARENA_COUNT_DONE:
        MOV     AX,BX
        MOV     DX,BP
        POP     BP
        POP     ES
        POP     DI
        POP     CX
        POP     BX
        RET
ARENA_COUNT_LF ENDP

; DX:AX = a block offset, COUNT_LEN = a length: carry when a line in those
; bytes (the last one runs to their end) is longer than LINE_LIMIT.
ARENA_LONG_LINE PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        XOR     BX,BX                   ; the line's length so far
ARENA_LONG_NEXT:
        MOV     CX,8000H
        CMP     WORD PTR COUNT_LEN+2,0
        JNE     ARENA_LONG_CHUNK
        CMP     COUNT_LEN,CX
        JAE     ARENA_LONG_CHUNK
        MOV     CX,COUNT_LEN
        JCXZ    ARENA_LONG_FITS
ARENA_LONG_CHUNK:
        CALL    ARENA_ES_DI
        SUB     COUNT_LEN,CX
        SBB     WORD PTR COUNT_LEN+2,0
        ADD     AX,CX
        ADC     DX,0
        PUSH    AX
ARENA_LONG_SCAN:
        JCXZ    ARENA_LONG_SCANNED
        MOV     SI,CX
        MOV     AL,10
        REPNE   SCASB
        PUSHF
        SUB     SI,CX                   ; the bytes passed, an LF included
        POPF
        JNE     ARENA_LONG_TAIL
        DEC     SI
        ADD     BX,SI
        JC      ARENA_LONG_TOO_LONG
        CMP     BX,LINE_LIMIT
        JA      ARENA_LONG_TOO_LONG
        XOR     BX,BX
        JMP     ARENA_LONG_SCAN
ARENA_LONG_TAIL:
        ADD     BX,SI
        JC      ARENA_LONG_TOO_LONG
        CMP     BX,LINE_LIMIT
        JA      ARENA_LONG_TOO_LONG
ARENA_LONG_SCANNED:
        POP     AX
        JMP     ARENA_LONG_NEXT
ARENA_LONG_FITS:
        CLC
        JMP     SHORT ARENA_LONG_DONE
ARENA_LONG_TOO_LONG:
        POP     AX
        STC
ARENA_LONG_DONE:
        POP     ES
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
ARENA_LONG_LINE ENDP

; AX = the line ends in the view.
VIEW_COUNT_LF PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DI
        PUSH    ES
        PUSH    SS
        POP     ES
        MOV     DI,BUFFER_BASE
        MOV     CX,BUFFER_LENGTH
        XOR     BX,BX
        MOV     AL,10
VIEW_COUNT_SCAN:
        JCXZ    VIEW_COUNT_DONE
        REPNE   SCASB
        JNE     VIEW_COUNT_DONE
        INC     BX
        JMP     VIEW_COUNT_SCAN
VIEW_COUNT_DONE:
        MOV     AX,BX
        POP     ES
        POP     DI
        POP     CX
        POP     BX
        RET
VIEW_COUNT_LF ENDP

; DX:AX = where the text after the view starts in the block.
SUFFIX_START PROC NEAR
        MOV     AX,ARENA_SIZE
        MOV     DX,ARENA_SIZE+2
        SUB     AX,CLIP_CAP
        SBB     DX,CLIP_CAP+2
        SUB     AX,SUFFIX_LEN
        SBB     DX,SUFFIX_LEN+2
        RET
SUFFIX_START ENDP

; DX:AX = where the clipboard's text starts in the block.
CLIP_BASE PROC NEAR
        MOV     AX,ARENA_SIZE
        MOV     DX,ARENA_SIZE+2
        SUB     AX,CLIP_CAP
        SBB     DX,CLIP_CAP+2
        RET
CLIP_BASE ENDP

; DX:AX = the document's length.
DOC_LENGTH PROC NEAR
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        ADD     AX,BUFFER_LENGTH
        ADC     DX,0
        ADD     AX,SUFFIX_LEN
        ADC     DX,SUFFIX_LEN+2
        RET
DOC_LENGTH ENDP

; DX:AX = the cursor as an offset in the whole text.
CURSOR_GLOBAL PROC NEAR
        MOV     AX,CURSOR_OFFSET
        XOR     DX,DX
        ADD     AX,PREFIX_LEN
        ADC     DX,PREFIX_LEN+2
        RET
CURSOR_GLOBAL ENDP

; DX:AX = the bytes the text may still grow by.
DOC_ROOM PROC NEAR
        CALL    SUFFIX_START
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        SUB     AX,BUFFER_LENGTH
        SBB     DX,0
        RET
DOC_ROOM ENDP

; AX = the bytes that may be added to the view now: its free space, and no
; more than the text may grow by (a clipboard larger than its text gives
; its spare room back first).
VIEW_ROOM PROC NEAR
        PUSH    BX
        PUSH    DX
        MOV     BX,BUFFER_MAX
        SUB     BX,BUFFER_LENGTH
        CALL    DOC_ROOM
        OR      DX,DX
        JNZ     VIEW_ROOM_VIEW
        CMP     AX,BX
        JAE     VIEW_ROOM_VIEW
        CALL    CLIP_RECLAIM
        CALL    DOC_ROOM
        OR      DX,DX
        JNZ     VIEW_ROOM_VIEW
        CMP     AX,BX
        JB      VIEW_ROOM_DONE
VIEW_ROOM_VIEW:
        MOV     AX,BX
VIEW_ROOM_DONE:
        POP     DX
        POP     BX
        RET
VIEW_ROOM ENDP

; CX = bytes a command is about to type: carry (and "Out of memory" at the
; next redraw) when they do not fit.
ROOM_FOR PROC NEAR
        PUSH    AX
        CALL    VIEW_ROOM
        CMP     AX,CX
        POP     AX
        JAE     ROOM_FOR_OK
        MOV     BYTE PTR MEMORY_FULL,1
        STC
        RET
ROOM_FOR_OK:
        CLC
        RET
ROOM_FOR ENDP

; Write the view back into the block after the text before it; the view is
; empty afterwards and the document is the block's two runs.
FLUSH_VIEW PROC NEAR
        PUSH    AX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        MOV     CX,BUFFER_LENGTH
        JCXZ    FLUSH_VIEW_DONE
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        CALL    ARENA_ES_DI
        MOV     SI,BUFFER_BASE
        PUSH    DS
        PUSH    SS
        POP     DS
        REP     MOVSB
        POP     DS
        CALL    VIEW_COUNT_LF
        ADD     VIEW_LINE,AX
        ADC     WORD PTR VIEW_LINE+2,0
        MOV     AX,BUFFER_LENGTH
        ADD     PREFIX_LEN,AX
        ADC     WORD PTR PREFIX_LEN+2,0
        MOV     WORD PTR BUFFER_LENGTH,0
FLUSH_VIEW_DONE:
        POP     ES
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     AX
        RET
FLUSH_VIEW ENDP

; With the view empty: DX:AX = where the text before the view is to end.
; The bytes between move from one run to the other.
SET_BOUNDARY PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     BX,AX
        MOV     CX,DX                   ; CX:BX = the new boundary
        CMP     CX,PREFIX_LEN+2
        JNE     SET_BOUNDARY_ORDERED
        CMP     BX,PREFIX_LEN
SET_BOUNDARY_ORDERED:
        JA      SET_BOUNDARY_FORWARD
        JB      SET_BOUNDARY_BACK
        JMP     SET_BOUNDARY_DONE
SET_BOUNDARY_BACK:                      ; [new, P) to the head of the suffix
        MOV     MOVE_SRC,BX
        MOV     MOVE_SRC+2,CX
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        SUB     AX,BX
        SBB     DX,CX
        MOV     MOVE_LEN,AX
        MOV     MOVE_LEN+2,DX
        MOV     COUNT_LEN,AX
        MOV     COUNT_LEN+2,DX
        ADD     SUFFIX_LEN,AX
        ADC     SUFFIX_LEN+2,DX
        MOV     PREFIX_LEN,BX
        MOV     PREFIX_LEN+2,CX
        CALL    SUFFIX_START
        MOV     MOVE_DST,AX
        MOV     MOVE_DST+2,DX
        MOV     AX,BX
        MOV     DX,CX
        CALL    ARENA_COUNT_LF
        SUB     VIEW_LINE,AX
        SBB     VIEW_LINE+2,DX
        ADD     SUFFIX_LINES,AX
        ADC     SUFFIX_LINES+2,DX
        CALL    ARENA_MOVE
        JMP     SET_BOUNDARY_DONE
SET_BOUNDARY_FORWARD:                   ; the head of the suffix to P
        CALL    SUFFIX_START            ; where it starts before the move
        MOV     MOVE_SRC,AX
        MOV     MOVE_SRC+2,DX
        PUSH    DX
        PUSH    AX
        MOV     AX,BX
        MOV     DX,CX
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        MOV     MOVE_LEN,AX
        MOV     MOVE_LEN+2,DX
        MOV     COUNT_LEN,AX
        MOV     COUNT_LEN+2,DX
        SUB     SUFFIX_LEN,AX
        SBB     SUFFIX_LEN+2,DX
        MOV     AX,PREFIX_LEN
        MOV     MOVE_DST,AX
        MOV     AX,PREFIX_LEN+2
        MOV     MOVE_DST+2,AX
        MOV     PREFIX_LEN,BX
        MOV     PREFIX_LEN+2,CX
        POP     AX
        POP     DX
        CALL    ARENA_COUNT_LF
        ADD     VIEW_LINE,AX
        ADC     VIEW_LINE+2,DX
        SUB     SUFFIX_LINES,AX
        SBB     SUFFIX_LINES+2,DX
        CALL    ARENA_MOVE
SET_BOUNDARY_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
SET_BOUNDARY ENDP

; With the view empty, from the boundary back: AX = the bytes back to the
; start of the line CX lines above the one the boundary is in (CX = 0: that
; line's own start), or to the furthest line start within BX bytes; BX
; itself when the boundary's own line is longer than that.
BACK_LINES PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        PUSH    BP
        XOR     BP,BP                   ; 1: the limit is the text's start,
        CMP     WORD PTR PREFIX_LEN+2,0 ; 2: the boundary's line start found
        JNE     BACK_LINES_LIMITED
        CMP     BX,PREFIX_LEN
        JB      BACK_LINES_LIMITED
        MOV     BX,PREFIX_LEN
        INC     BP
BACK_LINES_LIMITED:
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        SUB     AX,BX
        SBB     DX,0
        CALL    ARENA_ES_DI
        MOV     SI,DI
        ADD     SI,BX                   ; ES:SI = the boundary
        XOR     AX,AX
        XOR     DX,DX
        INC     CX
BACK_LINES_SCAN:
        CMP     AX,BX
        JAE     BACK_LINES_EDGE
        MOV     DI,SI
        SUB     DI,AX
        CMP     BYTE PTR ES:[DI-1],10
        JE      BACK_LINES_START
        INC     AX
        JMP     BACK_LINES_SCAN
BACK_LINES_START:
        MOV     DX,AX
        OR      BP,2
        DEC     CX
        JZ      BACK_LINES_DONE
        INC     AX
        JMP     BACK_LINES_SCAN
BACK_LINES_EDGE:
        TEST    BP,1
        JZ      BACK_LINES_CUT
        MOV     DX,AX                   ; the text's start begins a line
        JMP     SHORT BACK_LINES_DONE
BACK_LINES_CUT:
        TEST    BP,2
        JNZ     BACK_LINES_DONE
        MOV     DX,BX                   ; a line longer than the limit
BACK_LINES_DONE:
        MOV     AX,DX
        POP     BP
        POP     ES
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        RET
BACK_LINES ENDP

; Fill the empty view from the text after it: at least through the line
; holding byte FILL_MIN of that text, then up to FILL_LINES lines more,
; within FILL_BUDGET bytes, or all of it when it fits. A line longer than
; the budget comes in part.
FILL_VIEW PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    ES
        PUSH    BP
        CALL    SUFFIX_START
        CALL    ARENA_ES_DI
        MOV     CX,FILL_BUDGET
        XOR     BP,BP                   ; 1: FILL_MIN's line is in, 4: all fits
        CMP     WORD PTR SUFFIX_LEN+2,0
        JNE     FILL_VIEW_LIMITED
        CMP     CX,SUFFIX_LEN
        JB      FILL_VIEW_LIMITED
        MOV     CX,SUFFIX_LEN
        OR      BP,4
FILL_VIEW_LIMITED:
        XOR     BX,BX                   ; bytes looked at
        XOR     DX,DX                   ; bytes to take (a line boundary)
        XOR     SI,SI                   ; lines after FILL_MIN's
FILL_VIEW_SCAN:
        CMP     BX,CX
        JAE     FILL_VIEW_END
        INC     BX
        CMP     BYTE PTR ES:[DI+BX-1],10
        JNE     FILL_VIEW_SCAN
        MOV     DX,BX
        CMP     BX,FILL_MIN
        JBE     FILL_VIEW_SCAN
        TEST    BP,1
        JNZ     FILL_VIEW_EXTRA
        OR      BP,1
        JMP     FILL_VIEW_SCAN
FILL_VIEW_EXTRA:
        INC     SI
        CMP     SI,FILL_LINES
        JB      FILL_VIEW_SCAN
        JMP     SHORT FILL_VIEW_TAKE
FILL_VIEW_END:
        TEST    BP,4
        JZ      FILL_VIEW_PART
        MOV     DX,CX                   ; to the text's end
        JMP     SHORT FILL_VIEW_TAKE
FILL_VIEW_PART:
        TEST    BP,1
        JNZ     FILL_VIEW_TAKE
        MOV     DX,CX                   ; FILL_MIN's line is longer
FILL_VIEW_TAKE:
        MOV     CX,DX
        MOV     SI,DI
        MOV     AX,ES
        PUSH    SS
        POP     ES
        MOV     DI,BUFFER_BASE
        PUSH    DS
        MOV     DS,AX
        REP     MOVSB
        POP     DS
        MOV     BUFFER_LENGTH,DX
        SUB     SUFFIX_LEN,DX
        SBB     WORD PTR SUFFIX_LEN+2,0
        CALL    VIEW_COUNT_LF
        SUB     SUFFIX_LINES,AX
        SBB     WORD PTR SUFFIX_LINES+2,0
        POP     BP
        POP     ES
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
FILL_VIEW ENDP

; DX:AX = an offset in the whole text: AX = the same in the view, carry
; when it lies outside (AX is then the view's nearer end).
LOCAL_OFFSET PROC NEAR
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        JB      LOCAL_OFFSET_BEFORE
        OR      DX,DX
        JNZ     LOCAL_OFFSET_AFTER
        CMP     AX,BUFFER_LENGTH
        JA      LOCAL_OFFSET_AFTER
        CLC
        RET
LOCAL_OFFSET_BEFORE:
        XOR     AX,AX
        STC
        RET
LOCAL_OFFSET_AFTER:
        MOV     AX,BUFFER_LENGTH
        STC
        RET
LOCAL_OFFSET ENDP

; The view's positions as offsets in the whole text (G_*), then the view
; written back (FLUSH_VIEW): the whole-text work can start.
SAVE_POSITIONS PROC NEAR
        PUSH    AX
        PUSH    DX
        CALL    CURSOR_GLOBAL
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,DX
        MOV     AX,TOP_LINE
        XOR     DX,DX
        ADD     AX,VIEW_LINE
        ADC     DX,VIEW_LINE+2
        MOV     G_TOP,AX
        MOV     G_TOP+2,DX
        MOV     BYTE PTR G_FLAGS,0
        MOV     AX,BLOCK_MARK
        CMP     AX,0FFFFH
        JE      SAVE_POSITIONS_HIGHLIGHT
        OR      BYTE PTR G_FLAGS,1
        CALL    MARK_IS_OUTSIDE
        JNC     SAVE_POSITIONS_MARK_HERE
        MOV     AX,MARK_G
        MOV     DX,MARK_G+2
        JMP     SHORT SAVE_POSITIONS_MARK
SAVE_POSITIONS_MARK_HERE:
        XOR     DX,DX
        ADD     AX,PREFIX_LEN
        ADC     DX,PREFIX_LEN+2
SAVE_POSITIONS_MARK:
        MOV     G_MARK,AX
        MOV     G_MARK+2,DX
SAVE_POSITIONS_HIGHLIGHT:
        MOV     AX,SEARCH_HIGHLIGHT_START
        CMP     AX,0FFFFH
        JE      SAVE_POSITIONS_FLUSH
        OR      BYTE PTR G_FLAGS,2
        XOR     DX,DX
        ADD     AX,PREFIX_LEN
        ADC     DX,PREFIX_LEN+2
        MOV     G_HL_START,AX
        MOV     G_HL_START+2,DX
        MOV     AX,SEARCH_HIGHLIGHT_END
        XOR     DX,DX
        ADD     AX,PREFIX_LEN
        ADC     DX,PREFIX_LEN+2
        MOV     G_HL_END,AX
        MOV     G_HL_END+2,DX
SAVE_POSITIONS_FLUSH:
        CALL    FLUSH_VIEW
        POP     DX
        POP     AX
        RET
SAVE_POSITIONS ENDP

; The view's positions back from G_*: the cursor, the window's top line, the
; selection's anchor (outside the view: kept in MARK_G, the view's nearer end
; standing for it), and a Find match.
RESTORE_POSITIONS PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    DX
        MOV     AX,G_CURSOR
        MOV     DX,G_CURSOR+2
        CALL    LOCAL_OFFSET
        MOV     CURSOR_OFFSET,AX
        MOV     AX,G_TOP
        MOV     DX,G_TOP+2
        SUB     AX,VIEW_LINE
        SBB     DX,VIEW_LINE+2
        JNB     RESTORE_TOP_AFTER
        XOR     AX,AX                   ; above the view: its first line
        JMP     SHORT RESTORE_TOP
RESTORE_TOP_AFTER:
        OR      DX,DX
        JZ      RESTORE_TOP
        MOV     AX,0FFFFH               ; far below
RESTORE_TOP:
        MOV     TOP_LINE,AX
        MOV     BYTE PTR MARK_OUTSIDE,0
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        TEST    BYTE PTR G_FLAGS,1
        JZ      RESTORE_HIGHLIGHT
        MOV     AX,G_MARK
        MOV     DX,G_MARK+2
        CALL    LOCAL_OFFSET
        MOV     BLOCK_MARK,AX
        JNC     RESTORE_HIGHLIGHT
        MOV     BYTE PTR MARK_OUTSIDE,1
        MOV     MARK_CLAMP,AX
        MOV     AX,G_MARK
        MOV     MARK_G,AX
        MOV     AX,G_MARK+2
        MOV     MARK_G+2,AX
RESTORE_HIGHLIGHT:
        MOV     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        MOV     WORD PTR SEARCH_HIGHLIGHT_END,0FFFFH
        TEST    BYTE PTR G_FLAGS,2
        JZ      RESTORE_DONE
        MOV     AX,G_HL_START
        MOV     DX,G_HL_START+2
        CALL    LOCAL_OFFSET
        JC      RESTORE_DONE
        MOV     BX,AX
        MOV     AX,G_HL_END
        MOV     DX,G_HL_END+2
        CALL    LOCAL_OFFSET
        JC      RESTORE_DONE
        MOV     SEARCH_HIGHLIGHT_START,BX
        MOV     SEARCH_HIGHLIGHT_END,AX
RESTORE_DONE:
        POP     DX
        POP     BX
        POP     AX
        RET
RESTORE_POSITIONS ENDP

; Carry: the selection's anchor lies outside the view (in MARK_G).
MARK_IS_OUTSIDE PROC NEAR
        CMP     BYTE PTR MARK_OUTSIDE,0
        JE      MARK_IS_OUTSIDE_NO
        PUSH    AX
        MOV     AX,BLOCK_MARK
        CMP     AX,MARK_CLAMP
        POP     AX
        JNE     MARK_IS_OUTSIDE_NO
        STC
        RET
MARK_IS_OUTSIDE_NO:
        CLC
        RET
MARK_IS_OUTSIDE ENDP

; A new view around G_CURSOR (the view written back): 2R+16 lines before the
; cursor's (10 KB at most) and 2R+16 after it (R: the window's rows), then
; the positions.
REBUILD_VIEW PROC NEAR
        PUSH    AX
        MOV     AX,TEXT_ROW_COUNT
        SHL     AX,1
        ADD     AX,16
        MOV     REBUILD_BEFORE,AX
        MOV     REBUILD_AFTER,AX
        MOV     WORD PTR REBUILD_BUDGET,10240
        POP     AX
        JMP     REBUILD_VIEW_WITH
REBUILD_VIEW ENDP

; As REBUILD_VIEW with REBUILD_BEFORE lines (REBUILD_BUDGET bytes at most)
; before the cursor's line and REBUILD_AFTER lines after it.
REBUILD_VIEW_WITH PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        CALL    DOC_LENGTH
        CMP     DX,G_CURSOR+2
        JNE     REBUILD_CURSOR_CHECKED
        CMP     AX,G_CURSOR
REBUILD_CURSOR_CHECKED:
        JAE     REBUILD_CURSOR_IN
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,DX
REBUILD_CURSOR_IN:
        MOV     AX,G_CURSOR
        MOV     DX,G_CURSOR+2
        CALL    SET_BOUNDARY
        CALL    CURSOR_LINE_LENGTH      ; a long cursor line leaves less
        MOV     BX,BUFFER_MAX-VIEW_RESERVE      ; room for the lines before
        SUB     BX,AX
        JNB     REBUILD_ROOM_LEFT
        XOR     BX,BX
REBUILD_ROOM_LEFT:
        SHR     BX,1                    ; half of what is left, at most
        CMP     BX,REBUILD_BUDGET       ; the budget
        JBE     REBUILD_ROOM_BEFORE
        MOV     BX,REBUILD_BUDGET
REBUILD_ROOM_BEFORE:
        ADD     BX,CX                   ; and the cursor's own line start
        MOV     CX,REBUILD_BEFORE
        CALL    BACK_LINES
        MOV     FILL_MIN,AX
        MOV     BX,AX
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        SUB     AX,BX
        SBB     DX,0
        CALL    SET_BOUNDARY
        MOV     AX,REBUILD_AFTER
        MOV     FILL_LINES,AX
        MOV     WORD PTR FILL_BUDGET,BUFFER_MAX-VIEW_RESERVE
        CALL    FILL_VIEW
        CALL    RESTORE_POSITIONS
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
REBUILD_VIEW_WITH ENDP

; With the view empty and the boundary at the cursor: AX = the length of
; the cursor's line, CX = the part of it before the cursor (both at most
; BUFFER_MAX).
CURSOR_LINE_LENGTH PROC NEAR
        PUSH    BX
        PUSH    DX
        PUSH    DI
        PUSH    ES
        XOR     CX,CX
        MOV     BX,BUFFER_MAX
        CALL    BACK_LINES
        MOV     CX,AX                   ; back to the line's start
        CALL    SUFFIX_START
        CALL    ARENA_ES_DI
        MOV     DX,BUFFER_MAX           ; on to its end, at most the view
        CMP     WORD PTR SUFFIX_LEN+2,0
        JNE     CURSOR_LINE_LIMITED
        CMP     DX,SUFFIX_LEN
        JBE     CURSOR_LINE_LIMITED
        MOV     DX,SUFFIX_LEN
CURSOR_LINE_LIMITED:
        XOR     BX,BX
CURSOR_LINE_SCAN:
        CMP     BX,DX
        JAE     CURSOR_LINE_END
        INC     BX
        CMP     BYTE PTR ES:[DI+BX-1],10
        JNE     CURSOR_LINE_SCAN
CURSOR_LINE_END:
        MOV     AX,BX
        ADD     AX,CX
        JNC     CURSOR_LINE_DONE
        MOV     AX,0FFFFH
CURSOR_LINE_DONE:
        POP     ES
        POP     DI
        POP     DX
        POP     BX
        RET
CURSOR_LINE_LENGTH ENDP

; DX:AX = an offset in the whole text: the cursor goes there, a new view
; around it when it lies outside this one.
SET_CURSOR_GLOBAL PROC NEAR
        PUSH    AX
        PUSH    DX
        PUSH    AX
        PUSH    DX
        CALL    LOCAL_OFFSET
        POP     DX
        JC      SET_CURSOR_GLOBAL_FAR
        MOV     CURSOR_OFFSET,AX
        POP     AX
        JMP     SHORT SET_CURSOR_GLOBAL_DONE
SET_CURSOR_GLOBAL_FAR:
        CALL    SAVE_POSITIONS
        POP     AX
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,DX
        CALL    REBUILD_VIEW
SET_CURSOR_GLOBAL_DONE:
        POP     DX
        POP     AX
        RET
SET_CURSOR_GLOBAL ENDP

; Between commands: a new view when the next command may need more than this
; one holds - a page above the window's top line, two pages below the
; cursor's line - or when it has less than VIEW_RESERVE bytes free.
NORMALIZE_VIEW PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     AX,BUFFER_MAX
        SUB     AX,BUFFER_LENGTH
        CMP     AX,VIEW_RESERVE
        JB      NORMALIZE_VIEW_REBUILD
        CALL    CALCULATE_POSITION
        MOV     BX,CURSOR_OFFSET
        CALL    LINE_START_OF           ; BX = the cursor's line start
        MOV     AX,PREFIX_LEN
        OR      AX,PREFIX_LEN+2
        JZ      NORMALIZE_VIEW_AFTER
        MOV     AX,TEXT_ROW_COUNT
        INC     AX
        INC     AX
        CMP     TOP_LINE,AX
        JAE     NORMALIZE_VIEW_AFTER
        CMP     BX,6144                 ; long lines: as much as there is
        JB      NORMALIZE_VIEW_REBUILD
NORMALIZE_VIEW_AFTER:
        MOV     AX,SUFFIX_LEN
        OR      AX,SUFFIX_LEN+2
        JZ      NORMALIZE_VIEW_DONE
        CALL    FIND_LAST_LINE
        SUB     AX,CURRENT_LINE
        MOV     CX,TEXT_ROW_COUNT
        SHL     CX,1
        INC     CX
        INC     CX
        CMP     AX,CX
        JAE     NORMALIZE_VIEW_DONE
        MOV     AX,BUFFER_LENGTH
        SUB     AX,BX
        CMP     AX,12288
        JAE     NORMALIZE_VIEW_DONE
NORMALIZE_VIEW_REBUILD:
        MOV     BYTE PTR DRAWN_VALID,0
        CALL    SAVE_POSITIONS
        CALL    REBUILD_VIEW
NORMALIZE_VIEW_DONE:
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
NORMALIZE_VIEW ENDP

; Room in the view for a Change: a new view around the cursor when it is
; running out.
MAKE_VIEW_ROOM PROC NEAR
        PUSH    AX
        MOV     AX,BUFFER_MAX
        SUB     AX,BUFFER_LENGTH
        CMP     AX,VIEW_RESERVE/2
        JAE     MAKE_VIEW_ROOM_DONE
        CALL    SAVE_POSITIONS
        CALL    REBUILD_VIEW
MAKE_VIEW_ROOM_DONE:
        POP     AX
        RET
MAKE_VIEW_ROOM ENDP

; Page Up: the lines above the window a page needs, even of long lines.
PAGE_UP_VIEW PROC NEAR
        PUSH    AX
        MOV     AX,PREFIX_LEN
        OR      AX,PREFIX_LEN+2
        JZ      PAGE_UP_VIEW_DONE
        MOV     AX,TEXT_ROW_COUNT
        CMP     TOP_LINE,AX
        JA      PAGE_UP_VIEW_DONE
        CALL    SAVE_POSITIONS
        SHL     AX,1
        ADD     AX,2
        MOV     REBUILD_BEFORE,AX
        MOV     WORD PTR REBUILD_BUDGET,BUFFER_MAX-VIEW_RESERVE-4096
        MOV     AX,TEXT_ROW_COUNT
        MOV     REBUILD_AFTER,AX
        CALL    REBUILD_VIEW_WITH
PAGE_UP_VIEW_DONE:
        POP     AX
        RET
PAGE_UP_VIEW ENDP

; Page Down: the lines below the cursor a page needs, even of long lines.
PAGE_DOWN_VIEW PROC NEAR
        PUSH    AX
        PUSH    CX
        MOV     AX,SUFFIX_LEN
        OR      AX,SUFFIX_LEN+2
        JZ      PAGE_DOWN_VIEW_DONE
        CALL    CALCULATE_POSITION
        CALL    FIND_LAST_LINE
        SUB     AX,CURRENT_LINE
        MOV     CX,TEXT_ROW_COUNT
        SHL     CX,1
        INC     CX
        CMP     AX,CX
        JAE     PAGE_DOWN_VIEW_DONE
        CALL    SAVE_POSITIONS
        MOV     AX,TEXT_ROW_COUNT
        MOV     REBUILD_BEFORE,AX
        MOV     WORD PTR REBUILD_BUDGET,4096
        ADD     CX,2
        MOV     REBUILD_AFTER,CX
        CALL    REBUILD_VIEW_WITH
PAGE_DOWN_VIEW_DONE:
        POP     CX
        POP     AX
        RET
PAGE_DOWN_VIEW ENDP

; ---- The clipboard, at the block's top ----------------------------------
; DX:AX = a clipboard length: room for it (the text after the view moves
; down; a little more is kept for the next one). Carry: no memory for it.
CLIP_RESERVE PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        CMP     DX,CLIP_CAP+2
        JNE     CLIP_RESERVE_ORDERED
        CMP     AX,CLIP_CAP
CLIP_RESERVE_ORDERED:
        JBE     CLIP_RESERVE_OK
        SUB     AX,CLIP_CAP
        SBB     DX,CLIP_CAP+2           ; DX:AX = the growth needed
        MOV     BX,AX
        MOV     CX,DX
        CALL    DOC_ROOM
        CMP     DX,CX
        JNE     CLIP_RESERVE_ROOM
        CMP     AX,BX
CLIP_RESERVE_ROOM:
        JB      CLIP_RESERVE_FAIL
        PUSH    BX                      ; a kilobyte more when there is room
        PUSH    CX
        ADD     BX,1023
        ADC     CX,0
        AND     BX,0FC00H
        CMP     DX,CX
        JNE     CLIP_RESERVE_ROUNDED
        CMP     AX,BX
CLIP_RESERVE_ROUNDED:
        POP     DX                      ; DX:AX = just the growth needed
        POP     AX
        JAE     CLIP_RESERVE_GROW
        MOV     BX,AX
        MOV     CX,DX
CLIP_RESERVE_GROW:
        CALL    SUFFIX_START
        MOV     MOVE_SRC,AX
        MOV     MOVE_SRC+2,DX
        SUB     AX,BX
        SBB     DX,CX
        MOV     MOVE_DST,AX
        MOV     MOVE_DST+2,DX
        MOV     AX,SUFFIX_LEN
        MOV     MOVE_LEN,AX
        MOV     AX,SUFFIX_LEN+2
        MOV     MOVE_LEN+2,AX
        CALL    ARENA_MOVE
        ADD     CLIP_CAP,BX
        ADC     CLIP_CAP+2,CX
CLIP_RESERVE_OK:
        CLC
        JMP     SHORT CLIP_RESERVE_DONE
CLIP_RESERVE_FAIL:
        STC
CLIP_RESERVE_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
CLIP_RESERVE ENDP

; Give the clipboard's spare room back to the text: its text and the text
; after the view move up.
CLIP_RECLAIM PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     BX,CLIP_CAP
        MOV     CX,CLIP_CAP+2
        SUB     BX,CLIP_LEN
        SBB     CX,CLIP_LEN+2           ; CX:BX = the spare room
        MOV     AX,BX
        OR      AX,CX
        JZ      CLIP_RECLAIM_DONE
        CALL    CLIP_BASE
        MOV     MOVE_SRC,AX
        MOV     MOVE_SRC+2,DX
        ADD     AX,BX
        ADC     DX,CX
        MOV     MOVE_DST,AX
        MOV     MOVE_DST+2,DX
        MOV     AX,CLIP_LEN
        MOV     MOVE_LEN,AX
        MOV     AX,CLIP_LEN+2
        MOV     MOVE_LEN+2,AX
        CALL    ARENA_MOVE
        CALL    SUFFIX_START
        MOV     MOVE_SRC,AX
        MOV     MOVE_SRC+2,DX
        ADD     AX,BX
        ADC     DX,CX
        MOV     MOVE_DST,AX
        MOV     MOVE_DST+2,DX
        MOV     AX,SUFFIX_LEN
        MOV     MOVE_LEN,AX
        MOV     AX,SUFFIX_LEN+2
        MOV     MOVE_LEN+2,AX
        CALL    ARENA_MOVE
        MOV     AX,CLIP_LEN
        MOV     CLIP_CAP,AX
        MOV     AX,CLIP_LEN+2
        MOV     CLIP_CAP+2,AX
CLIP_RECLAIM_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
CLIP_RECLAIM ENDP

; ---- The whole text, the view written back ------------------------------
; DX:AX = an offset in the whole text: BL = its byte. Also usable with the
; view in place for offsets before it.
FLAT_BYTE PROC NEAR
        PUSH    AX
        PUSH    DX
        PUSH    DI
        PUSH    ES
        CMP     DX,PREFIX_LEN+2
        JNE     FLAT_BYTE_ORDERED
        CMP     AX,PREFIX_LEN
FLAT_BYTE_ORDERED:
        JB      FLAT_BYTE_READ
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        PUSH    DX
        PUSH    AX
        CALL    SUFFIX_START
        POP     DI
        ADD     AX,DI
        POP     DI
        ADC     DX,DI
FLAT_BYTE_READ:
        CALL    ARENA_ES_DI
        MOV     BL,ES:[DI]
        POP     ES
        POP     DI
        POP     DX
        POP     AX
        RET
FLAT_BYTE ENDP

; Carry when DX:AX is at (or past) the end of the whole text (the view
; written back).
FLAT_AT_END PROC NEAR
        PUSH    BX
        PUSH    CX
        MOV     BX,PREFIX_LEN
        MOV     CX,PREFIX_LEN+2
        ADD     BX,SUFFIX_LEN
        ADC     CX,SUFFIX_LEN+2
        CMP     DX,CX
        JNE     FLAT_AT_END_ORDERED
        CMP     AX,BX
FLAT_AT_END_ORDERED:
        POP     CX
        POP     BX
        JAE     FLAT_AT_END_YES
        CLC
        RET
FLAT_AT_END_YES:
        STC
        RET
FLAT_AT_END ENDP

; DX:AX = an offset: DX:AX returns the start of its line.
FLAT_LINE_START PROC NEAR
        PUSH    BX
FLAT_LINE_START_NEXT:
        MOV     BX,AX
        OR      BX,DX
        JZ      FLAT_LINE_START_DONE
        SUB     AX,1
        SBB     DX,0
        CALL    FLAT_BYTE
        CMP     BL,10
        JNE     FLAT_LINE_START_NEXT
        ADD     AX,1
        ADC     DX,0
FLAT_LINE_START_DONE:
        POP     BX
        RET
FLAT_LINE_START ENDP

; DX:AX = an offset: DX:AX returns the start of the next line; carry, and
; the text's end, when there is no line end before it.
FLAT_NEXT_LINE PROC NEAR
        PUSH    BX
FLAT_NEXT_LINE_NEXT:
        CALL    FLAT_AT_END
        JC      FLAT_NEXT_LINE_DONE
        CALL    FLAT_BYTE
        ADD     AX,1
        ADC     DX,0
        CMP     BL,10
        JNE     FLAT_NEXT_LINE_NEXT
        CLC
FLAT_NEXT_LINE_DONE:
        POP     BX
        RET
FLAT_NEXT_LINE ENDP

; DX:AX = a line start: AX = its leading blanks, CX = its length (no CR or
; LF), as LEADING_BLANKS.
FLAT_LEADING_BLANKS PROC NEAR
        PUSH    BX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        MOV     SI,AX
        MOV     DI,DX
        XOR     CX,CX
        PUSH    CX                      ; the blanks
FLAT_LEADING_NEXT:
        MOV     AX,SI
        MOV     DX,DI
        CALL    FLAT_AT_END
        JC      FLAT_LEADING_DONE
        CALL    FLAT_BYTE
        CMP     BL,13
        JE      FLAT_LEADING_DONE
        CMP     BL,10
        JE      FLAT_LEADING_DONE
        CMP     BL,' '
        JNE     FLAT_LEADING_TEXT
        POP     AX
        CMP     AX,CX
        JNE     FLAT_LEADING_KEEP
        INC     AX
FLAT_LEADING_KEEP:
        PUSH    AX
FLAT_LEADING_TEXT:
        INC     CX
        ADD     SI,1
        ADC     DI,0
        JMP     FLAT_LEADING_NEXT
FLAT_LEADING_DONE:
        POP     AX
        POP     DI
        POP     SI
        POP     DX
        POP     BX
        RET
FLAT_LEADING_BLANKS ENDP

; DX:AX = a line start, BX = a column: AX = the indentation of the nearest
; line above whose indentation is below BX, as INDENT_ABOVE (an empty line
; does not count, a line of blanks only counts as zero, none gives zero).
FLAT_INDENT_ABOVE PROC NEAR
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        MOV     SI,AX
        MOV     DI,DX
FLAT_INDENT_ABOVE_PREVIOUS:
        MOV     AX,SI
        OR      AX,DI
        JZ      FLAT_INDENT_ABOVE_NONE
        MOV     AX,SI
        MOV     DX,DI
        SUB     AX,1
        SBB     DX,0
        CALL    FLAT_LINE_START
        MOV     SI,AX
        MOV     DI,DX
        CALL    FLAT_LEADING_BLANKS
        JCXZ    FLAT_INDENT_ABOVE_PREVIOUS
        CMP     AX,CX
        JAE     FLAT_INDENT_ABOVE_NONE
        CMP     AX,BX
        JAE     FLAT_INDENT_ABOVE_PREVIOUS
        JMP     SHORT FLAT_INDENT_ABOVE_DONE
FLAT_INDENT_ABOVE_NONE:
        XOR     AX,AX
FLAT_INDENT_ABOVE_DONE:
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        RET
FLAT_INDENT_ABOVE ENDP

; DX:AX = a line number: DX:AX returns where it starts in the whole text, or
; where the last line starts when there are fewer.
FLAT_LINE_AT PROC NEAR
        PUSH    BX
        PUSH    CX
        MOV     BX,AX
        MOV     CX,DX                   ; CX:BX = lines to pass
        XOR     AX,AX
        XOR     DX,DX
FLAT_LINE_AT_NEXT:
        PUSH    BX
        OR      BX,CX
        POP     BX
        JZ      FLAT_LINE_AT_DONE
        PUSH    AX
        PUSH    DX
        CALL    FLAT_NEXT_LINE
        JC      FLAT_LINE_AT_LAST
        ADD     SP,4
        SUB     BX,1
        SBB     CX,0
        JMP     FLAT_LINE_AT_NEXT
FLAT_LINE_AT_LAST:
        POP     DX
        POP     AX
FLAT_LINE_AT_DONE:
        POP     CX
        POP     BX
        RET
FLAT_LINE_AT ENDP

; Start reading the whole text (the view written back) at STREAM_POS up to
; STREAM_END.
STREAM_BEGIN PROC NEAR
        MOV     WORD PTR STREAM_LEFT,0
        RET
STREAM_BEGIN ENDP

; AL = the next byte of the text being read; carry at its end.
STREAM_NEXT PROC NEAR
        CMP     WORD PTR STREAM_LEFT,0
        JE      STREAM_NEXT_RUN_START
        JMP     STREAM_NEXT_READ
STREAM_NEXT_END:
        POP     DX
        POP     BX
        STC
        RET
STREAM_NEXT_RUN_START:
        PUSH    BX
        PUSH    DX
        MOV     AX,STREAM_END
        MOV     DX,STREAM_END+2
        SUB     AX,STREAM_POS
        SBB     DX,STREAM_POS+2
        JB      STREAM_NEXT_END
        MOV     BX,AX
        OR      BX,DX
        JZ      STREAM_NEXT_END
        MOV     BX,8000H                ; BX = the bytes to read from here
        OR      DX,DX
        JNZ     STREAM_NEXT_RUN
        CMP     AX,BX
        JAE     STREAM_NEXT_RUN
        MOV     BX,AX
STREAM_NEXT_RUN:
        MOV     AX,STREAM_POS
        MOV     DX,STREAM_POS+2
        CMP     DX,PREFIX_LEN+2
        JNE     STREAM_NEXT_SIDE
        CMP     AX,PREFIX_LEN
STREAM_NEXT_SIDE:
        JAE     STREAM_NEXT_SUFFIX
        MOV     AX,PREFIX_LEN           ; before the boundary: up to it
        MOV     DX,PREFIX_LEN+2
        SUB     AX,STREAM_POS
        SBB     DX,STREAM_POS+2
        OR      DX,DX
        JNZ     STREAM_NEXT_PREFIX
        CMP     AX,BX
        JAE     STREAM_NEXT_PREFIX
        MOV     BX,AX
STREAM_NEXT_PREFIX:
        MOV     AX,STREAM_POS
        MOV     DX,STREAM_POS+2
        JMP     SHORT STREAM_NEXT_POINT
STREAM_NEXT_SUFFIX:                     ; after it: in the suffix
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        MOV     STREAM_TMP,AX
        MOV     STREAM_TMP+2,DX
        CALL    SUFFIX_START
        ADD     AX,STREAM_TMP
        ADC     DX,STREAM_TMP+2
STREAM_NEXT_POINT:
        PUSH    AX
        PUSH    DX
        CALL    ARENA_ADDRESS
        MOV     STREAM_SEG,AX
        MOV     STREAM_OFF,DX
        POP     DX
        POP     AX
        MOV     STREAM_LEFT,BX
        POP     DX
        POP     BX
STREAM_NEXT_READ:
        PUSH    DI
        PUSH    ES
        MOV     ES,STREAM_SEG
        MOV     DI,STREAM_OFF
        MOV     AL,ES:[DI]
        POP     ES
        POP     DI
        INC     WORD PTR STREAM_OFF
        DEC     WORD PTR STREAM_LEFT
        ADD     WORD PTR STREAM_POS,1
        ADC     WORD PTR STREAM_POS+2,0
        CLC
        RET
STREAM_NEXT ENDP

; ---- Find and Change over the whole text --------------------------------
; With the view written back: the first match of SEARCH_PATTERN from DX:AX
; on, as FIND_FROM_CURSOR matches (Match Upper/Lowercase, Whole Word).
; Carry: none; else DX:AX = the match, CX = its length.
FLAT_FIND PROC NEAR
        PUSH    BX
        PUSH    SI
        PUSH    DI
        CMP     BYTE PTR SEARCH_PATTERN,0
        JE      FLAT_FIND_NONE
FLAT_FIND_CANDIDATE:
        CALL    FLAT_AT_END
        JNC     FLAT_FIND_TRY
FLAT_FIND_NONE:
        JMP     FLAT_FIND_FAIL
FLAT_FIND_TRY:
        MOV     DI,AX                   ; the candidate (DX:DI)
        PUSH    DX
        MOV     SI,OFFSET SEARCH_PATTERN
        XOR     CX,CX
FLAT_FIND_COMPARE:
        MOV     BH,[SI]
        OR      BH,BH
        JZ      FLAT_FIND_MATCHED
        CALL    FLAT_AT_END
        JC      FLAT_FIND_MISS
        CALL    FLAT_BYTE
        CMP     BYTE PTR MATCH_CASE,0
        JNE     FLAT_FIND_FOLDED
        CMP     BH,'a'
        JB      FLAT_FIND_PATTERN_FOLDED
        CMP     BH,'z'
        JA      FLAT_FIND_PATTERN_FOLDED
        SUB     BH,20H
FLAT_FIND_PATTERN_FOLDED:
        CMP     BL,'a'
        JB      FLAT_FIND_FOLDED
        CMP     BL,'z'
        JA      FLAT_FIND_FOLDED
        SUB     BL,20H
FLAT_FIND_FOLDED:
        CMP     BH,BL
        JNE     FLAT_FIND_MISS
        INC     SI
        INC     CX
        ADD     AX,1
        ADC     DX,0
        JMP     FLAT_FIND_COMPARE
FLAT_FIND_MATCHED:                      ; DX:AX = the match's end
        CMP     BYTE PTR WHOLE_WORD,0
        JE      FLAT_FIND_ACCEPT
        CALL    FLAT_AT_END
        JC      FLAT_FIND_BEFORE
        CALL    FLAT_BYTE
        PUSH    AX
        MOV     AL,BL
        CALL    IS_WORD_BYTE
        POP     AX
        JC      FLAT_FIND_MISS
FLAT_FIND_BEFORE:
        POP     DX
        PUSH    DX
        MOV     AX,DI
        MOV     BX,AX
        OR      BX,DX
        JZ      FLAT_FIND_ACCEPT
        SUB     AX,1
        SBB     DX,0
        CALL    FLAT_BYTE
        MOV     AL,BL
        CALL    IS_WORD_BYTE
        JC      FLAT_FIND_MISS
FLAT_FIND_ACCEPT:
        POP     DX
        MOV     AX,DI
        CLC
        JMP     SHORT FLAT_FIND_DONE
FLAT_FIND_MISS:
        POP     DX
        MOV     AX,DI
        ADD     AX,1
        ADC     DX,0
        JMP     FLAT_FIND_CANDIDATE
FLAT_FIND_FAIL:
        STC
FLAT_FIND_DONE:
        POP     DI
        POP     SI
        POP     BX
        RET
FLAT_FIND ENDP

; Find from DX:AX (an offset in the whole text) on to the text's end, as
; FIND_FROM_CURSOR does in the view: carry when there is no match; else
; the cursor on it, SEARCH_HIGHLIGHT_START/END around it, and the view
; around it when it lies beyond.
FIND_FROM_GLOBAL PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     BX,AX
        MOV     CX,DX
        CALL    LOCAL_OFFSET
        JC      FIND_FROM_GLOBAL_FLAT
        MOV     CURSOR_OFFSET,AX
        CALL    FIND_FROM_CURSOR
        JNC     FIND_FROM_GLOBAL_DONE
        MOV     AX,SUFFIX_LEN
        OR      AX,SUFFIX_LEN+2
        JZ      FIND_FROM_GLOBAL_NONE
        MOV     BX,PREFIX_LEN           ; on from the view's end
        MOV     CX,PREFIX_LEN+2
        ADD     BX,BUFFER_LENGTH
        ADC     CX,0
FIND_FROM_GLOBAL_FLAT:
        CALL    SAVE_POSITIONS
        MOV     AX,BX
        MOV     DX,CX
        CALL    FLAT_FIND
        JC      FIND_FROM_GLOBAL_MISSED
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,DX
        MOV     G_HL_START,AX
        MOV     G_HL_START+2,DX
        ADD     AX,CX
        ADC     DX,0
        MOV     G_HL_END,AX
        MOV     G_HL_END+2,DX
        OR      BYTE PTR G_FLAGS,2
        CALL    REBUILD_VIEW
        CLC
        JMP     SHORT FIND_FROM_GLOBAL_DONE
FIND_FROM_GLOBAL_MISSED:
        CALL    REBUILD_VIEW
FIND_FROM_GLOBAL_NONE:
        MOV     WORD PTR SEARCH_HIGHLIGHT_START,0FFFFH
        MOV     WORD PTR SEARCH_HIGHLIGHT_END,0FFFFH
        STC
FIND_FROM_GLOBAL_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
FIND_FROM_GLOBAL ENDP

; DX:AX = the cursor's offset, one further unless at the text's end: where
; Find, Change and F3 look from (DOS 5 never offers a match at the cursor).
CURSOR_GLOBAL_NEXT PROC NEAR
        PUSH    BX
        PUSH    CX
        CALL    DOC_LENGTH
        MOV     BX,AX
        MOV     CX,DX
        CALL    CURSOR_GLOBAL
        CMP     DX,CX
        JNE     CURSOR_GLOBAL_NEXT_ORDERED
        CMP     AX,BX
CURSOR_GLOBAL_NEXT_ORDERED:
        JAE     CURSOR_GLOBAL_NEXT_DONE
        ADD     AX,1
        ADC     DX,0
CURSOR_GLOBAL_NEXT_DONE:
        POP     CX
        POP     BX
        RET
CURSOR_GLOBAL_NEXT ENDP

; ---- The clipboard and selections beyond the view -----------------------
; With the view written back and a selection (G_FLAGS bit 0): SEL_A and
; SEL_B = its ends in order.
SELECTION_ENDS PROC NEAR
        PUSH    AX
        PUSH    DX
        MOV     AX,G_MARK
        MOV     DX,G_MARK+2
        CMP     DX,G_CURSOR+2
        JNE     SELECTION_ENDS_ORDERED
        CMP     AX,G_CURSOR
SELECTION_ENDS_ORDERED:
        JB      SELECTION_ENDS_MARK_FIRST
        MOV     SEL_B,AX
        MOV     SEL_B+2,DX
        MOV     AX,G_CURSOR
        MOV     DX,G_CURSOR+2
        MOV     SEL_A,AX
        MOV     SEL_A+2,DX
        JMP     SHORT SELECTION_ENDS_DONE
SELECTION_ENDS_MARK_FIRST:
        MOV     SEL_A,AX
        MOV     SEL_A+2,DX
        MOV     AX,G_CURSOR
        MOV     DX,G_CURSOR+2
        MOV     SEL_B,AX
        MOV     SEL_B+2,DX
SELECTION_ENDS_DONE:
        POP     DX
        POP     AX
        RET
SELECTION_ENDS ENDP

; Copy a selection reaching beyond the view to the clipboard. Carry: no
; memory for it (DOS 5's "Out of memory"); the selection stays.
COPY_BLOCK_GLOBAL PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        CALL    SAVE_POSITIONS
        CALL    SELECTION_ENDS
        MOV     AX,SEL_B
        MOV     DX,SEL_B+2
        SUB     AX,SEL_A
        SBB     DX,SEL_A+2
        CALL    CLIP_RESERVE
        JNC     COPY_BLOCK_GLOBAL_ROOM
        JMP     COPY_BLOCK_GLOBAL_FAIL
COPY_BLOCK_GLOBAL_ROOM:
        MOV     CLIP_LEN,AX
        MOV     CLIP_LEN+2,DX
        CALL    CLIP_BASE
        MOV     BX,AX
        MOV     CX,DX                   ; CX:BX = where the copy goes
        MOV     AX,SEL_A                ; the part before the boundary
        MOV     DX,SEL_A+2
        CMP     DX,PREFIX_LEN+2
        JNE     COPY_GLOBAL_SIDE
        CMP     AX,PREFIX_LEN
COPY_GLOBAL_SIDE:
        JAE     COPY_GLOBAL_SUFFIX
        MOV     MOVE_SRC,AX
        MOV     MOVE_SRC+2,DX
        MOV     MOVE_DST,BX
        MOV     MOVE_DST+2,CX
        MOV     AX,SEL_B
        MOV     DX,SEL_B+2
        CMP     DX,PREFIX_LEN+2
        JNE     COPY_GLOBAL_END_SIDE
        CMP     AX,PREFIX_LEN
COPY_GLOBAL_END_SIDE:
        JBE     COPY_GLOBAL_PREFIX_END
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
COPY_GLOBAL_PREFIX_END:
        SUB     AX,SEL_A
        SBB     DX,SEL_A+2
        MOV     MOVE_LEN,AX
        MOV     MOVE_LEN+2,DX
        ADD     BX,AX
        ADC     CX,DX
        CALL    ARENA_MOVE
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        MOV     SEL_A,AX
        MOV     SEL_A+2,DX
COPY_GLOBAL_SUFFIX:                     ; the part after it, if any
        MOV     AX,SEL_B
        MOV     DX,SEL_B+2
        SUB     AX,SEL_A
        SBB     DX,SEL_A+2
        JB      COPY_GLOBAL_COPIED
        MOV     MOVE_LEN,AX
        OR      AX,DX
        JZ      COPY_GLOBAL_COPIED
        MOV     AX,MOVE_LEN
        MOV     MOVE_LEN,AX
        MOV     MOVE_LEN+2,DX
        MOV     MOVE_DST,BX
        MOV     MOVE_DST+2,CX
        MOV     AX,SEL_A
        MOV     DX,SEL_A+2
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        MOV     BX,AX
        MOV     CX,DX
        CALL    SUFFIX_START
        ADD     AX,BX
        ADC     DX,CX
        MOV     MOVE_SRC,AX
        MOV     MOVE_SRC+2,DX
        CALL    ARENA_MOVE
COPY_GLOBAL_COPIED:
        CALL    REBUILD_VIEW
        CLC
        JMP     SHORT COPY_BLOCK_GLOBAL_DONE
COPY_BLOCK_GLOBAL_FAIL:
        CALL    REBUILD_VIEW
        STC
COPY_BLOCK_GLOBAL_DONE:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
COPY_BLOCK_GLOBAL ENDP

; Delete a selection reaching beyond the view; the cursor at its start.
CLEAR_BLOCK_GLOBAL PROC NEAR
        PUSH    AX
        PUSH    DX
        CALL    SAVE_POSITIONS
        CALL    SELECTION_ENDS
        MOV     AX,SEL_B
        MOV     DX,SEL_B+2
        CALL    SET_BOUNDARY            ; the selection ends the prefix
        SUB     AX,SEL_A
        SBB     DX,SEL_A+2
        MOV     COUNT_LEN,AX
        MOV     COUNT_LEN+2,DX
        MOV     AX,SEL_A
        MOV     DX,SEL_A+2
        MOV     PREFIX_LEN,AX
        MOV     PREFIX_LEN+2,DX
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,DX
        CALL    ARENA_COUNT_LF          ; its line ends go
        SUB     VIEW_LINE,AX
        SBB     VIEW_LINE+2,DX
        MOV     AX,G_TOP                ; a window top in it: its first line
        MOV     DX,G_TOP+2
        CMP     DX,VIEW_LINE+2
        JNE     CLEAR_GLOBAL_TOP
        CMP     AX,VIEW_LINE
CLEAR_GLOBAL_TOP:
        JBE     CLEAR_GLOBAL_TOP_KEPT
        MOV     AX,VIEW_LINE
        MOV     G_TOP,AX
        MOV     AX,VIEW_LINE+2
        MOV     G_TOP+2,AX
CLEAR_GLOBAL_TOP_KEPT:
        MOV     BYTE PTR G_FLAGS,0
        MOV     BYTE PTR MODIFIED,1
        MOV     BYTE PTR SHIFT_SELECTION,0
        CALL    REBUILD_VIEW
        POP     DX
        POP     AX
        RET
CLEAR_BLOCK_GLOBAL ENDP

; Paste a clipboard too large for the view's room. Carry: no memory for it.
PASTE_BLOCK_GLOBAL PROC NEAR
        PUSH    AX
        PUSH    DX
        CALL    DOC_ROOM
        CMP     DX,CLIP_LEN+2
        JNE     PASTE_GLOBAL_ROOM
        CMP     AX,CLIP_LEN
PASTE_GLOBAL_ROOM:
        JB      PASTE_GLOBAL_FAIL
        CALL    SAVE_POSITIONS
        MOV     AX,G_CURSOR
        MOV     DX,G_CURSOR+2
        CALL    SET_BOUNDARY
        MOV     MOVE_DST,AX
        MOV     MOVE_DST+2,DX
        CALL    CLIP_BASE
        MOV     MOVE_SRC,AX
        MOV     MOVE_SRC+2,DX
        MOV     AX,CLIP_LEN
        MOV     DX,CLIP_LEN+2
        MOV     MOVE_LEN,AX
        MOV     MOVE_LEN+2,DX
        MOV     COUNT_LEN,AX
        MOV     COUNT_LEN+2,DX
        ADD     PREFIX_LEN,AX
        ADC     PREFIX_LEN+2,DX
        CALL    ARENA_MOVE
        CALL    CLIP_BASE
        CALL    ARENA_COUNT_LF
        ADD     VIEW_LINE,AX
        ADC     VIEW_LINE+2,DX
        MOV     BYTE PTR G_FLAGS,0
        MOV     BYTE PTR MODIFIED,1
        MOV     BYTE PTR SHIFT_SELECTION,0
        CALL    REBUILD_VIEW
        CLC
        JMP     SHORT PASTE_GLOBAL_DONE
PASTE_GLOBAL_FAIL:
        STC
PASTE_GLOBAL_DONE:
        POP     DX
        POP     AX
        RET
PASTE_BLOCK_GLOBAL ENDP

; With the view written back and a selection: SEL_A = the start of its first
; line, SEL_B = the start of the line after its last (a line it only reaches
; at column 0 is not one of them), or the text's end.
SELECTION_LINES PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    DX
        CALL    SELECTION_ENDS
        MOV     AX,SEL_A
        MOV     DX,SEL_A+2
        CALL    FLAT_LINE_START
        MOV     SEL_A,AX
        MOV     SEL_A+2,DX
        MOV     AX,SEL_B
        MOV     DX,SEL_B+2
        MOV     BX,AX
        OR      BX,DX
        JZ      SELECTION_LINES_DONE
        SUB     AX,1
        SBB     DX,0
        CALL    FLAT_BYTE
        CMP     BL,10
        JE      SELECTION_LINES_DONE
        CALL    FLAT_NEXT_LINE
        MOV     SEL_B,AX
        MOV     SEL_B+2,DX
SELECTION_LINES_DONE:
        POP     DX
        POP     BX
        POP     AX
        RET
SELECTION_LINES ENDP

; DX:AX = a position after the text at BX (CX bytes were inserted there):
; past BX it moves on with the text.
GLOBAL_ADJUST_INSERT PROC NEAR
        CMP     DX,INSERT_AT+2
        JNE     GLOBAL_ADJUST_INSERT_ORDERED
        CMP     AX,INSERT_AT
GLOBAL_ADJUST_INSERT_ORDERED:
        JBE     GLOBAL_ADJUST_INSERT_DONE
        ADD     AX,CX
        ADC     DX,0
GLOBAL_ADJUST_INSERT_DONE:
        RET
GLOBAL_ADJUST_INSERT ENDP

; DX:AX = a position after CX bytes at INSERT_AT went: past it, it moves
; back, never before INSERT_AT.
GLOBAL_ADJUST_DELETE PROC NEAR
        CMP     DX,INSERT_AT+2
        JNE     GLOBAL_ADJUST_DELETE_ORDERED
        CMP     AX,INSERT_AT
GLOBAL_ADJUST_DELETE_ORDERED:
        JBE     GLOBAL_ADJUST_DELETE_DONE
        SUB     AX,CX
        SBB     DX,0
        CMP     DX,INSERT_AT+2
        JNE     GLOBAL_ADJUST_DELETE_CLAMP
        CMP     AX,INSERT_AT
GLOBAL_ADJUST_DELETE_CLAMP:
        JAE     GLOBAL_ADJUST_DELETE_DONE
        MOV     AX,INSERT_AT
        MOV     DX,INSERT_AT+2
GLOBAL_ADJUST_DELETE_DONE:
        RET
GLOBAL_ADJUST_DELETE ENDP

; The cursor and the selection's anchor after CX bytes were inserted
; (AL = 1) or deleted (AL = 0) at INSERT_AT.
GLOBAL_ADJUST_POSITIONS PROC NEAR
        PUSH    AX
        PUSH    DX
        PUSH    BX
        MOV     BL,AL
        MOV     AX,G_CURSOR
        MOV     DX,G_CURSOR+2
        CALL    GLOBAL_ADJUST_ONE
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,DX
        MOV     AX,G_MARK
        MOV     DX,G_MARK+2
        CALL    GLOBAL_ADJUST_ONE
        MOV     G_MARK,AX
        MOV     G_MARK+2,DX
        POP     BX
        POP     DX
        POP     AX
        RET
GLOBAL_ADJUST_POSITIONS ENDP

GLOBAL_ADJUST_ONE PROC NEAR
        OR      BL,BL
        JZ      GLOBAL_ADJUST_ONE_DELETE
        JMP     GLOBAL_ADJUST_INSERT
GLOBAL_ADJUST_ONE_DELETE:
        JMP     GLOBAL_ADJUST_DELETE
GLOBAL_ADJUST_ONE ENDP

; Tab over a selection reaching beyond the view (or past the view's room):
; every line of it gets Tab Stops blanks in front, as INDENT_LINES. Carry
; (and "Out of memory"): they do not fit; nothing changes.
GLOBAL_INDENT PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    DI
        PUSH    ES
        CALL    SAVE_POSITIONS
        CALL    SELECTION_LINES
        MOV     AX,SEL_B                ; lines = their line ends, and one
        MOV     DX,SEL_B+2              ; more when the text ends without one
        SUB     AX,SEL_A
        SBB     DX,SEL_A+2
        MOV     BX,AX
        OR      BX,DX
        JNZ     GLOBAL_INDENT_HAS_LINES
        JMP     GLOBAL_INDENT_REBUILD
GLOBAL_INDENT_HAS_LINES:
        MOV     AX,SEL_B
        MOV     DX,SEL_B+2
        SUB     AX,1
        SBB     DX,0
        CALL    FLAT_BYTE
        MOV     AX,SEL_A                ; count the line ends in [A, B)
        MOV     DX,SEL_A+2
        MOV     CX,SEL_B
        MOV     DI,SEL_B+2
        SUB     CX,AX
        SBB     DI,DX
        MOV     COUNT_LEN,CX
        MOV     COUNT_LEN+2,DI
        CALL    FLAT_COUNT_LF
        CMP     BL,10
        JE      GLOBAL_INDENT_COUNTED
        ADD     AX,1
        ADC     DX,0
GLOBAL_INDENT_COUNTED:
        XOR     CX,CX
        MOV     CL,TAB_STOPS
        CALL    MUL32_16                ; DX:AX = the bytes needed
        MOV     BX,AX
        MOV     CX,DX
        CALL    DOC_ROOM
        CMP     DX,CX
        JNE     GLOBAL_INDENT_ROOM
        CMP     AX,BX
GLOBAL_INDENT_ROOM:
        JAE     GLOBAL_INDENT_LINES
        MOV     BYTE PTR MEMORY_FULL,1
        JMP     GLOBAL_INDENT_REBUILD
GLOBAL_INDENT_LINES:
        MOV     AX,SEL_A
        MOV     DX,SEL_A+2
GLOBAL_INDENT_LINE:                     ; DX:AX = a line start before SEL_B
        CMP     DX,SEL_B+2
        JNE     GLOBAL_INDENT_LINE_ORDERED
        CMP     AX,SEL_B
GLOBAL_INDENT_LINE_ORDERED:
        JAE     GLOBAL_INDENT_DONE
        CALL    SET_BOUNDARY
        MOV     INSERT_AT,AX
        MOV     INSERT_AT+2,DX
        CALL    ARENA_ES_DI
        XOR     CX,CX
        MOV     CL,TAB_STOPS
        PUSH    AX
        PUSH    CX
        MOV     AL,' '
        REP     STOSB
        POP     CX
        POP     AX
        ADD     AX,CX
        ADC     DX,0
        MOV     PREFIX_LEN,AX
        MOV     PREFIX_LEN+2,DX
        ADD     SEL_B,CX
        ADC     WORD PTR SEL_B+2,0
        PUSH    AX
        MOV     AL,1
        CALL    GLOBAL_ADJUST_POSITIONS
        POP     AX
        CALL    FLAT_NEXT_LINE
        JNC     GLOBAL_INDENT_LINE
GLOBAL_INDENT_DONE:
        MOV     BYTE PTR MODIFIED,1
GLOBAL_INDENT_REBUILD:
        CALL    REBUILD_VIEW
        POP     ES
        POP     DI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
GLOBAL_INDENT ENDP

; Shift+Tab over a selection reaching beyond the view: as OUTDENT_LINES,
; every line loses the first line's indentation less the indentation of the
; nearest less indented line above the cursor's, or all its leading blanks.
GLOBAL_OUTDENT PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    DI
        PUSH    ES
        CALL    SAVE_POSITIONS
        CALL    SELECTION_LINES
        MOV     AX,SEL_A
        MOV     DX,SEL_A+2
        CALL    FLAT_LEADING_BLANKS
        OR      AX,AX
        JNZ     GLOBAL_OUTDENT_SOME
        JMP     GLOBAL_OUTDENT_REBUILD
GLOBAL_OUTDENT_SOME:
        MOV     BX,AX
        PUSH    BX
        MOV     AX,G_CURSOR
        MOV     DX,G_CURSOR+2
        CALL    FLAT_LINE_START
        CALL    FLAT_INDENT_ABOVE
        POP     BX
        SUB     BX,AX
        MOV     OUTDENT_COUNT,BX        ; blanks to take from every line
        MOV     AX,SEL_A
        MOV     DX,SEL_A+2
GLOBAL_OUTDENT_LINE:
        CMP     DX,SEL_B+2
        JNE     GLOBAL_OUTDENT_LINE_ORDERED
        CMP     AX,SEL_B
GLOBAL_OUTDENT_LINE_ORDERED:
        JAE     GLOBAL_OUTDENT_DONE
        CALL    SET_BOUNDARY
        MOV     INSERT_AT,AX
        MOV     INSERT_AT+2,DX
        PUSH    AX
        PUSH    DX
        CALL    SUFFIX_START
        CALL    ARENA_ES_DI
        POP     DX
        POP     AX
        XOR     CX,CX                   ; leading spaces, at most the count
GLOBAL_OUTDENT_COUNT:
        CMP     CX,OUTDENT_COUNT
        JAE     GLOBAL_OUTDENT_COUNTED
        CMP     WORD PTR SUFFIX_LEN+2,0
        JNE     GLOBAL_OUTDENT_MORE
        CMP     CX,SUFFIX_LEN
        JAE     GLOBAL_OUTDENT_COUNTED
GLOBAL_OUTDENT_MORE:
        MOV     BX,DI
        ADD     BX,CX
        CMP     BYTE PTR ES:[BX],' '
        JNE     GLOBAL_OUTDENT_COUNTED
        INC     CX
        JMP     GLOBAL_OUTDENT_COUNT
GLOBAL_OUTDENT_COUNTED:
        SUB     SUFFIX_LEN,CX           ; dropped from the suffix's head
        SBB     WORD PTR SUFFIX_LEN+2,0
        SUB     SEL_B,CX
        SBB     WORD PTR SEL_B+2,0
        PUSH    AX
        XOR     AL,AL
        CALL    GLOBAL_ADJUST_POSITIONS
        POP     AX
        OR      CX,CX
        JZ      GLOBAL_OUTDENT_NEXT
        MOV     BYTE PTR MODIFIED,1
GLOBAL_OUTDENT_NEXT:
        CALL    FLAT_NEXT_LINE
        JNC     GLOBAL_OUTDENT_LINE
GLOBAL_OUTDENT_DONE:
GLOBAL_OUTDENT_REBUILD:
        CALL    REBUILD_VIEW
        POP     ES
        POP     DI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
GLOBAL_OUTDENT ENDP

; With the view written back: DX:AX = an offset, COUNT_LEN = a length: DX:AX
; returns the line ends in those bytes of the whole text.
FLAT_COUNT_LF PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    SI
        PUSH    DI
        XOR     SI,SI                   ; DI:SI = the count so far
        XOR     DI,DI
        CMP     DX,PREFIX_LEN+2         ; the part before the boundary
        JNE     FLAT_COUNT_SIDE
        CMP     AX,PREFIX_LEN
FLAT_COUNT_SIDE:
        JAE     FLAT_COUNT_SUFFIX
        MOV     BX,PREFIX_LEN
        MOV     CX,PREFIX_LEN+2
        SUB     BX,AX
        SBB     CX,DX                   ; CX:BX = bytes up to the boundary
        CMP     CX,COUNT_LEN+2
        JNE     FLAT_COUNT_PART
        CMP     BX,COUNT_LEN
FLAT_COUNT_PART:
        JBE     FLAT_COUNT_PREFIX
        MOV     BX,COUNT_LEN
        MOV     CX,COUNT_LEN+2
FLAT_COUNT_PREFIX:
        PUSH    WORD PTR COUNT_LEN+2
        PUSH    WORD PTR COUNT_LEN
        MOV     COUNT_LEN,BX
        MOV     COUNT_LEN+2,CX
        CALL    ARENA_COUNT_LF
        MOV     SI,AX
        MOV     DI,DX
        POP     WORD PTR COUNT_LEN
        POP     WORD PTR COUNT_LEN+2
        SUB     COUNT_LEN,BX
        SBB     COUNT_LEN+2,CX
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
FLAT_COUNT_SUFFIX:
        MOV     BX,COUNT_LEN
        OR      BX,COUNT_LEN+2
        JZ      FLAT_COUNT_DONE
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        MOV     BX,AX
        MOV     CX,DX
        CALL    SUFFIX_START
        ADD     AX,BX
        ADC     DX,CX
        CALL    ARENA_COUNT_LF
        ADD     SI,AX
        ADC     DI,DX
FLAT_COUNT_DONE:
        MOV     AX,SI
        MOV     DX,DI
        POP     DI
        POP     SI
        POP     CX
        POP     BX
        RET
FLAT_COUNT_LF ENDP

; DX:AX = DX:AX * CX (the product fits).
MUL32_16 PROC NEAR
        PUSH    BX
        PUSH    SI
        MOV     SI,AX
        MOV     AX,DX
        MUL     CX
        MOV     BX,AX
        MOV     AX,SI
        MUL     CX
        ADD     DX,BX
        POP     SI
        POP     BX
        RET
MUL32_16 ENDP

; DX:AX = DX:AX / CX, CX = the remainder.
DIV32_16 PROC NEAR
        PUSH    BX
        MOV     BX,AX
        MOV     AX,DX
        XOR     DX,DX
        DIV     CX
        XCHG    AX,BX
        DIV     CX
        MOV     CX,DX
        MOV     DX,BX
        POP     BX
        RET
DIV32_16 ENDP

; DX:AX = a block offset, COUNT_LEN = a length, BL = a byte: carry when
; it is not in those bytes, else DX:AX = where it first is.
ARENA_FIND_BYTE PROC NEAR
        PUSH    CX
        PUSH    DI
        PUSH    ES
ARENA_FIND_NEXT:
        MOV     CX,8000H
        CMP     WORD PTR COUNT_LEN+2,0
        JNE     ARENA_FIND_CHUNK
        CMP     COUNT_LEN,CX
        JAE     ARENA_FIND_CHUNK
        MOV     CX,COUNT_LEN
        JCXZ    ARENA_FIND_NONE
ARENA_FIND_CHUNK:
        CALL    ARENA_ES_DI
        SUB     COUNT_LEN,CX
        SBB     WORD PTR COUNT_LEN+2,0
        PUSH    AX
        PUSH    CX
        MOV     AL,BL
        REPNE   SCASB
        POP     AX                      ; the chunk's length
        JNE     ARENA_FIND_MISSED
        SUB     AX,CX
        DEC     AX                      ; the byte's place in the chunk
        MOV     CX,AX
        POP     AX
        ADD     AX,CX
        ADC     DX,0
        CLC
        JMP     SHORT ARENA_FIND_DONE
ARENA_FIND_MISSED:
        MOV     CX,AX
        POP     AX
        ADD     AX,CX
        ADC     DX,0
        JMP     ARENA_FIND_NEXT
ARENA_FIND_NONE:
        STC
ARENA_FIND_DONE:
        POP     ES
        POP     DI
        POP     CX
        RET
ARENA_FIND_BYTE ENDP

; An empty text, the clipboard kept.
CLEAR_TEXT PROC NEAR
        XOR     AX,AX
        MOV     PREFIX_LEN,AX
        MOV     PREFIX_LEN+2,AX
        MOV     SUFFIX_LEN,AX
        MOV     SUFFIX_LEN+2,AX
        MOV     VIEW_LINE,AX
        MOV     VIEW_LINE+2,AX
        MOV     SUFFIX_LINES,AX
        MOV     SUFFIX_LINES+2,AX
        MOV     BUFFER_LENGTH,AX
        MOV     CURSOR_OFFSET,AX
        MOV     TOP_LINE,AX
        MOV     MARK_OUTSIDE,AL
        MOV     WORD PTR BLOCK_MARK,0FFFFH
        RET
CLEAR_TEXT ENDP

; Read the open file (handle BX) as the text: its bytes to the block's top,
; up to an end-of-file mark (1Ah); with no tab in them they stay there as
; the text after the view, otherwise every tab becomes the blanks to the
; next Tab Stops column on their way to the block's start (DOS 5 EDIT keeps
; no tabs, see SAVE_TEXT_BUFFER for their way back). Carry: FILE_ERROR is a
; DOS error, or ERROR_MEMORY for a text larger than the block or with a line
; longer than LINE_LIMIT; the text is then empty.
LOAD_TEXT PROC NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    BP
        PUSH    ES
        MOV     BYTE PTR TAB_FILE,0
        CALL    CLEAR_TEXT
        CALL    CLIP_RECLAIM
        MOV     AX,4202H
        XOR     CX,CX
        XOR     DX,DX
        INT     21H
        JC      LOAD_TEXT_DOS
        MOV     SI,AX
        MOV     DI,DX                   ; DI:SI = the file's size
        MOV     AX,4200H
        XOR     CX,CX
        XOR     DX,DX
        INT     21H
        JC      LOAD_TEXT_DOS
        CALL    CLIP_BASE
        SUB     AX,SI
        SBB     DX,DI
        JNB     LOAD_TEXT_FITS
        JMP     LOAD_TEXT_MEMORY
LOAD_TEXT_DOS:
        CALL    NOTE_DOS_ERROR
        JMP     LOAD_TEXT_FAILED
LOAD_TEXT_FITS:
        MOV     LOAD_AT,AX
        MOV     LOAD_AT+2,DX
        MOV     LOAD_LEFT,SI
        MOV     LOAD_LEFT+2,DI
        XOR     AX,AX
        MOV     LOAD_READ,AX
        MOV     LOAD_READ+2,AX
LOAD_TEXT_READ:
        MOV     CX,8000H
        CMP     WORD PTR LOAD_LEFT+2,0
        JNE     LOAD_TEXT_CHUNK
        CMP     LOAD_LEFT,CX
        JAE     LOAD_TEXT_CHUNK
        MOV     CX,LOAD_LEFT
        JCXZ    LOAD_TEXT_IN
LOAD_TEXT_CHUNK:
        MOV     AX,LOAD_AT
        MOV     DX,LOAD_AT+2
        ADD     AX,LOAD_READ
        ADC     DX,LOAD_READ+2
        CALL    ARENA_ADDRESS
        PUSH    DS
        MOV     DS,AX
        MOV     AH,3FH
        INT     21H
        POP     DS
        JC      LOAD_TEXT_DOS
        ADD     LOAD_READ,AX
        ADC     WORD PTR LOAD_READ+2,0
        SUB     LOAD_LEFT,AX
        SBB     WORD PTR LOAD_LEFT+2,0
        CMP     AX,CX
        JAE     LOAD_TEXT_READ
LOAD_TEXT_IN:
        MOV     AX,LOAD_READ            ; up to an end-of-file mark
        MOV     COUNT_LEN,AX
        MOV     AX,LOAD_READ+2
        MOV     COUNT_LEN+2,AX
        MOV     AX,LOAD_AT
        MOV     DX,LOAD_AT+2
        MOV     BL,1AH
        CALL    ARENA_FIND_BYTE
        JC      LOAD_TEXT_WHOLE
        SUB     AX,LOAD_AT
        SBB     DX,LOAD_AT+2
        MOV     LOAD_READ,AX
        MOV     LOAD_READ+2,DX
LOAD_TEXT_WHOLE:
        MOV     AX,LOAD_READ
        MOV     COUNT_LEN,AX
        MOV     AX,LOAD_READ+2
        MOV     COUNT_LEN+2,AX
        MOV     AX,LOAD_AT
        MOV     DX,LOAD_AT+2
        MOV     BL,9
        CALL    ARENA_FIND_BYTE
        JNC     LOAD_TEXT_TABS
        MOV     AX,LOAD_READ            ; no tab: the text after the view,
        MOV     DX,LOAD_READ+2          ; moved up to the clipboard
        MOV     SUFFIX_LEN,AX
        MOV     SUFFIX_LEN+2,DX
        MOV     MOVE_LEN,AX
        MOV     MOVE_LEN+2,DX
        MOV     COUNT_LEN,AX
        MOV     COUNT_LEN+2,DX
        MOV     AX,LOAD_AT
        MOV     MOVE_SRC,AX
        MOV     AX,LOAD_AT+2
        MOV     MOVE_SRC+2,AX
        CALL    SUFFIX_START
        MOV     MOVE_DST,AX
        MOV     MOVE_DST+2,DX
        CALL    ARENA_MOVE
        CALL    ARENA_COUNT_LF
        MOV     SUFFIX_LINES,AX
        MOV     SUFFIX_LINES+2,DX
        CALL    SUFFIX_START
        JMP     SHORT LOAD_TEXT_CHECK
LOAD_TEXT_TABS:
        MOV     BYTE PTR TAB_FILE,1
        CALL    EXPAND_LOADED_TABS
        JC      LOAD_TEXT_MEMORY
        MOV     AX,PREFIX_LEN
        MOV     COUNT_LEN,AX
        MOV     AX,PREFIX_LEN+2
        MOV     COUNT_LEN+2,AX
        XOR     AX,AX
        XOR     DX,DX
        CALL    ARENA_COUNT_LF
        MOV     VIEW_LINE,AX
        MOV     VIEW_LINE+2,DX
        MOV     AX,PREFIX_LEN
        MOV     COUNT_LEN,AX
        MOV     AX,PREFIX_LEN+2
        MOV     COUNT_LEN+2,AX
        XOR     AX,AX
        XOR     DX,DX
LOAD_TEXT_CHECK:                        ; DX:AX = the text's one run
        CALL    DOC_LENGTH
        MOV     COUNT_LEN,AX
        MOV     COUNT_LEN+2,DX
        MOV     AX,PREFIX_LEN
        OR      AX,PREFIX_LEN+2
        MOV     AX,0
        MOV     DX,AX
        JNZ     LOAD_TEXT_RUN
        CALL    SUFFIX_START
LOAD_TEXT_RUN:
        CALL    ARENA_LONG_LINE
        JC      LOAD_TEXT_MEMORY
        XOR     AX,AX
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,AX
        MOV     G_TOP,AX
        MOV     G_TOP+2,AX
        MOV     G_FLAGS,AL
        CALL    REBUILD_VIEW
        CLC
        JMP     SHORT LOAD_TEXT_DONE
LOAD_TEXT_MEMORY:
        MOV     WORD PTR FILE_ERROR,ERROR_MEMORY
LOAD_TEXT_FAILED:
        MOV     BYTE PTR TAB_FILE,0
        CALL    CLEAR_TEXT
        STC
LOAD_TEXT_DONE:
        POP     ES
        POP     BP
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
LOAD_TEXT ENDP

; The file's LOAD_READ bytes at LOAD_AT, tabs expanded, to the block's start
; as the text before the view: each written byte stays behind the next one to
; read. Carry: the expanded text does not fit.
EXPAND_LOADED_TABS PROC NEAR
        XOR     AX,AX
        MOV     PREFIX_LEN,AX           ; where the next byte goes
        MOV     PREFIX_LEN+2,AX
        XOR     BP,BP                   ; the column
EXPAND_TABS_NEXT:
        MOV     AX,LOAD_READ
        OR      AX,LOAD_READ+2
        JZ      EXPAND_TABS_DONE
        MOV     AX,LOAD_AT
        MOV     DX,LOAD_AT+2
        CALL    ARENA_ES_DI
        MOV     BL,ES:[DI]
        ADD     LOAD_AT,1
        ADC     WORD PTR LOAD_AT+2,0
        SUB     LOAD_READ,1
        SBB     WORD PTR LOAD_READ+2,0
        MOV     CX,1
        CMP     BL,9
        JNE     EXPAND_TABS_WIDTH_READY
        MOV     AX,BP
        XOR     DX,DX
        XOR     CX,CX
        MOV     CL,TAB_STOPS
        DIV     CX
        SUB     CX,DX                   ; blanks to the next stop
        MOV     BL,' '
EXPAND_TABS_WIDTH_READY:
        MOV     AX,LOAD_AT              ; room: written + CX <= next to read
        MOV     DX,LOAD_AT+2
        SUB     AX,PREFIX_LEN
        SBB     DX,PREFIX_LEN+2
        JNZ     EXPAND_TABS_ROOM
        CMP     AX,CX
        JB      EXPAND_TABS_FULL
EXPAND_TABS_ROOM:
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        CALL    ARENA_ES_DI
        MOV     AL,BL
        ADD     PREFIX_LEN,CX
        ADC     WORD PTR PREFIX_LEN+2,0
        CMP     AL,10
        JNE     EXPAND_TABS_NOT_LF
        XOR     BP,BP
        JMP     SHORT EXPAND_TABS_STORE
EXPAND_TABS_NOT_LF:
        CMP     AL,13
        JE      EXPAND_TABS_STORE
        ADD     BP,CX
EXPAND_TABS_STORE:
        REP     STOSB
        JMP     EXPAND_TABS_NEXT
EXPAND_TABS_FULL:
        STC
        RET
EXPAND_TABS_DONE:
        CLC
        RET
EXPAND_LOADED_TABS ENDP

; DX:AX = the line ends in the whole text.
TOTAL_LINE_ENDS PROC NEAR
        PUSH    BX
        CALL    FIND_LAST_LINE
        XOR     DX,DX
        ADD     AX,VIEW_LINE
        ADC     DX,VIEW_LINE+2
        ADD     AX,SUFFIX_LINES
        ADC     DX,SUFFIX_LINES+2
        POP     BX
        RET
TOTAL_LINE_ENDS ENDP

; BL = the text's last byte (0 when it is empty).
DOC_LAST_BYTE PROC NEAR
        PUSH    AX
        PUSH    DX
        PUSH    DI
        PUSH    ES
        XOR     BL,BL
        MOV     AX,SUFFIX_LEN
        OR      AX,SUFFIX_LEN+2
        JZ      DOC_LAST_NOT_AFTER
        CALL    CLIP_BASE
        JMP     SHORT DOC_LAST_READ
DOC_LAST_NOT_AFTER:
        MOV     DI,BUFFER_LENGTH
        OR      DI,DI
        JZ      DOC_LAST_NOT_VIEW
        MOV     BL,BUFFER[DI-1]
        JMP     SHORT DOC_LAST_DONE
DOC_LAST_NOT_VIEW:
        MOV     AX,PREFIX_LEN
        MOV     DX,PREFIX_LEN+2
        MOV     DI,AX
        OR      DI,DX
        JZ      DOC_LAST_DONE
DOC_LAST_READ:
        SUB     AX,1
        SBB     DX,0
        CALL    ARENA_ES_DI
        MOV     BL,ES:[DI]
DOC_LAST_DONE:
        POP     ES
        POP     DI
        POP     DX
        POP     AX
        RET
DOC_LAST_BYTE ENDP

; DX:AX = a line of the whole text: the view made around it when it is not
; in it; AX returns its line in the view (the last one, when the text has
; fewer lines).
VIEW_TO_LINE PROC NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     BX,AX
        MOV     CX,DX
        SUB     AX,VIEW_LINE
        SBB     DX,VIEW_LINE+2
        JB      VIEW_TO_LINE_FAR
        OR      DX,DX
        JNZ     VIEW_TO_LINE_FAR
        PUSH    AX
        CALL    FIND_LAST_LINE
        MOV     DX,AX
        POP     AX
        CMP     AX,DX
        JBE     VIEW_TO_LINE_DONE
VIEW_TO_LINE_FAR:
        CALL    SAVE_POSITIONS
        MOV     AX,BX
        MOV     DX,CX
        CALL    FLAT_LINE_AT
        MOV     G_CURSOR,AX
        MOV     G_CURSOR+2,DX
        CALL    REBUILD_VIEW
        MOV     AX,BX
        MOV     DX,CX
        SUB     AX,VIEW_LINE
        SBB     DX,VIEW_LINE+2
        JNB     VIEW_TO_LINE_AFTER
        XOR     AX,AX
        JMP     SHORT VIEW_TO_LINE_DONE
VIEW_TO_LINE_AFTER:
        PUSH    AX
        CALL    FIND_LAST_LINE
        MOV     BX,AX
        POP     AX
        OR      DX,DX
        JNZ     VIEW_TO_LINE_LAST
        CMP     AX,BX
        JBE     VIEW_TO_LINE_DONE
VIEW_TO_LINE_LAST:
        MOV     AX,BX
VIEW_TO_LINE_DONE:
        POP     DX
        POP     CX
        POP     BX
        RET
VIEW_TO_LINE ENDP

; Give the memory blocks back to DOS (at exit; the screen is restored).
FREE_MEMORY PROC NEAR
        PUSH    AX
        PUSH    ES
        MOV     AX,ARENA_SEG
        CALL    FREE_BLOCK
        MOV     AX,DOSUI_SAVE_SEGMENT
        CALL    FREE_BLOCK
        POP     ES
        POP     AX
        RET
FREE_MEMORY ENDP

FREE_BLOCK PROC NEAR
        OR      AX,AX
        JZ      FREE_BLOCK_DONE
        MOV     ES,AX
        MOV     AH,49H
        INT     21H
FREE_BLOCK_DONE:
        RET
FREE_BLOCK ENDP

MENU_BAR_LETTER_COLUMNS DB 3,9,15,23,74
MENU_BAR_STATUS_TEXT DB ' F1=Help   Enter=Display Menu   Esc=Cancel   Arrow=Next Item',0
MENU_TEXT       DB '   File  Edit  Search  Options',44 DUP (' '),'Help',0
FILE_HEADER_TEXT DB ' File ',0
EDIT_HEADER_TEXT DB ' Edit ',0
SEARCH_HEADER_TEXT DB ' Search ',0
OPTIONS_HEADER_TEXT DB ' Options ',0
HELP_HEADER_TEXT DB ' Help ',0
UNTITLED_TEXT   DB 'Untitled',0
STATUS_LEFT_TEXT DB ' CK-DOS Editor  <F1=Help> Press ALT to activate menus         ',0
STATUS_POSITION_TEXT DB '00001:001',0
WELCOME_TITLE_TEXT DB 'Welcome to the CK-DOS Editor',0
WELCOME_COPYRIGHT_TEXT DB 'Copyright (C) C. Klukas / CK-DOS Project, 2026.',0
WELCOME_RIGHTS_TEXT DB 'All rights reserved.',0
WELCOME_GUIDE_TEXT DB '< Press Enter to see the Survival Guide >',0
WELCOME_DISMISS_TEXT DB '< Press ESC to clear this dialog box >',0
WELCOME_STATUS_TEXT DB ' F1=Help   Enter=Execute   Esc=Cancel   Tab=Next Field   Arrow=Next Item       ',0
MENU_STATUS_PREFIX DB ' F1=Help ',0
ABOUT_LINE1     DB 'CK-DOS Editor',0
ABOUT_LINE2     DB 'Version 1.0',0
ABOUT_BUTTON_TEXT DB '<  OK  >',0
HELP_PATH_TITLE_TEXT DB ' Help Path ',0
HELP_PATH_LABEL_TEXT DB 'Location (path) of EDIT.HLP file:',0
HELP_PATH_BUTTONS_TEXT DB '< OK >       < Cancel >       < Help >',0
HELP_PATH_RECT  DB 7,12,16,66,70H,70H
ERROR_TEXT      DB 'Path/File access error',0
FILE_ERROR_BUTTONS_TEXT DB '<  OK  >   < Help >',0
SAVED_TEXT      DB 'Saved',0
CHANGE_COMPLETE_TEXT DB 'Change complete',0
EXIT_TEXT       DB 'Loaded file is not saved. Save it now?',0
FIND_TITLE_TEXT DB ' Find ',0
CHANGE_TITLE_TEXT DB ' Change ',0
FIND_TEXT       DB 'Find What:',0
REPLACE_TEXT    DB 'Change To:',0
FIND_OPTIONS_TEXT DB '[ ] Match Upper/Lowercase        [ ] Whole Word',0
CHANGE_OPTIONS_TEXT DB '[ ] Match Upper/Lowercase       [ ] Whole Word',0
FIND_BUTTONS_TEXT DB '< OK >         < Cancel >        < Help >',0
CHANGE_BUTTONS_TEXT DB '< Find and Verify > < Change All > < Cancel > < Help >',0
NO_MEMORY_TEXT  DB 'Insufficient memory',13,10,'$'
NOT_FOUND_TEXT  DB 'Match not found',0
FILE_NOT_FOUND_TEXT DB 'File not found',0
BAD_NAME_TEXT   DB 'Bad file name',0
DISK_FULL_TEXT  DB 'Disk full',0
DISK_NOT_READY_TEXT DB 'Disk not ready',0
PERMISSION_TEXT DB 'Permission denied',0
OUT_OF_MEMORY_TEXT DB 'Out of memory',0
PRESS_ANY_KEY_TEXT DB 'Press any key to continue',0
BAD_NAME_CHARACTERS DB '"+,;=[]|<>*?'
BAD_NAME_COUNT  EQU $-BAD_NAME_CHARACTERS
SAVE_AS_TITLE_TEXT DB ' Save As ',0
FILE_NAME_TEXT  DB 'File Name:',0
SAVE_AS_DIRS_TEXT DB 'Dirs/Drives',0
SAVE_AS_BUTTONS_TEXT DB '< OK >    < Cancel >    < Help >',0
OVERWRITE_TEXT  DB 'File already exists. Overwrite?',0
FILE_CONFIRM_BUTTONS_TEXT DB '< Yes >   <  No  >   <Cancel>   < Help >',0
FILE_MENU_NEW_TEXT DB ' New',0
FILE_MENU_OPEN_TEXT DB ' Open...',0
FILE_MENU_SAVE_TEXT DB ' Save',0
FILE_MENU_SAVE_AS_TEXT DB ' Save As...',0
FILE_MENU_PRINT_TEXT DB ' Print...',0
FILE_MENU_EXIT_TEXT DB ' Exit',0
EDIT_MENU_COPY_TEXT DB ' Copy        Ctrl+Ins',0
EDIT_MENU_CUT_TEXT  DB ' Cut        Shift+Del',0
EDIT_MENU_PASTE_TEXT DB ' Paste      Shift+Ins',0
EDIT_MENU_CLEAR_TEXT DB ' Clear            Del',0
SEARCH_MENU_FIND_TEXT DB ' Find...',0
SEARCH_MENU_NEXT_TEXT DB ' Repeat Last Find      F3',0
SEARCH_MENU_REPLACE_TEXT DB ' Change...',0
OPTIONS_MENU_DISPLAY_TEXT DB ' Display...',0
OPTIONS_MENU_HELP_PATH_TEXT DB ' Help Path...',0
HELP_MENU_START_TEXT DB ' Getting Started',0
HELP_MENU_KEYBOARD_TEXT DB ' Keyboard',0
HELP_MENU_ABOUT_TEXT DB ' About...',0

MENU_STATUS_FILE_NEW DB 'Removes currently loaded file from memory',0
MENU_STATUS_FILE_OPEN DB 'Opens an existing file',0
MENU_STATUS_FILE_SAVE DB 'Saves the current file',0
MENU_STATUS_FILE_SAVE_AS DB 'Saves the current file under a new name',0
MENU_STATUS_FILE_PRINT DB 'Prints the current file',0
MENU_STATUS_FILE_EXIT DB 'Exits the CK-DOS Editor',0
MENU_STATUS_EDIT_CUT DB 'Deletes selected text and copies it to buffer',0
MENU_STATUS_EDIT_COPY DB 'Copies selected text to buffer',0
MENU_STATUS_EDIT_PASTE DB 'Inserts text from buffer',0
MENU_STATUS_EDIT_CLEAR DB 'Deletes selected text',0
MENU_STATUS_SEARCH_FIND DB 'Finds specified text',0
MENU_STATUS_SEARCH_NEXT DB 'Repeats the last find',0
MENU_STATUS_SEARCH_CHANGE DB 'Finds and changes specified text',0
MENU_STATUS_OPTIONS_DISPLAY DB 'Changes display attributes',0
MENU_STATUS_OPTIONS_HELP_PATH DB 'Changes the help path',0
MENU_STATUS_HELP_START DB 'Displays information on loading and using the CK-DOS Editor',0
MENU_STATUS_HELP_KEYBOARD DB 'Displays keyboard help',0
MENU_STATUS_HELP_ABOUT DB 'Displays program information',0
MENU_STATUS_UNUSED DB 0

MENU_STATUS_POINTERS DW OFFSET MENU_STATUS_FILE_NEW
        DW OFFSET MENU_STATUS_FILE_OPEN,OFFSET MENU_STATUS_FILE_SAVE
        DW OFFSET MENU_STATUS_FILE_SAVE_AS,OFFSET MENU_STATUS_FILE_PRINT
        DW OFFSET MENU_STATUS_FILE_EXIT
        DW OFFSET MENU_STATUS_EDIT_CUT,OFFSET MENU_STATUS_EDIT_COPY
        DW OFFSET MENU_STATUS_EDIT_PASTE,OFFSET MENU_STATUS_EDIT_CLEAR
        DW OFFSET MENU_STATUS_UNUSED,OFFSET MENU_STATUS_UNUSED
        DW OFFSET MENU_STATUS_SEARCH_FIND,OFFSET MENU_STATUS_SEARCH_NEXT
        DW OFFSET MENU_STATUS_SEARCH_CHANGE,OFFSET MENU_STATUS_UNUSED
        DW OFFSET MENU_STATUS_UNUSED,OFFSET MENU_STATUS_UNUSED
        DW OFFSET MENU_STATUS_OPTIONS_DISPLAY,OFFSET MENU_STATUS_OPTIONS_HELP_PATH
        DW OFFSET MENU_STATUS_UNUSED,OFFSET MENU_STATUS_UNUSED
        DW OFFSET MENU_STATUS_UNUSED,OFFSET MENU_STATUS_UNUSED
        DW OFFSET MENU_STATUS_HELP_START,OFFSET MENU_STATUS_HELP_KEYBOARD
        DW OFFSET MENU_STATUS_HELP_ABOUT,OFFSET MENU_STATUS_UNUSED
        DW OFFSET MENU_STATUS_UNUSED,OFFSET MENU_STATUS_UNUSED
; EDIT /? in the layout of DOS 5's own (captured 2026-11-11), without the
; display switches /B /G /H /NOHI, which this EDIT accepts but does not act
; on (PARSE_COMMAND_LINE).
HELP_DOS        DB 'Starts the CK-DOS Editor, which creates and changes ASCII files.',13,10
                DB ' ',13,10
                DB 'EDIT [[drive:][path]filename] [/B] [/G] [/H] [/NOHI]',13,10
                DB ' ',13,10
                DB '  [drive:][path]filename  Specifies the ASCII file to edit.',13,10
                DB '  /B          Uses black-and-white attributes on a colour display.',13,10
                DB '  /G          Updates a CGA screen without waiting for retrace.',13,10
                DB '  /H          Shows as many text rows as the display allows.',13,10
                DB '  /NOHI       Uses only the eight colours without high intensity.',13,10,'$'

ABOUT_RECT      DB 8,13,16,65,70H,70H
SAVED_RECT      DB 10,29,12,40,2FH,27H
FILE_CONFIRM_RECT DB 9,17,15,62,70H,70H
FIND_RECT       DB 7,10,17,68,70H,70H
REPLACE_RECT    DB 5,11,18,68,70H,70H
FILE_NAME_RECT  DB 3,19,20,60,70H,70H
FILE_MENU_RECT  DB 1,1,10,18,70H,70H
EDIT_MENU_RECT  DB 1,7,6,30,70H,70H
SEARCH_MENU_RECT DB 1,13,5,40,70H,70H
OPTIONS_MENU_RECT DB 1,21,4,39,70H,70H
HELP_MENU_RECT  DB 1,59,6,77,70H,70H

; Drop-down hit geometry for MENU_ITEM_FROM_CELL: the frame descriptor of
; the open menu and its row-to-item table (items start on row 2, FFh marks
; a separator row).
MENU_HIT_RECTS  DW OFFSET FILE_MENU_RECT,OFFSET EDIT_MENU_RECT
                DW OFFSET SEARCH_MENU_RECT,OFFSET OPTIONS_MENU_RECT
                DW OFFSET HELP_MENU_RECT
MENU_HIT_ROWS   DW OFFSET FILE_MENU_ROW_ITEMS,OFFSET EDIT_MENU_ROW_ITEMS
                DW OFFSET SEARCH_MENU_ROW_ITEMS,OFFSET OPTIONS_MENU_ROW_ITEMS
                DW OFFSET HELP_MENU_ROW_ITEMS
FILE_MENU_ROW_ITEMS DB 0,1,2,3,0FFH,4,0FFH,5
EDIT_MENU_ROW_ITEMS DB 0,1,2,3
SEARCH_MENU_ROW_ITEMS DB 0,1,2
OPTIONS_MENU_ROW_ITEMS DB 0,1
HELP_MENU_ROW_ITEMS DB 0,1,0FFH,2

MENU_COMMANDS   DB COMMAND_NEW,COMMAND_OPEN,COMMAND_SAVE,COMMAND_SAVE_AS
                DB COMMAND_PRINT,COMMAND_EXIT
                DB COMMAND_CUT,COMMAND_COPY,COMMAND_PASTE,COMMAND_CLEAR
                DB COMMAND_NONE,COMMAND_NONE
                DB COMMAND_FIND,COMMAND_FIND_NEXT,COMMAND_REPLACE
                DB COMMAND_NONE,COMMAND_NONE,COMMAND_NONE
                DB COMMAND_DISPLAY,COMMAND_HELP_PATH
                DB COMMAND_NONE,COMMAND_NONE,COMMAND_NONE,COMMAND_NONE
                DB COMMAND_GETTING_STARTED,COMMAND_KEYBOARD,COMMAND_ABOUT
                DB COMMAND_NONE,COMMAND_NONE,COMMAND_NONE

FILE_NAME       DB 64 DUP (0)
SWITCH_WORD     DB 4 DUP (0)
EDIT_SWITCHES   DB 0                    ; 1 /B, 2 /G, 4 /H, 8 /NOHI
DISPLAY_NAME    DB 64 DUP (0)
BUFFER_LENGTH   DW 0
CURSOR_OFFSET   DW 0
CURRENT_LINE    DW 0
CURRENT_COLUMN  DW 0
TOP_LINE        DW 0
SCROLL_THUMB_ROW DB TEXT_FIRST_ROW+1
PREFERRED_COLUMN DW 0
VIRTUAL_COLUMN  DW 0
PREFERRED_COLUMN_VALID DB 0
VIRTUAL_COLUMN_VALID DB 0
MODIFIED        DB 0
INSERT_MODE     DB 1
MODIFIER_STATE  DB 0
SHIFT_SELECTION DB 0
STARTUP_ERROR   DB 0
SAVE_DISABLED   DB 0
SAVE_AS_NAME    DB 0
SAVE_AS_REQUESTED DB 0
SAVE_AS_RETRY   DB 0
SAVE_CANCELLED  DB 0
MENU_ACTIVE     DB MENU_FILE
MENU_BAR_ONLY   DB 0
FIELD_POINTER   DW 0
FIELD_LENGTH    DW 0
FIELD_CAPACITY  DB 0
FIELD_SELECT_ALL DB 0
SEARCH_PATTERN  DB 32 DUP (0)
REPLACE_PATTERN DB 32 DUP (0)
SEARCH_HIGHLIGHT_START DW 0FFFFH
SEARCH_HIGHLIGHT_END DW 0FFFFH
REPLACE_ALL_REQUESTED DB 0
REPLACE_SEARCH_LENGTH DW 0
REPLACE_REPLACEMENT_LENGTH DW 0
REPLACE_MATCH_COUNT DW 0,0
REPLACE_SAVED_CURSOR DW 0,0
MOUSE_CLICK_CELL DW 0                   ; the press EDIT_MOUSE handles
MOUSE_REPEAT    DB 0                    ; armed: repeat while held
MOUSE_REPEATING DB 0                    ; EDIT_MOUSE runs a repeat
MOUSE_THIS_REPEAT DB 0
MOUSE_REPEAT_CELL DW 0
MOUSE_REPEAT_DUE DW 0
MOUSE_DOUBLE    DB 0
MOUSE_LAST_CELL DW 0FFFFH
MOUSE_LAST_TICK DW 0
REPLACE_OLD_COLUMN DW 0
REPLACE_OLD_VIRTUAL DW 0
REPLACE_WAS_VIRTUAL DB 0
BLOCK_MARK      DW 0FFFFH
MATCH_CASE      DB 0
TEXT_ATTR       DB 17H
SELECT_ATTR     DB 71H
SCROLL_BARS     DB 1
TEXT_ROW_COUNT  DW 21                   ; 22 with Scroll Bars off
TEXT_BOTTOM_ROW DB 22                   ; 23 with Scroll Bars off
; The screen's rows (/H: 50 on a VGA, as in DOS 5; 43 on an EGA): the text
; rows, the horizontal scroll bar and the status line follow the last row,
; dialogs laid out for 25 rows are centred (Change at the bottom).
TEXT_ROWS       DW 21
FAR_JUMP_LINES  DW 5                    ; a quarter of the text rows: 11 at 50
TEXT_LAST_ROW   DB 22
SCROLL_ROW      DB 23
STATUS_ROW      DB 24
STATUS_HIT_ROW  DB 24                   ; the status line, in dialog rows
DIALOG_ROW_OFFSET DB 0
VERIFY_ROW_OFFSET DB 0
BOX_SAVED_OFFSET DB 0
TAB_STOPS       DB 8
INI_DIRTY       DB 0
INI_FILE_NAME   DB 'QBASIC.INI',0
INI_PATH_VARIABLE DB 'PATH='
INI_PATH        DB 80 DUP (0)
INI_SCRATCH_PATH DB 80 DUP (0)
; The QBASIC.INI image, holding DOS 5's defaults until a file is read.
INI_IMAGE       DB 'QBP',4
INI_BACKGROUND  DW 1
INI_FOREGROUND  DW 7
INI_FOREGROUND_BRIGHT DW 0
INI_BACKGROUND_BRIGHT DW 0
INI_STATEMENT_SETS DW 1,7,1,0,4,7,0,0   ; QBasic's statement/breakpoint sets
INI_TAB_STOPS   DW 8
INI_SCROLL_BARS DW 1
                DB 1,0,1,1,1,0,0,1,0,0,1,0,0
INI_HELP_PATH_LENGTH DW 1
INI_HELP_PATH   DB INI_HELP_PATH_MAX DUP (0)
HELP_PATH_EDIT  DB HELP_PATH_FIELD_WIDTH+1 DUP (0)
PRINT_CHOICE    DB 1
PRINT_RECT      DB 7,20,15,53,70H,70H
PRINT_TITLE_TEXT DB ' Print ',0
PRINT_SELECTED_TEXT DB '( ) Selected Text Only',0
PRINT_DOCUMENT_TEXT DB '( ) Complete Document',0
PRINT_BUTTONS_TEXT DB '< OK >  < Cancel >  < Help >',0
PRINT_CONTROLS  DB DLG_RADIO,9,27,2
                DW OFFSET PRINT_CHOICE
                DB 0,0
                DB DLG_BUTTON,14,23,6
                DW 1
                DB 0,2
                DB DLG_BUTTON,14,31,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,14,43,8
                DW DLG_HELP
                DB 0,2
DISPLAY_FOREGROUND DW 7
DISPLAY_BACKGROUND DW 1
DISPLAY_FOREGROUND_TOP DW 0
DISPLAY_BACKGROUND_TOP DW 0
DISPLAY_ATTR    DB 17H
DISPLAY_FOREGROUND_COUNT DW 16
DISPLAY_BACKGROUND_COUNT DW 8
MODE_MAP        DB 256 DUP (0)
MODE_STATEMENT_SETS DW OFFSET INI_STATEMENT_SETS
; DOS 5's colour attribute and the mode's, measured on the same 48 screens
; (normal, /B, /NOHI, /B /NOHI); 1Fh, 4Fh, 47h, 2Fh and 27h are CK-DOS's own
; boxes, given the mode's dialog or text equivalent.
MONO_PAIRS      DB 08H,07H, 0AH,7FH, 17H,07H, 30H,70H, 3FH,70H, 71H,70H, 78H,70H
                DB 1FH,0FH, 4FH,70H, 47H,70H, 2FH,70H, 27H,70H, 0,0
NOHI_PAIRS      DB 0AH,02H, 0FH,02H, 3FH,30H, 78H,76H, 7FH,74H
                DB 1FH,17H, 4FH,47H, 2FH,27H, 0,0
MONO_NOHI_PAIRS DB 08H,07H, 0AH,07H, 0FH,70H, 17H,07H, 30H,70H, 3FH,70H, 71H,70H
                DB 78H,70H, 7FH,07H, 1FH,70H, 4FH,70H, 47H,70H, 2FH,70H, 27H,70H, 0,0
MONO_STATEMENT_SETS DW 0,7,1,0,7,0,0,0
NOHI_STATEMENT_SETS DW 1,2,0,0,4,7,0,0
MONO_NOHI_STATEMENT_SETS DW 7,0,0,0,7,0,0,0
MONO_COLOURS    DB 'Black',0,0,0,'White',0,0,0,'Gray',0,0,0,0,'BrWhite',0
MONO_VALUES     DB 0,7,8,15

DISPLAY_SCROLL_BARS DB 1
DISPLAY_TAB_TEXT DB 3 DUP (0)
DISPLAY_RECT    DB 1,10,22,69,70H,70H
DISPLAY_COLORS_BOX DB 2,12,15,67
DISPLAY_OPTIONS_BOX DB 17,12,19,67
DISPLAY_FOREGROUND_BOX DB 4,41,13,51
DISPLAY_BACKGROUND_BOX DB 4,54,13,64
DISPLAY_TEXTS   DB 3,42,'Foreground',0
                DB 3,55,'Background',0
                DB 18,16,'[ ] Scroll Bars',0
                DB 18,49,'Tab Stops:',0
                DW 0
DISPLAY_PREVIEW_LINE1 DB 'Set colors for the',0
DISPLAY_PREVIEW_LINE2 DB 'text editor window:',0
DISPLAY_TITLES  DB 1,35,' Display ',0
                DB 2,36,' Colors ',0
                DB 17,31,' Display Options ',0
                DW 0
DISPLAY_BUTTONS_TEXT DB '< OK >        < Cancel >        < Help >',0
DISPLAY_CONTROLS DB DLG_LIST,5,42,8
                DW OFFSET DISPLAY_FOREGROUND_LIST
                DB 22H,0
                DB DLG_LIST,5,55,8
                DW OFFSET DISPLAY_BACKGROUND_LIST
                DB 30H,0
                DB DLG_CHECK,18,16,3
                DW OFFSET DISPLAY_SCROLL_BARS
                DB 1FH,0
                DB DLG_FIELD,18,60,2
                DW OFFSET DISPLAY_TAB_TEXT
                DB 14H,2
                DB DLG_BUTTON,21,20,6
                DW 1
                DB 0,2
                DB DLG_BUTTON,21,34,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,21,52,8
                DW DLG_HELP
                DB 0,2
DISPLAY_FOREGROUND_LIST DW OFFSET COLOR_NAMES
                DB 8,1
                DW OFFSET DISPLAY_FOREGROUND_COUNT,OFFSET DISPLAY_FOREGROUND_TOP
                DW OFFSET DISPLAY_FOREGROUND
                DB 9,51
                DW OFFSET DISPLAY_FOREGROUND_MOVED,0
DISPLAY_BACKGROUND_LIST DW OFFSET COLOR_NAMES
                DB 8,1
                DW OFFSET DISPLAY_BACKGROUND_COUNT,OFFSET DISPLAY_BACKGROUND_TOP
                DW OFFSET DISPLAY_BACKGROUND
                DB 9,64
                DW OFFSET DISPLAY_BACKGROUND_MOVED,0
COLOR_NAMES     DB 'Black',0,0,0,'Blue',0,0,0,0,'Green',0,0,0,'Cyan',0,0,0,0
                DB 'Red',0,0,0,0,0,'Magenta',0,'Brown',0,0,0,'White',0,0,0
                DB 'Gray',0,0,0,0,'BrBlue',0,0,'BrGreen',0,'BrCyan',0,0
                DB 'BrRed',0,0,0,'Pink',0,0,0,0,'Yellow',0,0,'BrWhite',0
OPEN_NAME       DB 64 DUP (0)
OPEN_PATTERN    DB '*.TXT',0,7 DUP (0)
ALL_FILES_PATTERN DB '*.*',0
OPEN_PATH       DB 68 DUP (0)
OPEN_DTA        DB 43 DUP (0)
OPEN_FCB        DB 37 DUP (0)
DRIVE_PROBE     DB 'A:',0
SORT_BASE       DW 0
SORT_COUNT      DW 0
SORT_I          DW 0
SORT_J          DW 0
OPEN_FILE_COUNT DW 0
OPEN_FILE_TOP   DW 0
OPEN_FILE_SELECTED DW 0FFFFH
OPEN_DIR_COUNT  DW 0
OPEN_DIR_TOP    DW 0
OPEN_DIR_SELECTED DW 0FFFFH
OPEN_RECT       DB 2,6,21,72,70H,70H
OPEN_FILES_BOX  DB 8,8,17,52
OPEN_DIRS_BOX   DB 8,55,17,70
OPEN_TITLE_TEXT DB ' Open ',0
OPEN_FILES_TEXT DB 'Files',0
OPEN_BUTTONS_TEXT DB '< OK >          < Cancel >          < Help >',0
OPEN_CONTROLS   DB DLG_FIELD,4,20,50
                DW OFFSET OPEN_NAME
                DB 31H,50
                DB DLG_LIST,9,9,8
                DW OFFSET OPEN_FILE_LIST
                DB 0,0
                DB DLG_LIST,9,56,8
                DW OFFSET OPEN_DIR_LIST
                DB 0,0
                DB DLG_BUTTON,20,18,6
                DW 1
                DB 0,2
                DB DLG_BUTTON,20,34,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,20,54,8
                DW DLG_HELP
                DB 0,2
OPEN_FILE_LIST  DW OFFSET OPEN_FILE_ENTRIES
                DB 13,3
                DW OFFSET OPEN_FILE_COUNT,OFFSET OPEN_FILE_TOP
                DW OFFSET OPEN_FILE_SELECTED
                DB 13,9
                DW OFFSET OPEN_FILE_CHOSEN,0
OPEN_DIR_LIST   DW OFFSET OPEN_DIR_ENTRIES
                DB 13,1
                DW OFFSET OPEN_DIR_COUNT,OFFSET OPEN_DIR_TOP
                DW OFFSET OPEN_DIR_SELECTED
                DB 14,70
                DW OFFSET OPEN_DIR_CHOSEN,0
OPEN_FILE_ENTRIES DB OPEN_FILE_MAX*13 DUP (0)
OPEN_DIR_ENTRIES DB OPEN_DIR_MAX*13 DUP (0)
WHOLE_WORD      DB 0
SAVED_MATCH_CASE DB 0
SAVED_WHOLE_WORD DB 0
SAVED_SEARCH    DB 32 DUP (0)
CHANGE_COUNT    DW 0
CHANGE_ORIGIN   DW 0,0
CHANGE_LAST     DW 0,0
CHANGE_WRAPPED  DB 0
DLG_TABLE       DW 0
DLG_COUNT       DB 0
DLG_FOCUS       DB 0
DLG_DEFAULT     DB 0
FIELD_POSITION  DW 0
LIST_DRAW_INDEX DW 0
LIST_PAGE       DW 0
LIST_STEP       DW 0
LIST_LAST_SCAN  DB 0
; Control tables: type, row, column, size, data, Alt scan, aux.
HELP_PATH_CONTROLS DB DLG_FIELD,12,15,HELP_PATH_FIELD_WIDTH
                DW OFFSET HELP_PATH_EDIT
                DB 0,HELP_PATH_FIELD_WIDTH
                DB DLG_BUTTON,15,22,6
                DW 1
                DB 0,2
                DB DLG_BUTTON,15,35,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,15,52,8
                DW DLG_HELP
                DB 0,2
FIND_CONTROLS   DB DLG_FIELD,9,24,42
                DW OFFSET SEARCH_PATTERN
                DB 21H,31
                DB DLG_CHECK,13,15,3
                DW OFFSET MATCH_CASE
                DB 32H,0
                DB DLG_CHECK,13,48,3
                DW OFFSET WHOLE_WORD
                DB 11H,0
                DB DLG_BUTTON,16,19,6
                DW 1
                DB 0,2
                DB DLG_BUTTON,16,34,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,16,52,8
                DW DLG_HELP
                DB 0,2
CHANGE_CONTROLS DB DLG_FIELD,7,25,41
                DW OFFSET SEARCH_PATTERN
                DB 21H,31
                DB DLG_FIELD,10,25,41
                DW OFFSET REPLACE_PATTERN
                DB 14H,31
                DB DLG_CHECK,14,16,3
                DW OFFSET MATCH_CASE
                DB 32H,0
                DB DLG_CHECK,14,48,3
                DW OFFSET WHOLE_WORD
                DB 11H,0
                DB DLG_BUTTON,17,13,19
                DW 1
                DB 2FH,2
                DB DLG_BUTTON,17,33,14
                DW 3
                DB 2EH,2
                DB DLG_BUTTON,17,48,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,17,59,8
                DW DLG_HELP
                DB 0,2
VERIFY_CONTROLS DB DLG_BUTTON,22,17,10
                DW 1
                DB 2EH,2                ; C (plan CF: "c" changes)
                DB DLG_BUTTON,22,29,8
                DW 2
                DB 1FH,2                ; S
                DB DLG_BUTTON,22,41,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,22,54,8
                DW DLG_HELP
                DB 0,2
SAVE_AS_CONTROLS DB DLG_FIELD,5,33,24
                DW OFFSET FILE_NAME
                DB 31H,24
                DB DLG_LIST,10,33,7
                DW OFFSET SAVE_AS_DRIVE_LIST
                DB 0,0
                DB DLG_BUTTON,19,26,6
                DW 1
                DB 0,2
                DB DLG_BUTTON,19,36,10
                DW 0
                DB 0,2
                DB DLG_BUTTON,19,50,8
                DW DLG_HELP
                DB 0,2
SAVE_AS_DRIVE_LIST DW OFFSET OPEN_DIR_ENTRIES
                DB 13,1
                DW OFFSET OPEN_DIR_COUNT,OFFSET SAVE_AS_DRIVE_TOP
                DW OFFSET SAVE_AS_DRIVE_SELECTED
                DB 14,47
                DW OFFSET DRIVE_SELECTED_INTO_FIELD,0
SAVE_AS_DRIVE_TOP DW 0
SAVE_AS_DRIVE_SELECTED DW 0FFFFH
SAVE_AS_LIST_BOX DB 9,32,17,47
SAVE_AS_NAVIGATED DB 0
SAVE_AS_HOME    DB 68 DUP (0)
PATH_ERROR_TEXT DB 'Path not found',0
DEVICE_ERROR_TEXT DB 'Device unavailable',0
DEVICE_ERROR_RECT DB 9,22,15,56,70H,70H
DEVICE_ERROR_BUTTONS_TEXT DB '<Retry>   <Cancel>   < Help >',0
DEVICE_ERROR_BRACKETS DB 25,31,35,42,46,53
DEVICE_ERROR_CURSORS DB 26,36,48
DEVICE_ERROR_FOCUS DB 0
; DOS error -> DOS 5's message; the last entry (0) is the fallback.
FILE_ERROR_TABLE DW 2,OFFSET FILE_NOT_FOUND_TEXT,3,OFFSET PATH_ERROR_TEXT
                DW 15,OFFSET DEVICE_ERROR_TEXT,21,OFFSET DISK_NOT_READY_TEXT
                DW 19,OFFSET PERMISSION_TEXT,ERROR_DISK_FULL,OFFSET DISK_FULL_TEXT
                DW ERROR_BAD_NAME,OFFSET BAD_NAME_TEXT
                DW ERROR_MEMORY,OFFSET OUT_OF_MEMORY_TEXT,0,OFFSET ERROR_TEXT
FILE_ERROR      DW 5
STARTUP_ABORT_LAST DB 24
MESSAGE_TEXT    DW 0
MESSAGE_HEADER  DB 0FFH
MESSAGE_REDRAWS DB 0
MESSAGE_FOCUS   DB 0
MESSAGE_WIDTH   DB 25
MESSAGE_TEXT_COLUMN DB 0
MESSAGE_BUTTON_COLUMN DB 0
MESSAGE_RECT    DB 9,0,15,0,70H,70H
EDITOR_ROW_SHIFT DB 0
HELP_TOPIC      DB 0
HELP_TOP        DW 0
HELP_LINES      DW 0
HELP_WINDOW_ROWS DB 0
HELP_LINK       DB 0FFH
HELP_LINK_COUNT DB 0
HELP_LINKS      DB HELP_LINK_MAX*4 DUP (0)
HELP_DEPTH      DB 0
HELP_HISTORY    DB HELP_HISTORY_MAX DUP (0)
HELP_TITLE      DB 48 DUP (0)
DIALOG_HELP_FOCUS DB 0
DIALOG_HELP_RECT DB 0,5,0,72,70H,70H
; Help topics: CK-DOS's own text in DOS 5 EDIT's viewer (HELP_VIEW): their
; titles here, their lines in EDITHLP.ASM.
HELP_TOPICS     DW OFFSET HELP_T0,OFFSET HELP_T1,OFFSET HELP_T2,OFFSET HELP_T3
                DW OFFSET HELP_T4,OFFSET HELP_T5,OFFSET HELP_T6,OFFSET HELP_T7
                DW OFFSET HELP_T8
HELP_SEGMENT    DW 0                    ; the topics' lines (EDITHLP.ASM)
HELP_LINE_BUFFER DB 80 DUP (0)
HELP_T0         DB 'Survival Guide',0
HELP_T1         DB 'Getting Started',0
HELP_T2         DB 'Using Help',0
HELP_T3         DB 'Menus and Commands',0
HELP_T4         DB 'Dialog Boxes',0
HELP_T5         DB 'Keyboard',0
HELP_T6         DB 'Cursor Movement Keys',0
HELP_T7         DB 'Editing Keys',0
HELP_T8         DB 'Selecting and Copying',0
HELP_BACK_TEXT  DB 'Back',0
HELP_NAVIGATION_LINE DB '  ',11H,'Getting Started',10H,'  ',11H,'Keyboard',10H
                DB '  ',11H,'Back',10H,0
HELP_RULE_LINE  DB 2,0
HELP_TITLE_PREFIX DB ' HELP: ',0
HELP_STATUS_TEXT DB ' <F1=Help> <F6=Window> <Esc=Cancel> <Ctrl+F1=Next> <Alt+F1=Back>',0
HELP_STATUS_ITEMS DB 1,9
                DW 3B00H                ; F1
                DB 11,21
                DW 4000H                ; F6
                DB 23,34
                DW 011BH                ; Esc
                DB 36,49
                DW 5E00H                ; Ctrl+F1
                DB 51,63
                DW 6800H                ; Alt+F1
                DB 0
HELP_STATE_COPY DB HELP_STATE_BYTES DUP (0)
HELP_OPEN       DB 0                    ; Help's window on screen
HELP_ACTIVE     DB 0                    ; ... and the active one
HELP_PENDING_CLICK DB 0                 ; the click that made it active
HELP_CLICK_CELL DW 0
HELP_KEY_STASH  DW 0                    ; a menu key pressed in Help
BARS_SHOWN      DB 1                    ; the editor's scroll bars drawn
VBAR_SHOWN      DB 1                    ; ... the vertical one too
VERIFY_RECT     DB 19,13,23,65,70H,70H
VERIFY_BUTTONS_TEXT DB '< Change >  < Skip >    < Cancel >   < Help >',0
LEFT_COLUMN     DW 0
MENU_ACCELERATOR_SELECTED DB 0
ALT_LETTERS     DB 'QWERTYUIOP',0,0,0,0,'ASDFGHJKL',0,0,0,0,0,'ZXCVBNM'
STEP_LEFT       DB 0
WORD_MARKS      DB '.!#$%&'
DRAW_COLUMN     DW 0
DRAW_ROW_DI     DW 0
DRAW_SEL_START  DW 0
DRAW_SEL_END    DW 0
DRAW_HAS_HIGHLIGHT DB 0
DRAWN_VALID     DB 0
DRAWN_HAD_HIGHLIGHT DB 0
DRAWN_THUMB_ROW DB 0
DRAWN_TOP_LINE  DW 0
DRAWN_LEFT_COLUMN DW 0
DRAG_LAST       DW 0FFFFH
BOOKMARKS       DW 0,0FFFFH,0,0,0FFFFH,0,0,0FFFFH,0,0,0FFFFH,0
; Ctrl+@ .. Ctrl+_ in the DOS 5 EDIT (WordStar) meanings, as observed in
; real DOS 5 EDIT. Ctrl+B, Ctrl+O, Ctrl+U and the rest do nothing.
CONTROL_KEY_TABLE DW EDIT_LOOP,EDIT_WORD_LEFT,EDIT_LOOP,EDIT_PAGE_DOWN
                DW EDIT_RIGHT,EDIT_UP,EDIT_WORD_RIGHT,EDIT_DELETE_UNSHIFTED
                DW EDIT_BACKSPACE,EDIT_TAB,EDIT_DOWN,EDIT_PREFIX_K
                DW EDIT_FIND_NEXT,EDIT_ENTER,EDIT_SPLIT_LINE,EDIT_LOOP
                DW EDIT_LITERAL,EDIT_PREFIX_Q,EDIT_PAGE_UP,EDIT_LEFT
                DW EDIT_DELETE_WORD,EDIT_LOOP,EDIT_INSERT_MODE,EDIT_SCROLL_UP
                DW EDIT_DOWN,EDIT_CUT_LINE,EDIT_SCROLL_DOWN,EDIT_ESCAPE
                DW EDIT_LOOP,EDIT_LOOP,EDIT_LOOP,EDIT_LOOP
CONFIRM_FOCUS   DB 0
; Bracket columns and label (cursor) columns of < Yes > <  No  > <Cancel>
; < Help > in the file confirmation frame, as DOS 5 places them.
CONFIRM_BRACKETS DB 20,26,30,37,41,48,52,59
CONFIRM_CURSORS DB 22,33,42,54
IO_BYTE         DB 0
SAVE_CRLF       DB 13,10
SAVE_REPEAT_BUFFER DB 16 DUP (0)
TAB_FILE        DB 0                    ; the loaded file had a tab
; The document's memory (double words low word first; see ARENA_ADDRESS).
ARENA_SEG       DW 0
ARENA_SIZE      DW 0,0
PREFIX_LEN      DW 0,0                  ; the text before the view
SUFFIX_LEN      DW 0,0                  ; the text after it
VIEW_LINE       DW 0,0                  ; line ends before the view
SUFFIX_LINES    DW 0,0                  ; line ends after it
CLIP_CAP        DW 0,0                  ; the clipboard's room, and its text
CLIP_LEN        DW 0,0
MOVE_SRC        DW 0,0
MOVE_DST        DW 0,0
MOVE_LEN        DW 0,0
COUNT_LEN       DW 0,0
G_CURSOR        DW 0,0                  ; positions while the view is out
G_TOP           DW 0,0
G_MARK          DW 0,0
G_HL_START      DW 0,0
G_HL_END        DW 0,0
G_FLAGS         DB 0                    ; 1: G_MARK, 2: G_HL_*
MARK_OUTSIDE    DB 0                    ; the anchor is MARK_G, BLOCK_MARK
MARK_CLAMP      DW 0                    ; then the view's end standing for it
MARK_G          DW 0,0
MEMORY_FULL     DB 0
FILL_MIN        DW 0
FILL_LINES      DW 0
FILL_BUDGET     DW 0
REBUILD_BEFORE  DW 0
REBUILD_AFTER   DW 0
REBUILD_BUDGET  DW 0
STREAM_POS      DW 0,0
STREAM_END      DW 0,0
STREAM_TMP      DW 0,0
STREAM_SEG      DW 0
STREAM_OFF      DW 0
STREAM_LEFT     DW 0
OUT_LENGTH      DW 0
SAVE_AT_END     DB 0
FIND_ORIGIN     DW 0,0
FIND_START      DW 0,0
SEL_A           DW 0,0
SEL_B           DW 0,0
INSERT_AT       DW 0,0
OUTDENT_COUNT   DW 0
LOAD_AT         DW 0,0
LOAD_LEFT       DW 0,0
LOAD_READ       DW 0,0

SCRATCH         DB 176 DUP (0)          ; INI_TRY_READ, STARTUP_ABORT

IF (($ - START) + 100H) GT 0E000H       ; room for DOSUI and the help topics
.ERR
ENDIF

DOSUI_TEXT ENDS
END START
