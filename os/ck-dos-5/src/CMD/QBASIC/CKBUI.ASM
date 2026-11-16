; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT

.8086
DOSUI_TEXT SEGMENT PUBLIC 'CODE'
ASSUME CS:DOSUI_TEXT

INCLUDE DOSUI.INC

PUBLIC CKBASIC_DOSUI_SMOKE

; Watcom DOS bridge: far call, AX = caller-owned save segment,
; DS:SI = C string in DGROUP. DOSUI state lives in DOSUI_TEXT (CS), and
; DOSUI_WRITE_Z reads the string from DS; no segment switch is necessary.
; Returns AX = 1 if DOSUI wrote the expected attributed first cell.
CKBASIC_DOSUI_SMOKE PROC FAR
    PUSH BX
    PUSH CX
    PUSH DX
    PUSH SI
    PUSH DI
    PUSH DS
    PUSH ES

    MOV CS:DOSUI_SAVE_SEGMENT,AX
    CALL DOSUI_INIT

    MOV BL,1Eh
    XOR DX,DX
    CALL DOSUI_WRITE_Z
    MOV AX,0B800h
    MOV ES,AX
    MOV AL,[SI]
    MOV AH,BL
    XOR CX,CX
    CMP ES:[0],AX
    JNE SHORT CKBASIC_DOSUI_RESTORE
    INC CX

CKBASIC_DOSUI_RESTORE:
    CALL DOSUI_SHUTDOWN
    MOV WORD PTR CS:DOSUI_SAVE_SEGMENT,0
    MOV AX,CX

    POP ES
    POP DS
    POP DI
    POP SI
    POP DX
    POP CX
    POP BX
    RET
CKBASIC_DOSUI_SMOKE ENDP

; IDE bridge. Watcom's medium model uses far code and a near DGROUP.
; DOSUI positioning/text primitives preserve their arguments.
PUBLIC CKBIDE_INIT, CKBIDE_DONE, CKBIDE_FILL, CKBIDE_WRITE
PUBLIC CKBIDE_MENU, CKBIDE_CURSOR, CKBIDE_EVENT

CKBIDE_INIT PROC FAR
    MOV CS:DOSUI_SAVE_SEGMENT,AX
    CALL DOSUI_INIT
    PUSH CX
    MOV CX,0607h
    CALL DOSUI_SET_CURSOR_SHAPE
    POP CX
    MOV BYTE PTR CS:DOSUI_ALT_TAP,1
    CALL DOSUI_MOUSE_INIT
    RET
CKBIDE_INIT ENDP

CKBIDE_DONE PROC FAR
    CALL DOSUI_MOUSE_SHUTDOWN
    CALL DOSUI_SHUTDOWN
    MOV WORD PTR CS:DOSUI_SAVE_SEGMENT,0
    RET
CKBIDE_DONE ENDP

CKBIDE_FILL PROC FAR
    CALL DOSUI_FILL_ROW
    RET
CKBIDE_FILL ENDP

CKBIDE_WRITE PROC FAR
    CALL DOSUI_WRITE_Z
    RET
CKBIDE_WRITE ENDP

CKBIDE_MENU PROC FAR
    PUSH SI
    CALL DOSUI_MENU_BOX
    POP SI
    RET
CKBIDE_MENU ENDP

; DX = packed row/column or FFFFh to hide.
CKBIDE_CURSOR PROC FAR
    CMP DX,0FFFFh
    JNE SHORT CKBIDE_CURSOR_VISIBLE
    CALL DOSUI_HIDE_CURSOR
    RET
CKBIDE_CURSOR_VISIBLE:
    CALL DOSUI_SET_CURSOR
    CALL DOSUI_SHOW_CURSOR
    RET
CKBIDE_CURSOR ENDP

; DX:AX = (BIOS shift state in DH):key or 1:packed mouse coordinate.
CKBIDE_EVENT PROC FAR
    CALL DOSUI_READ_EVENT
    JC SHORT CKBIDE_EVENT_MOUSE
    PUSH AX
    MOV AH,02h
    INT 16h
    MOV DH,AL
    XOR DL,DL
    POP AX
    RET
CKBIDE_EVENT_MOUSE:
    MOV AX,DX
    MOV DX,1
    RET
CKBIDE_EVENT ENDP

; BIOS sets CF on entry to the INT 15h keyboard intercept. Decline the
; original Ctrl+Scroll Lock make so older keyboard maps do not toggle it.
; An enhanced Break sequence still reaches the BIOS INT 1Bh path.
PUBLIC CKB_INPUT_SCAN, CKB_INPUT_SCAN_INIT, CKB_INPUT_SCAN_PENDING

CKB_INPUT_OLD_15 DD 0
CKB_INPUT_BREAK DB 0
CKB_INPUT_E0 DB 0

CKB_INPUT_SCAN_INIT PROC FAR
    MOV WORD PTR CS:CKB_INPUT_OLD_15,AX
    MOV WORD PTR CS:CKB_INPUT_OLD_15+2,DX
    MOV BYTE PTR CS:CKB_INPUT_BREAK,0
    MOV BYTE PTR CS:CKB_INPUT_E0,0
    RET
CKB_INPUT_SCAN_INIT ENDP

