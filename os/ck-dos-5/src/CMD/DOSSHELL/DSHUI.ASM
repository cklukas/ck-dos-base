; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; DSHUI.ASM — Medium-model DOSUI bridge and DGROUP compactor for DOSSHELL.EXE.

.8086
_DATA   SEGMENT WORD PUBLIC 'DATA'
        EXTRN   __psp:WORD
        EXTRN   __heap_ptr:WORD
        EXTRN   __heap_limit:WORD
        EXTRN   __stack_bottom:WORD
_DATA   ENDS
DGROUP  GROUP   _DATA

DOSUI_TEXT SEGMENT PUBLIC 'CODE'
ASSUME CS:DOSUI_TEXT

INCLUDE DOSUI.INC

PUBLIC _dsh_ui_init
PUBLIC _dsh_ui_done
PUBLIC _dsh_ui_rows
PUBLIC _dsh_ui_begin_frame
PUBLIC _dsh_ui_end_frame
PUBLIC _dsh_ui_suspend_bb
PUBLIC _dsh_ui_resume_bb
PUBLIC _dsh_ui_fill
PUBLIC _dsh_ui_write
PUBLIC _dsh_ui_put_cell
PUBLIC _dsh_ui_shadow_rect
PUBLIC _dsh_ui_cursor
PUBLIC _dsh_ui_event
PUBLIC _dsh_ui_poll_key
PUBLIC _dsh_compact_dgroup

; void far dsh_ui_init(unsigned save_seg, unsigned bw_mode, unsigned rows_req)
_dsh_ui_init PROC FAR
    PUSH BP
    MOV  BP, SP
    PUSH BX
    PUSH CX
    PUSH DX
    PUSH SI
    PUSH DI
    PUSH ES

    MOV  AX, [BP+6]
    MOV  CS:DOSUI_SAVE_SEGMENT, AX
    MOV  BYTE PTR CS:DOSUI_HAS_BACKBUFFER, 0
    OR   AX, AX
    JZ   SHORT DUI_NO_BB
    MOV  BYTE PTR CS:DOSUI_HAS_BACKBUFFER, 1
DUI_NO_BB:
    MOV  BYTE PTR CS:DOSUI_ROWS, 25
    MOV  BYTE PTR CS:DOSUI_ROW_OFFSET, 0
    CALL DOSUI_INIT

    ; Check if high-resolution text rows requested (rows_req > 25)
    MOV  AX, [BP+10]
    CMP  AX, 25
    JBE  SHORT DUI_ROWS_DONE
    MOV  AX, 1112h
    XOR  BL, BL
    INT  10h
    MOV  AX, 0040h
    MOV  ES, AX
    MOV  AL, ES:[0084h]
    OR   AL, AL
    JZ   SHORT DUI_ROWS_DONE
    INC  AL
    CMP  AL, 50
    JBE  SHORT DUI_STORE_ROWS
    MOV  AL, 50
DUI_STORE_ROWS:
    MOV  CS:DOSUI_ROWS, AL
    ; Clear full 80xN screen
    XOR  AH, AH
    MOV  CX, 80
    MUL  CX
    MOV  CX, AX
    MOV  AX, 0B800h
    MOV  ES, AX
    XOR  DI, DI
    MOV  AX, 0720h
    CLD
    REP  STOSW
DUI_ROWS_DONE:

    ; Configure high-intensity background (BL=00h) in color mode for 0xF0
    ; bright-white surfaces, or normal blink (BL=01h) in /B monochrome mode.
    MOV  AX, 1003h
    MOV  BL, 00h
    CMP  WORD PTR [BP+8], 0
    JE   SHORT DUI_SET_BLINK
    MOV  BL, 01h
DUI_SET_BLINK:
    INT  10h

    MOV  CX, 0607h
    CALL DOSUI_SET_CURSOR_SHAPE
    CALL DOSUI_HIDE_CURSOR
    MOV  BYTE PTR CS:DOSUI_ALT_TAP, 1
    CALL DOSUI_MOUSE_INIT

    POP  ES
    POP  DI
    POP  SI
    POP  DX
    POP  CX
    POP  BX
    POP  BP
    RET
_dsh_ui_init ENDP

; void far dsh_ui_done(void)
_dsh_ui_done PROC FAR
    PUSH SI
    PUSH DI
    CALL DOSUI_MOUSE_SHUTDOWN
    CALL DOSUI_SHUTDOWN
    MOV  WORD PTR CS:DOSUI_SAVE_SEGMENT, 0
    MOV  BYTE PTR CS:DOSUI_ROWS, 25
    POP  DI
    POP  SI
    RET
_dsh_ui_done ENDP

; unsigned far dsh_ui_rows(void)
_dsh_ui_rows PROC FAR
    XOR  AX, AX
    MOV  AL, CS:DOSUI_ROWS
    RET
_dsh_ui_rows ENDP

_dsh_ui_begin_frame PROC FAR
    CALL DOSUI_BEGIN_FRAME
    RET
_dsh_ui_begin_frame ENDP

_dsh_ui_end_frame PROC FAR
    CALL DOSUI_END_FRAME
    RET
_dsh_ui_end_frame ENDP

_dsh_ui_suspend_bb PROC FAR
    CALL DOSUI_END_FRAME
    MOV  WORD PTR CS:DOSUI_FRAME_SEGMENT, 0
    MOV  BYTE PTR CS:DOSUI_HAS_BACKBUFFER, 0
    RET
_dsh_ui_suspend_bb ENDP

; void far dsh_ui_resume_bb(unsigned save_seg)
_dsh_ui_resume_bb PROC FAR
    PUSH BP
    MOV  BP, SP
    MOV  AX, [BP+6]
    OR   AX, AX
    JZ   SHORT DUR_DONE
    ADD  AX, DOSUI_TEXT_SAVE_PARAGRAPHS
    MOV  CS:DOSUI_FRAME_SEGMENT, AX
    MOV  BYTE PTR CS:DOSUI_HAS_BACKBUFFER, 1
DUR_DONE:
    POP  BP
    RET
_dsh_ui_resume_bb ENDP

; void far dsh_ui_fill(unsigned row, unsigned col, unsigned ch, unsigned attr, unsigned count)
_dsh_ui_fill PROC FAR
    PUSH BP
    MOV  BP, SP
    PUSH BX
    PUSH CX
    PUSH DX
    PUSH SI
    PUSH DI
    MOV  DH, BYTE PTR [BP+6]
    MOV  DL, BYTE PTR [BP+8]
    MOV  AL, BYTE PTR [BP+10]
    MOV  BL, BYTE PTR [BP+12]
    MOV  CX, [BP+14]
    CALL DOSUI_FILL_ROW
    POP  DI
    POP  SI
    POP  DX
    POP  CX
    POP  BX
    POP  BP
    RET
_dsh_ui_fill ENDP

; void far dsh_ui_write(unsigned row, unsigned col, unsigned attr, const char *str)
_dsh_ui_write PROC FAR
    PUSH BP
    MOV  BP, SP
    PUSH BX
    PUSH DX
    PUSH SI
    PUSH DI
    MOV  DH, BYTE PTR [BP+6]
    MOV  DL, BYTE PTR [BP+8]
    MOV  BL, BYTE PTR [BP+10]
    MOV  SI, [BP+12]
    CALL DOSUI_WRITE_Z
    POP  DI
    POP  SI
    POP  DX
    POP  BX
    POP  BP
    RET
_dsh_ui_write ENDP

; void far dsh_ui_put_cell(unsigned row, unsigned col, unsigned ch, unsigned attr)
_dsh_ui_put_cell PROC FAR
    PUSH BP
    MOV  BP, SP
    PUSH BX
    PUSH CX
    PUSH DX
    PUSH SI
    PUSH DI
    MOV  DH, BYTE PTR [BP+6]
    MOV  DL, BYTE PTR [BP+8]
    MOV  AL, BYTE PTR [BP+10]
    MOV  BL, BYTE PTR [BP+12]
    MOV  CX, 1
    CALL DOSUI_FILL_ROW
    POP  DI
    POP  SI
    POP  DX
    POP  CX
    POP  BX
    POP  BP
    RET
_dsh_ui_put_cell ENDP

; void far dsh_ui_shadow_rect(unsigned top, unsigned left, unsigned bottom, unsigned right, unsigned attr)
_dsh_ui_shadow_rect PROC FAR
    PUSH BP
    MOV  BP, SP
    PUSH AX
    PUSH BX
    PUSH CX
    PUSH DX
    PUSH DI
    PUSH ES

    MOV  ES, CS:DOSUI_DRAW_SEGMENT
    MOV  DH, BYTE PTR [BP+6]        ; cur row
    MOV  BH, BYTE PTR [BP+10]       ; bottom row
    MOV  BL, BYTE PTR [BP+14]       ; shadow attr