CKB_INPUT_SCAN_PENDING PROC FAR
    XOR AX,AX
    MOV AL,CS:CKB_INPUT_BREAK
    RET
CKB_INPUT_SCAN_PENDING ENDP

_input_scan LABEL FAR
CKB_INPUT_SCAN PROC FAR
    PUSHF
    CMP AH,4Fh
    JNE SHORT CKB_INPUT_SCAN_CHAIN
    CMP AL,0E0h
    JE SHORT CKB_INPUT_SCAN_PREFIX
    CMP BYTE PTR CS:CKB_INPUT_E0,0
    JNE SHORT CKB_INPUT_SCAN_EXTENDED
    CMP AL,46h
    JNE SHORT CKB_INPUT_SCAN_CHAIN
    PUSH BX
    PUSH ES
    MOV BX,40h
    MOV ES,BX
    TEST BYTE PTR ES:[17h],4
    POP ES
    POP BX
    JZ SHORT CKB_INPUT_SCAN_CHAIN
    MOV BYTE PTR CS:CKB_INPUT_BREAK,1
    POPF
    PUSH BP
    MOV BP,SP
    AND WORD PTR [BP+6],0FFFEh
    POP BP
    IRET
CKB_INPUT_SCAN_PREFIX:
    MOV BYTE PTR CS:CKB_INPUT_E0,1
    JMP SHORT CKB_INPUT_SCAN_CHAIN
CKB_INPUT_SCAN_EXTENDED:
    MOV BYTE PTR CS:CKB_INPUT_E0,0
CKB_INPUT_SCAN_CHAIN:
    POPF
    JMP DWORD PTR CS:CKB_INPUT_OLD_15
CKB_INPUT_SCAN ENDP

; ---------------------------------------------------------------------------
; Standard 16-bit Medium-Model (cdecl FAR) entry points for ckcl / MSC
; ---------------------------------------------------------------------------
PUBLIC _dosui_smoke
PUBLIC _ui_init, _ui_done, _ui_fill, _ui_write, _ui_menu, _ui_cursor, _ui_event
PUBLIC _input_scan, _input_scan_init, _input_scan_pending, _input_key, _input_idle

_dosui_smoke PROC FAR
    PUSH BP
    MOV BP,SP
    PUSH SI
    MOV AX,[BP+6]
    MOV SI,[BP+8]
    CALL FAR PTR CKBASIC_DOSUI_SMOKE
    POP SI
    POP BP
    RET
_dosui_smoke ENDP

_ui_init PROC FAR
    PUSH BP
    MOV BP,SP
    PUSH SI
    PUSH DI
    MOV AX,[BP+6]
    CALL FAR PTR CKBIDE_INIT
    POP DI
    POP SI
    POP BP
    RET
_ui_init ENDP

_ui_done PROC FAR
    PUSH SI
    PUSH DI
    CALL FAR PTR CKBIDE_DONE
    POP DI
    POP SI
    RET
_ui_done ENDP

_ui_fill PROC FAR
    PUSH BP
    MOV BP,SP
    PUSH SI
    PUSH DI
    MOV DX,[BP+6]
    MOV AX,[BP+8]
    MOV BX,[BP+10]
    MOV CX,[BP+12]
    CALL FAR PTR CKBIDE_FILL
    POP DI
    POP SI
    POP BP
    RET
_ui_fill ENDP

_ui_write PROC FAR
    PUSH BP
    MOV BP,SP
    PUSH SI
    PUSH DI
    MOV DX,[BP+6]
    MOV BX,[BP+8]
    MOV SI,[BP+10]
    CALL FAR PTR CKBIDE_WRITE
    POP DI
    POP SI
    POP BP
    RET
_ui_write ENDP

_ui_menu PROC FAR
    PUSH BP
    MOV BP,SP
    PUSH SI
    PUSH DI
    MOV SI,[BP+6]
    CALL FAR PTR CKBIDE_MENU
    POP DI
    POP SI
    POP BP
    RET
_ui_menu ENDP

_ui_cursor PROC FAR
    PUSH BP
    MOV BP,SP
    PUSH SI
    PUSH DI
    MOV DX,[BP+6]
    CALL FAR PTR CKBIDE_CURSOR
    POP DI
    POP SI
    POP BP
    RET
_ui_cursor ENDP

_ui_event PROC FAR
    PUSH SI
    PUSH DI
    CALL FAR PTR CKBIDE_EVENT
    POP DI
    POP SI
    RET
_ui_event ENDP

_input_scan_init PROC FAR
    PUSH BP
    MOV BP,SP
    MOV AX,[BP+6]
    MOV DX,[BP+8]
    CALL FAR PTR CKB_INPUT_SCAN_INIT
    POP BP
    RET
_input_scan_init ENDP

_input_scan_pending PROC FAR
    JMP SHORT CKB_INPUT_SCAN_PENDING
_input_scan_pending ENDP

_input_key PROC FAR
    MOV AH,01h
    INT 16h
    JZ SHORT _input_key_empty
    XOR AH,AH
    INT 16h
    RET
_input_key_empty:
    MOV AX,0FFFFh
    RET
_input_key ENDP

_input_idle PROC FAR
    STI
    HLT
    RET
_input_idle ENDP

DOSUI_TEXT ENDS
END