DSR_ROW:
    CMP  DH, CS:DOSUI_ROWS
    JAE  SHORT DSR_DONE
    CMP  DH, BH
    JA   SHORT DSR_DONE
    MOV  DL, BYTE PTR [BP+8]        ; left col
DSR_COL:
    CMP  DL, 80
    JAE  SHORT DSR_NEXT_ROW
    CMP  DL, BYTE PTR [BP+12]
    JA   SHORT DSR_NEXT_ROW
    XOR  AX, AX
    MOV  AL, DH
    MOV  CX, 160
    PUSH DX
    MUL  CX
    POP  DX
    XOR  CH, CH
    MOV  CL, DL
    SHL  CX, 1
    ADD  AX, CX
    MOV  DI, AX
    MOV  ES:[DI+1], BL
    INC  DL
    JMP  SHORT DSR_COL
DSR_NEXT_ROW:
    INC  DH
    JMP  SHORT DSR_ROW
DSR_DONE:
    POP  ES
    POP  DI
    POP  DX
    POP  CX
    POP  BX
    POP  AX
    POP  BP
    RET
_dsh_ui_shadow_rect ENDP

; void far dsh_ui_cursor(unsigned row_or_ffff, unsigned col)
_dsh_ui_cursor PROC FAR
    PUSH BP
    MOV  BP, SP
    PUSH DX
    MOV  AX, [BP+6]
    CMP  AX, 0FFFFh
    JE   SHORT DUC_HIDE
    MOV  DH, AL
    MOV  DL, BYTE PTR [BP+8]
    CALL DOSUI_SET_CURSOR
    CALL DOSUI_SHOW_CURSOR
    POP  DX
    POP  BP
    RET
DUC_HIDE:
    CALL DOSUI_HIDE_CURSOR
    POP  DX
    POP  BP
    RET
_dsh_ui_cursor ENDP

; unsigned long far dsh_ui_event(void)
; Returns:
;   Mouse click: DX = 1, AX = (row << 8) | col
;   Key event:   DX = (shift_flags << 8), AX = (scan << 8) | ascii
_dsh_ui_event PROC FAR
    PUSH SI
    PUSH DI
    CALL DOSUI_READ_EVENT
    JC   SHORT DUE_MOUSE
    PUSH AX
    MOV  AH, 02h
    INT  16h
    MOV  DH, AL
    XOR  DL, DL
    POP  AX
    POP  DI
    POP  SI
    RET
DUE_MOUSE:
    MOV  AX, DX
    MOV  DX, 1
    POP  DI
    POP  SI
    RET
_dsh_ui_event ENDP

; unsigned far dsh_ui_poll_key(void) -> FFFFh if none, else AX key
_dsh_ui_poll_key PROC FAR
    MOV  AH, 01h
    INT  16h
    JZ   SHORT DUPK_NONE
    XOR  AH, AH
    INT  16h
    RET
DUPK_NONE:
    MOV  AX, 0FFFFh
    RET
_dsh_ui_poll_key ENDP

ASSUME DS:DGROUP
_dsh_compact_dgroup PROC FAR
    MOV  BX, SP
    MOV  AX, SS:[BX+4]
    MOV  DX, SS:[BX+6]
    ADD  AX, __heap_ptr
    JC   SHORT DCD_DONE
    INC  AX
    AND  AX, 0FFFEh
    ADD  DX, AX
    JC   SHORT DCD_DONE
    AND  DX, 0FFFEh
    CMP  DX, 0FFD0h
    JAE  SHORT DCD_DONE
    MOV  __heap_limit, AX
    MOV  __stack_bottom, AX
    PUSH SI
    PUSH DI
    PUSH ES
    PUSH DS
    POP  ES
    MOV  CX, 0FFF0h
    SUB  CX, SP
    MOV  AX, 0FFF0h
    SUB  AX, DX
    MOV  SI, SP
    MOV  DI, SP
    SUB  DI, AX
    SHR  CX, 1
    CLD
    REP  MOVSW
    SUB  SP, AX
    SUB  BP, AX
    MOV  ES, __psp
    MOV  AX, DX
    ADD  AX, 15
    MOV  CL, 4
    SHR  AX, CL
    MOV  BX, DS
    ADD  AX, BX
    SUB  AX, __psp
    MOV  BX, AX
    MOV  AH, 4Ah
    INT  21h
    POP  ES
    POP  DI
    POP  SI
DCD_DONE:
    RET
_dsh_compact_dgroup ENDP

DOSUI_TEXT ENDS
END
