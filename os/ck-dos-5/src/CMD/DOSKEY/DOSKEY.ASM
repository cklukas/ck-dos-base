; Copyright (c) 2026 C. Klukas
; SPDX-License-Identifier: MIT
;
; DOSKEY.COM — Clean-room CK-DOS 5.0 Command-Line Editor, History, and Macro TSR.
;
; Implements:
;   - Dual-mode host multiplex fast path (INT 2Fh AX=0D700h / AX=0D701h, BX=1)
;   - INT 2Fh AX=4800h (installation check -> AL=0FFh, BX=0500h, ES=resident seg)
;   - INT 2Fh AX=4810h (read line with history recall and macro expansion)
;   - INT 21h AH=0Ah   (buffered console input line editor + history + macros)
;   - Switches: /?, /REINSTALL, /BUFSIZE=n, /MACROS (/M), /HISTORY (/H),
;               /INSERT, /OVERSTRIKE, and macro definitions NAME=[TEXT]

.8086

CODE    SEGMENT PARA PUBLIC 'CODE'
        ASSUME  CS:CODE, DS:NOTHING, ES:NOTHING, SS:NOTHING
        ORG     100h

START:
        ; -------------------------------------------------------------------
        ; Mandatory Host Multiplex Fast Path (INT 2Fh AX=0D700h / 0D701h)
        ; -------------------------------------------------------------------
        MOV     AX,0D700h
        MOV     BX,434Bh                ; 'CK'
        XOR     CX,CX
        INT     2Fh
        CMP     AX,0D7FFh
        JNE     STANDALONE_ENTRY
        CMP     CX,434Bh
        JNE     STANDALONE_ENTRY

        MOV     AX,0D701h
        MOV     BX,0001h                ; tool 1 = DOSKEY
        XOR     CH,CH
        MOV     CL,DS:[0080h]
        MOV     DX,0081h
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET HOST_RESP_BUF
        MOV     SI,0F000h
        CMP     SP,0F400h
        JAE     HOST_CALL_GO
        MOV     SI,SP
        SUB     SI,DI
        SUB     SI,0100h
HOST_CALL_GO:
        INT     2Fh
        JC      STANDALONE_ENTRY
        PUSH    DX                      ; DL = exit code
        JCXZ    HOST_NO_OUT
        PUSH    ES
        POP     DS
        MOV     DX,DI
        MOV     BX,0001h                ; stdout
        MOV     AH,40h
        INT     21h
HOST_NO_OUT:
        POP     DX
        MOV     AL,DL
        MOV     AH,4Ch
        INT     21h

STANDALONE_ENTRY:
        JMP     INIT_MAIN

; ===========================================================================
; RESIDENT TSR CODE & STATE (kept compact for UMB/low-RAM residency)
; ===========================================================================

RES_SIG         DW      4B44h           ; 'DK' signature at resident offset
OLD_INT21       DD      0
OLD_INT2F       DD      0

DEF_INSERT      DB      1               ; 1 = insert, 0 = overstrike
CUR_INSERT      DB      1
IN_EDIT_BUSY    DB      0               ; re-entrancy guard

HIST_CAP        DW      512             ; history buffer capacity (bytes)
HIST_USED       DW      0               ; bytes currently stored in HIST_BUF
HIST_IDX        DW      0FFFFh          ; current recall entry byte offset (FFFF=end)

MAC_CAP         DW      512             ; macro buffer capacity (bytes)
MAC_USED        DW      0               ; bytes currently stored in MAC_BUF

HIST_OFS        DW      OFFSET RES_POOL + 512 ; offset of history buffer in CS
MAC_OFS         DW      OFFSET RES_POOL       ; offset of macro buffer in CS

PEND_LEN        DB      0               ; queued $T continuation command length
PEND_BUF        DB      130 DUP (0)

WORK_LINE       DB      132 DUP (0)     ; active edit buffer
EXP_LINE        DB      132 DUP (0)     ; macro expansion scratch buffer
ARG_COPY        DB      132 DUP (0)     ; macro argument tail scratch

; ---------------------------------------------------------------------------
; INT 2Fh Handler (AX=4800h installation check, AX=4810h read line)
; ---------------------------------------------------------------------------
NEW_INT2F       PROC    FAR
        CMP     AX,4800h
        JE      I2F_CHECK
        CMP     AX,4810h
        JE      I2F_READLINE
        JMP     DWORD PTR CS:[OLD_INT2F]

I2F_CHECK:
        MOV     AL,0FFh
        MOV     BX,0500h
        PUSH    CS
        POP     ES
        IRET

I2F_READLINE:
        STI
        CALL    DO_BUFFERED_EDIT
        XOR     AX,AX
        IRET
NEW_INT2F       ENDP

; ---------------------------------------------------------------------------
; INT 21h Handler (intercepts AH=0Ah when STDIN is console char device)
; ---------------------------------------------------------------------------
NEW_INT21       PROC    FAR
        CMP     AH,0Ah
        JE      I21_CHK_0A
I21_CHAIN:
        JMP     DWORD PTR CS:[OLD_INT21]

I21_CHK_0A:
        CMP     BYTE PTR CS:[IN_EDIT_BUSY],0
        JNE     I21_CHAIN

        ; Verify handle 0 is console device (IOCTL AX=4400h, BX=0 -> DX bit 7 & bit 0)
        PUSH    AX
        PUSH    BX
        PUSH    DX
        MOV     AX,4400h
        XOR     BX,BX
        PUSHF
        CALL    DWORD PTR CS:[OLD_INT21]
        JC      I21_NOT_CON
        AND     DX,0081h
        CMP     DX,0081h
        JNE     I21_NOT_CON
        POP     DX
        POP     BX
        POP     AX
        STI
        CALL    DO_BUFFERED_EDIT
        IRET

I21_NOT_CON:
        POP     DX
        POP     BX
        POP     AX
        JMP     DWORD PTR CS:[OLD_INT21]
NEW_INT21       ENDP

; ---------------------------------------------------------------------------
; DO_BUFFERED_EDIT: DS:DX -> DOS 0Ah buffer [max_len, actual_len, data...]
; ---------------------------------------------------------------------------
DO_BUFFERED_EDIT PROC   NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    BP
        PUSH    DS
        PUSH    ES

        MOV     BYTE PTR CS:[IN_EDIT_BUSY],1
        CLD

        ; Save caller buffer pointer DS:DX in ES_USER / BP
        MOV     BP,DX                   ; caller DS:BP -> [max_len, len, buf...]
        MOV     AL,DS:[BP]              ; max capacity (includes trailing CR)
        CMP     AL,2
        JAE     DBE_CAP_OK
        JMP     DBE_DONE_RAW
DBE_CAP_OK:
        DEC     AL                      ; max characters excluding CR
        XOR     AH,AH
        MOV     DI,AX                   ; DI = max_chars (1..254)
        CMP     DI,126
        JBE     DBE_CAP_CLAMP
        MOV     DI,126
DBE_CAP_CLAMP:

        ; If a queued $T continuation command is pending, deliver it immediately!
        CMP     BYTE PTR CS:[PEND_LEN],0
        JE      DBE_INTERACTIVE
        PUSH    DS
        POP     ES                      ; ES:BP -> caller buffer
        PUSH    CS
        POP     DS
        ASSUME  DS:CODE
        MOV     SI,OFFSET PEND_BUF
        CALL    DEQUEUE_AND_RETURN
        ASSUME  DS:NOTHING
        JMP     DBE_EXIT

DBE_INTERACTIVE:
        PUSH    DS
        POP     ES                      ; ES:BP -> caller buffer
        PUSH    CS
        POP     DS
        ASSUME  DS:CODE

        MOV     AL,[DEF_INSERT]
        MOV     [CUR_INSERT],AL
        MOV     AX,[HIST_USED]
        MOV     [HIST_IDX],AX           ; start recall pointer at end of history

        XOR     CX,CX                   ; CL = current line len, CH = cursor pos
        MOV     DX,DI                   ; DL = max_chars

ED_KEY_LOOP:
        XOR     AH,AH
        INT     16h                     ; AL = ASCII, AH = scan code
        OR      AL,AL
        JZ      ED_GO_EXT
        CMP     AL,0E0h
        JNE     ED_NOT_EXT
ED_GO_EXT:
        JMP     ED_EXT_KEY
ED_NOT_EXT:

        CMP     AL,0Dh                  ; Enter
        JNE     ED_CHK_ESC
        JMP     ED_FINISH_LINE

ED_CHK_ESC:
        CMP     AL,1Bh                  ; Esc: clear line
        JNE     ED_CHK_BS
        CALL    CLEAR_SCREEN_LINE
        JMP     ED_KEY_LOOP

ED_CHK_BS:
        CMP     AL,08h                  ; Backspace
        JNE     ED_CHK_CTRLC
        OR      CH,CH
        JZ      ED_KEY_LOOP
        ; Move cursor left 1 and delete at cursor
        DEC     CH
        MOV     AL,08h
        CALL    CON_PUTC
        CALL    DELETE_AT_CURSOR
        JMP     ED_KEY_LOOP

ED_CHK_CTRLC:
        CMP     AL,03h                  ; Ctrl+C
        JNE     ED_PRINTABLE
        MOV     AL,'^'
        CALL    CON_PUTC
        MOV     AL,'C'
        CALL    CON_PUTC
        MOV     AL,0Dh
        CALL    CON_PUTC
        MOV     AL,0Ah
        CALL    CON_PUTC
        XOR     CX,CX
        INT     23h
        JMP     ED_FINISH_EMPTY

ED_PRINTABLE:
        CMP     AL,20h
        JB      ED_KEY_LOOP
        CMP      BYTE PTR [CUR_INSERT],0
        JE      ED_OVERSTRIKE

        ; Insert mode
        CMP     CL,DL
        JAE     ED_BEEP
        ; Shift WORK_LINE[CH..CL-1] right by 1
        PUSH    AX
        MOV     BL,CL
        XOR     BH,BH
        MOV     SI,BX                   ; SI = len
        MOV     BL,CH
        CMP     SI,BX
        JBE     ED_INS_STORE
ED_INS_SHIFT:
        MOV     AL,[WORK_LINE+SI-1]
        MOV     [WORK_LINE+SI],AL
        DEC     SI
        CMP     SI,BX
        JA      ED_INS_SHIFT
ED_INS_STORE:
        POP     AX
        MOV     BL,CH
        XOR     BH,BH
        MOV     [WORK_LINE+BX],AL
        INC     CL
        INC     CH
        CALL    CON_PUTC
        ; Redraw shifted tail WORK_LINE[CH..CL-1] and backspace cursor back to CH
        CALL    REDRAW_TAIL_FROM_CH
        JMP     ED_KEY_LOOP

ED_OVERSTRIKE:
        CMP     CH,CL
        JB      ED_OV_WRITE
        CMP     CL,DL
        JAE     ED_BEEP
        INC     CL
ED_OV_WRITE:
        MOV     BL,CH
        XOR     BH,BH
        MOV     [WORK_LINE+BX],AL
        INC     CH
        CALL    CON_PUTC
        JMP     ED_KEY_LOOP

ED_BEEP:
        MOV     AL,07h
        CALL    CON_PUTC
        JMP     ED_KEY_LOOP

; --- Extended Scan Codes (AL == 00h or E0h, AH == scan) ---
ED_EXT_KEY:
        CMP     AH,4Bh                  ; Left
        JE      EK_LEFT
        CMP     AH,4Dh                  ; Right
        JE      EK_RIGHT
        CMP     AH,47h                  ; Home
        JNE     EK_CHK_END
        JMP     EK_HOME
EK_CHK_END:
        CMP     AH,4Fh                  ; End
        JNE     EK_CHK_INS
        JMP     EK_END
EK_CHK_INS:
        CMP     AH,52h                  ; Insert
        JNE     EK_CHK_DEL
        JMP     EK_INS
EK_CHK_DEL:
        CMP     AH,53h                  ; Delete
        JNE     EK_CHK_UP
        JMP     EK_DEL
EK_CHK_UP:
        CMP     AH,48h                  ; Up
        JNE     EK_CHK_DN
        JMP     EK_UP
EK_CHK_DN:
        CMP     AH,50h                  ; Down
        JNE     EK_CHK_PGUP
        JMP     EK_DOWN
EK_CHK_PGUP:
        CMP     AH,49h                  ; PgUp
        JNE     EK_CHK_PGDN
        JMP     EK_PGUP
EK_CHK_PGDN:
        CMP     AH,51h                  ; PgDn
        JNE     EK_CHK_F7
        JMP     EK_PGDN
EK_CHK_F7:
        CMP     AH,41h                  ; F7 (list numbered history)
        JNE     EK_CHK_F8
        JMP     EK_F7
EK_CHK_F8:
        CMP     AH,42h                  ; F8 (prefix search)
        JNE     EK_CHK_AF7
        JMP     EK_F8
EK_CHK_AF7:
        CMP     AH,6Eh                  ; Alt+F7 (clear history)
        JNE     EK_IGNORE
        MOV     WORD PTR [HIST_USED],0
        MOV     WORD PTR [HIST_IDX],0
EK_IGNORE:
        JMP     ED_KEY_LOOP

EK_LEFT:
        OR      CH,CH
        JZ      EK_IGNORE
        DEC     CH
        MOV     AL,08h
        CALL    CON_PUTC
        JMP     ED_KEY_LOOP

EK_RIGHT:
        CMP     CH,CL
        JAE     EK_IGNORE
        MOV     BL,CH
        XOR     BH,BH
        MOV     AL,[WORK_LINE+BX]
        INC     CH
        CALL    CON_PUTC
        JMP     ED_KEY_LOOP

EK_HOME:
        OR      CH,CH
        JZ      EK_IGNORE
EK_HOME_LP:
        DEC     CH
        MOV     AL,08h
        CALL    CON_PUTC
        OR      CH,CH
        JNZ     EK_HOME_LP
        JMP     ED_KEY_LOOP

EK_END:
        CMP     CH,CL
        JAE     EK_IGNORE
EK_END_LP:
        MOV     BL,CH
        XOR     BH,BH
        MOV     AL,[WORK_LINE+BX]
        INC     CH
        CALL    CON_PUTC
        CMP     CH,CL
        JB      EK_END_LP
        JMP     ED_KEY_LOOP

EK_INS:
        XOR     BYTE PTR [CUR_INSERT],1
        JMP     ED_KEY_LOOP

EK_DEL:
        CMP     CH,CL
        JAE     EK_IGNORE
        CALL    DELETE_AT_CURSOR
        JMP     ED_KEY_LOOP

EK_UP:
        CMP     WORD PTR [HIST_USED],0
        JE      EK_IGNORE
        MOV     SI,[HIST_IDX]
        OR      SI,SI
        JZ      EK_LOAD_HIST            ; already at oldest entry
        CALL    PREV_HIST_ENTRY         ; SI -> previous ASCIIZ entry
        MOV     [HIST_IDX],SI
        JMP     EK_LOAD_HIST

EK_DOWN:
        MOV     SI,[HIST_IDX]
        CMP     SI,[HIST_USED]
        JAE     EK_CLR_HIST
        CALL    NEXT_HIST_ENTRY
        MOV     [HIST_IDX],SI
        CMP     SI,[HIST_USED]
        JAE     EK_CLR_HIST
        JMP     EK_LOAD_HIST

EK_CLR_HIST:
        CALL    CLEAR_SCREEN_LINE
        JMP     ED_KEY_LOOP

EK_PGUP:
        CMP     WORD PTR [HIST_USED],0
        JE      EK_IGNORE_BR
        XOR     SI,SI
        MOV     [HIST_IDX],SI
        JMP     EK_LOAD_HIST
EK_IGNORE_BR:
        JMP     ED_KEY_LOOP

EK_PGDN:
        CMP     WORD PTR [HIST_USED],0
        JE      EK_IGNORE_BR
        MOV     SI,[HIST_USED]
        CALL    PREV_HIST_ENTRY
        MOV     [HIST_IDX],SI

EK_LOAD_HIST:
        CALL    CLEAR_SCREEN_LINE
        MOV     SI,[HIST_IDX]
        CMP     SI,[HIST_USED]
        JAE     EK_IGNORE_BR
        ADD     SI,[HIST_OFS]
EK_LH_CP:
        LODSB
        OR      AL,AL
        JZ      EK_IGNORE_BR
        CMP     CL,DL
        JAE     EK_IGNORE_BR
        MOV     BL,CL
        XOR     BH,BH
        MOV     [WORK_LINE+BX],AL
        INC     CL
        INC     CH
        CALL    CON_PUTC
        JMP     EK_LH_CP

EK_F7:
        CALL    CLEAR_SCREEN_LINE
        MOV     AL,0Dh
        CALL    CON_PUTC
        MOV     AL,0Ah
        CALL    CON_PUTC
        CALL    PRINT_NUMBERED_HIST
        JMP     ED_KEY_LOOP

EK_F8:
        ; Search backwards from HIST_IDX for entry matching WORK_LINE[0..CH-1]
        CMP     WORD PTR [HIST_USED],0
        JE      EK_IGNORE_BR
        MOV     SI,[HIST_IDX]
        OR      SI,SI
        JNZ     EK_F8_SCAN
        MOV     SI,[HIST_USED]
EK_F8_SCAN:
        OR      SI,SI
        JZ      EK_IGNORE_BR
        CALL    PREV_HIST_ENTRY
        PUSH    SI
        ADD     SI,[HIST_OFS]
        XOR     BX,BX
EK_F8_CMP:
        CMP     BL,CH
        JAE     EK_F8_MATCH
        MOV     AL,[SI+BX]
        CALL    TO_UPPER_AL
        MOV     AH,AL
        MOV     AL,[WORK_LINE+BX]
        CALL    TO_UPPER_AL
        CMP     AL,AH
        JNE     EK_F8_NOMATCH
        INC     BX
        JMP     EK_F8_CMP
EK_F8_NOMATCH:
        POP     SI
        JMP     EK_F8_SCAN
EK_F8_MATCH:
        POP     SI
        MOV     [HIST_IDX],SI
        MOV     AL,CH                   ; preserve prefix cursor position? DOSKEY puts cursor at end
        JMP     EK_LOAD_HIST

; --- Line Complete (Enter pressed) ---
ED_FINISH_LINE:
        ; Move cursor to end of line and echo CR
        CMP     CH,CL
        JAE     ED_ECHO_CR
ED_FIN_END:
        MOV     BL,CH
        XOR     BH,BH
        MOV     AL,[WORK_LINE+BX]
        INC     CH
        CALL    CON_PUTC
        CMP     CH,CL
        JB      ED_FIN_END
ED_ECHO_CR:
        MOV     AL,0Dh
        CALL    CON_PUTC

        MOV     BL,CL
        XOR     BH,BH
        MOV     BYTE PTR [WORK_LINE+BX],0

        ; Record non-empty line in circular history
        OR      CL,CL
        JZ      ED_FINISH_EMPTY
        CALL    APPEND_HISTORY

        ; Attempt macro expansion on WORK_LINE -> EXP_LINE (and queue $T tail if any)
        CALL    TRY_EXPAND_MACRO
        JNC     ED_USE_WORK

        ; Macro matched! EXP_LINE holds expanded text (possibly with CR from $T)
        MOV     SI,OFFSET EXP_LINE
        CALL    STORE_WITH_CHAIN
        JMP     DBE_EXIT

ED_FINISH_EMPTY:
        XOR     CL,CL
ED_USE_WORK:
        ; Copy WORK_LINE[0..CL-1] + CR into caller ES:BP
        MOV     ES:[BP+1],CL
        MOV     DI,BP
        ADD     DI,2
        MOV     SI,OFFSET WORK_LINE
        XOR     CH,CH
        JCXZ    ED_WR_CR
        REP     MOVSB
ED_WR_CR:
        MOV     BYTE PTR ES:[DI],0Dh
        JMP     DBE_EXIT

DBE_DONE_RAW:
        ASSUME  DS:NOTHING
        MOV     BYTE PTR DS:[BP+1],0
        MOV     BYTE PTR DS:[BP+2],0Dh

DBE_EXIT:
        MOV     BYTE PTR CS:[IN_EDIT_BUSY],0
        POP     ES
        POP     DS
        POP     BP
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
DO_BUFFERED_EDIT ENDP

        ASSUME  DS:CODE

; ---------------------------------------------------------------------------
; Line Editing & Console Helpers (CS == DS == CODE, ES:BP preserved!)
; ---------------------------------------------------------------------------
CON_PUTC        PROC    NEAR
        PUSH    AX
        PUSH    BX
        MOV     AH,0Eh
        XOR     BX,BX
        INT     10h
        POP     BX
        POP     AX
        RET
CON_PUTC        ENDP

CLEAR_SCREEN_LINE PROC  NEAR
        ; Move to end of current text, then backspace-space-backspace CL times
        CMP     CH,CL
        JAE     CSL_BK_LP
CSL_TO_END:
        MOV     BL,CH
        XOR     BH,BH
        MOV     AL,[WORK_LINE+BX]
        INC     CH
        CALL    CON_PUTC
        CMP     CH,CL
        JB      CSL_TO_END
CSL_BK_LP:
        OR      CL,CL
        JZ      CSL_DONE
        MOV     AL,08h
        CALL    CON_PUTC
        MOV     AL,' '
        CALL    CON_PUTC
        MOV     AL,08h
        CALL    CON_PUTC
        DEC     CL
        DEC     CH
        JMP     CSL_BK_LP
CSL_DONE:
        RET
CLEAR_SCREEN_LINE ENDP

DELETE_AT_CURSOR PROC   NEAR
        ; Deletes WORK_LINE[CH], shifts tail left, redraws tail + blank
        MOV     BL,CH
        XOR     BH,BH
        MOV     SI,BX
        MOV     BL,CL
        DEC     BL                      ; new length
DAC_LP:
        CMP     SI,BX
        JAE     DAC_REDRAW
        MOV     AL,[WORK_LINE+SI+1]
        MOV     [WORK_LINE+SI],AL
        INC     SI
        JMP     DAC_LP
DAC_REDRAW:
        DEC     CL
        MOV     BL,CH
        XOR     BH,BH
        MOV     SI,BX
DAC_OUT:
        CMP     BL,CL
        JAE     DAC_ERASE_LAST
        MOV     AL,[WORK_LINE+SI]
        CALL    CON_PUTC
        INC     SI
        INC     BL
        JMP     DAC_OUT
DAC_ERASE_LAST:
        MOV     AL,' '
        CALL    CON_PUTC
        ; Backspace (CL - CH + 1) times
        MOV     BL,CL
        SUB     BL,CH
        INC     BL
DAC_BK:
        MOV     AL,08h
        CALL    CON_PUTC
        DEC     BL
        JNZ     DAC_BK
        RET
DELETE_AT_CURSOR ENDP

REDRAW_TAIL_FROM_CH PROC NEAR
        MOV     BL,CH
        XOR     BH,BH
        MOV     SI,BX
RTF_OUT:
        CMP     BL,CL
        JAE     RTF_BK_PREP
        MOV     AL,[WORK_LINE+SI]
        CALL    CON_PUTC
        INC     SI
        INC     BL
        JMP     RTF_OUT
RTF_BK_PREP:
        MOV     BL,CL
        SUB     BL,CH
        JZ      RTF_DONE
RTF_BK:
        MOV     AL,08h
        CALL    CON_PUTC
        DEC     BL
        JNZ     RTF_BK
RTF_DONE:
        RET
REDRAW_TAIL_FROM_CH ENDP

; ---------------------------------------------------------------------------
; History Ring/List Helpers (entries stored back-to-back as ASCIIZ strings)
; ---------------------------------------------------------------------------
; In: SI = byte offset in HIST_BUF (> 0) -> Out: SI = start of previous entry
PREV_HIST_ENTRY PROC    NEAR
        OR      SI,SI
        JZ      PHE_RET
        DEC     SI                      ; step onto terminating NUL of prev entry
PHE_LP:
        OR      SI,SI
        JZ      PHE_RET
        MOV     BX,[HIST_OFS]
        CMP     BYTE PTR [BX+SI-1],0
        JE      PHE_RET
        DEC     SI
        JMP     PHE_LP
PHE_RET:
        RET
PREV_HIST_ENTRY ENDP

; In: SI = byte offset in HIST_BUF -> Out: SI = start of next entry (<= HIST_USED)
NEXT_HIST_ENTRY PROC    NEAR
        MOV     BX,[HIST_OFS]
NHE_LP:
        CMP     SI,[HIST_USED]
        JAE     NHE_RET
        CMP     BYTE PTR [BX+SI],0
        JE      NHE_PAST_NUL
        INC     SI
        JMP     NHE_LP
NHE_PAST_NUL:
        INC     SI
NHE_RET:
        RET
NEXT_HIST_ENTRY ENDP

APPEND_HISTORY  PROC    NEAR
        ; Compute needed bytes = CL + 1
        XOR     AH,AH
        MOV     AL,CL
        INC     AX                      ; AX = entry_bytes (with NUL)
        CMP     AX,[HIST_CAP]
        JBE     AH_CAP_OK
        RET
AH_CAP_OK:

        ; Skip duplicate if identical to most recent entry
        CMP     WORD PTR [HIST_USED],0
        JE      AH_ENSURE_ROOM
        MOV     SI,[HIST_USED]
        CALL    PREV_HIST_ENTRY
        ADD     SI,[HIST_OFS]
        MOV     DI,OFFSET WORK_LINE
        PUSH    CX
        XOR     CH,CH
        INC     CX                      ; compare CL chars + NUL!
AH_DUP_LP:
        MOV     BL,[SI]
        CMP     BL,[DI]
        JNE     AH_NOT_DUP
        INC     SI
        INC     DI
        LOOP    AH_DUP_LP
        POP     CX
        JMP     AH_SET_END        ; identical to last command -> keep as-is
AH_NOT_DUP:
        POP     CX

AH_ENSURE_ROOM:
        MOV     BX,[HIST_USED]
        ADD     BX,AX
        CMP     BX,[HIST_CAP]
        JBE     AH_COPY_IN
        ; Drop oldest entry at offset 0 and compact remaining bytes down
        XOR     SI,SI
        CALL    NEXT_HIST_ENTRY         ; SI = length of oldest entry
        OR      SI,SI
        JZ      AH_RESET_ALL
        PUSH    AX
        PUSH    CX
        PUSH    ES
        PUSH    CS
        POP     ES
        MOV     CX,[HIST_USED]
        SUB     CX,SI
        MOV     [HIST_USED],CX
        MOV     DI,[HIST_OFS]
        ADD     SI,DI
        JCXZ    AH_COMP_DONE
        REP     MOVSB
AH_COMP_DONE:
        POP     ES
        POP     CX
        POP     AX
        JMP     AH_ENSURE_ROOM

AH_RESET_ALL:
        MOV     WORD PTR [HIST_USED],0

AH_COPY_IN:
        PUSH    CX
        PUSH    ES
        PUSH    CS
        POP     ES
        MOV     DI,[HIST_OFS]
        ADD     DI,[HIST_USED]
        MOV     SI,OFFSET WORK_LINE
        XOR     CH,CH
        INC     CX                      ; include trailing NUL
        ADD     [HIST_USED],CX
        REP     MOVSB
        POP     ES
        POP     CX

AH_SET_END:
        MOV     AX,[HIST_USED]
        MOV     [HIST_IDX],AX
AH_RET:
        RET
APPEND_HISTORY  ENDP

PRINT_NUMBERED_HIST PROC NEAR
        XOR     SI,SI
        MOV     BX,1                    ; 1-based line number
PNH_LP:
        CMP     SI,[HIST_USED]
        JAE     PNH_DONE
        ; Print BX in decimal + ": "
        MOV     AX,BX
        CALL    CON_PRINT_UINT
        MOV     AL,':'
        CALL    CON_PUTC
        MOV     AL,' '
        CALL    CON_PUTC
        MOV     DI,[HIST_OFS]
PNH_CH:
        MOV     AL,[DI+SI]
        INC     SI
        OR      AL,AL
        JZ      PNH_NL
        CALL    CON_PUTC
        JMP     PNH_CH
PNH_NL:
        MOV     AL,0Dh
        CALL    CON_PUTC
        MOV     AL,0Ah
        CALL    CON_PUTC
        INC     BX
        JMP     PNH_LP
PNH_DONE:
        RET
PRINT_NUMBERED_HIST ENDP

CON_PRINT_UINT  PROC    NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        XOR     CX,CX
        MOV     BX,10
CPU_DIV:
        XOR     DX,DX
        DIV     BX
        PUSH    DX
        INC     CX
        OR      AX,AX
        JNZ     CPU_DIV
CPU_OUT:
        POP     AX
        ADD     AL,'0'
        CALL    CON_PUTC
        LOOP    CPU_OUT
        POP     DX
        POP     CX
        POP     BX
        RET
CON_PRINT_UINT  ENDP

TO_UPPER_AL     PROC    NEAR
        CMP     AL,'a'
        JB      TUA_RET
        CMP     AL,'z'
        JA      TUA_RET
        SUB     AL,20h
TUA_RET:
        RET
TO_UPPER_AL     ENDP

; ---------------------------------------------------------------------------
; Macro Expansion (MAC_BUF stores entries as UPPER_NAME,0,BODY,0)
;   In:  WORK_LINE (ASCIIZ)
;   Out: CF=1 if expanded into EXP_LINE, CF=0 if not a macro
; ---------------------------------------------------------------------------
TRY_EXPAND_MACRO PROC   NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI

        CMP     WORD PTR [MAC_USED],0
        JE      TEM_NO_MATCH

        ; Skip leading spaces in WORK_LINE
        MOV     SI,OFFSET WORK_LINE
TEM_SKIP_WS:
        CMP     BYTE PTR [SI],' '
        JE      TEM_WS_ADV
        CMP     BYTE PTR [SI],9
        JNE     TEM_GOT_CMD
TEM_WS_ADV:
        INC     SI
        JMP     TEM_SKIP_WS

TEM_GOT_CMD:
        CMP     BYTE PTR [SI],0
        JE      TEM_NO_MATCH
        MOV     DX,SI                   ; DX = command word start
        XOR     CX,CX                   ; CX = command word length
TEM_CMD_LEN:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      TEM_SEARCH_MAC
        CMP     AL,' '
        JE      TEM_SEARCH_MAC
        CMP     AL,9
        JE      TEM_SEARCH_MAC
        INC     SI
        INC     CX
        JMP     TEM_CMD_LEN

TEM_NO_MATCH:
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        CLC
        RET

TEM_SEARCH_MAC:
        ; Save argument tail (trimmed of leading spaces) into ARG_COPY
TEM_SKIP_ARG_WS:
        CMP     BYTE PTR [SI],' '
        JE      TEM_ARG_ADV
        CMP     BYTE PTR [SI],9
        JNE     TEM_CP_ARGS
TEM_ARG_ADV:
        INC     SI
        JMP     TEM_SKIP_ARG_WS
TEM_CP_ARGS:
        MOV     DI,OFFSET ARG_COPY
TEM_CP_ARG_LP:
        LODSB
        MOV     [DI],AL
        INC     DI
        OR      AL,AL
        JNZ     TEM_CP_ARG_LP

        ; Search MAC_BUF for name matching [DX..DX+CX-1] case-insensitively
        MOV     DI,[MAC_OFS]
        MOV     BX,DI
        ADD     BX,[MAC_USED]           ; BX = end of MAC_BUF
TEM_MAC_LP:
        CMP     DI,BX
        JAE     TEM_NO_MATCH
        PUSH    CX
        PUSH    DX
        MOV     SI,DX
TEM_CMP_NAME:
        MOV     AL,[SI]
        CALL    TO_UPPER_AL
        CMP     AL,[DI]
        JNE     TEM_NEXT_MAC
        INC     SI
        INC     DI
        LOOP    TEM_CMP_NAME
        CMP     BYTE PTR [DI],0         ; exact length match?
        JNE     TEM_NEXT_MAC
        POP     DX
        POP     CX
        INC     DI                      ; DI -> macro body!
        JMP     TEM_EXPAND_BODY

TEM_NEXT_MAC:
        POP     DX
        POP     CX
        ; Skip rest of name + NUL + body + NUL
TEM_SK1:
        CMP     BYTE PTR [DI],0
        JE      TEM_SK2_START
        INC     DI
        JMP     TEM_SK1
TEM_SK2_START:
        INC     DI
TEM_SK2:
        CMP     BYTE PTR [DI],0
        JE      TEM_SK_DONE
        INC     DI
        JMP     TEM_SK2
TEM_SK_DONE:
        INC     DI
        JMP     TEM_MAC_LP

; Expand macro body at [DI] -> EXP_LINE (up to 126 chars)
TEM_EXPAND_BODY:
        MOV     SI,DI
        MOV     DI,OFFSET EXP_LINE
        MOV     CX,126                  ; remaining space in EXP_LINE
TEM_EB_LP:
        LODSB
        OR      AL,AL
        JNZ     TEM_EB_CONT
        JMP     TEM_EB_DONE
TEM_EB_CONT:
        CMP     AL,'$'
        JNE     TEM_EB_PUT
        MOV     AL,[SI]
        OR      AL,AL
        JZ      TEM_EB_PUT_DOLLAR
        INC     SI
        CMP     AL,'$'
        JE      TEM_EB_PUT_DOLLAR
        CMP     AL,'*'
        JE      TEM_EB_ALL_ARGS
        CMP     AL,'1'
        JB      TEM_EB_META
        CMP     AL,'9'
        JBE     TEM_EB_NTH_ARG
TEM_EB_META:
        CALL    TO_UPPER_AL
        CMP     AL,'T'
        JE      TEM_EB_SEP
        CMP     AL,'G'
        JE      TEM_EB_GT
        CMP     AL,'L'
        JE      TEM_EB_LT
        CMP     AL,'B'
        JE      TEM_EB_PIPE
        JMP     TEM_EB_LP

TEM_EB_PUT_DOLLAR:
        MOV     AL,'$'
        JMP     TEM_EB_PUT
TEM_EB_SEP:
        MOV     AL,0Dh                  ; $T command separator
        JMP     TEM_EB_PUT
TEM_EB_GT:
        MOV     AL,'>'
        JMP     TEM_EB_PUT
TEM_EB_LT:
        MOV     AL,'<'
        JMP     TEM_EB_PUT
TEM_EB_PIPE:
        MOV     AL,'|'
TEM_EB_PUT:
        JCXZ    TEM_EB_DONE
        MOV     [DI],AL
        INC     DI
        DEC     CX
        JMP     TEM_EB_LP

TEM_EB_ALL_ARGS:
        PUSH    SI
        MOV     SI,OFFSET ARG_COPY
TEM_AA_LP:
        LODSB
        OR      AL,AL
        JZ      TEM_AA_DONE
        JCXZ    TEM_AA_DONE
        MOV     [DI],AL
        INC     DI
        DEC     CX
        JMP     TEM_AA_LP
TEM_AA_DONE:
        POP     SI
        JMP     TEM_EB_LP

TEM_EB_NTH_ARG:
        SUB     AL,'0'                  ; AL = 1..9
        PUSH    SI
        MOV     SI,OFFSET ARG_COPY
        MOV     AH,AL
TEM_NA_FIND:
        CMP     BYTE PTR [SI],' '
        JE      TEM_NA_WS
        CMP     BYTE PTR [SI],9
        JNE     TEM_NA_AT_TOK
TEM_NA_WS:
        INC     SI
        JMP     TEM_NA_FIND
TEM_NA_AT_TOK:
        CMP     BYTE PTR [SI],0
        JE      TEM_NA_DONE
        DEC     AH
        JZ      TEM_NA_COPY
TEM_NA_SKIP_TOK:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      TEM_NA_DONE
        CMP     AL,' '
        JE      TEM_NA_FIND
        CMP     AL,9
        JE      TEM_NA_FIND
        INC     SI
        JMP     TEM_NA_SKIP_TOK
TEM_NA_COPY:
        LODSB
        OR      AL,AL
        JZ      TEM_NA_DONE
        CMP     AL,' '
        JE      TEM_NA_DONE
        CMP     AL,9
        JE      TEM_NA_DONE
        JCXZ    TEM_NA_DONE
        MOV     [DI],AL
        INC     DI
        DEC     CX
        JMP     TEM_NA_COPY
TEM_NA_DONE:
        POP     SI
        JMP     TEM_EB_LP

TEM_EB_DONE:
        MOV     BYTE PTR [DI],0
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        STC
        RET
TRY_EXPAND_MACRO ENDP

; Copies first command from DS:SI (up to 0Dh or NUL) into ES:BP, queues any tail after 0Dh into PEND_BUF
STORE_WITH_CHAIN PROC   NEAR
        XOR     CL,CL
        MOV     BX,BP
        ADD     BX,2
SWC_LP:
        LODSB
        OR      AL,AL
        JZ      SWC_NO_MORE
        CMP     AL,0Dh
        JE      SWC_QUEUE_REST
        CMP     CL,DL
        JAE     SWC_LP
        MOV     ES:[BX],AL
        INC     BX
        INC     CL
        JMP     SWC_LP

SWC_NO_MORE:
        MOV     BYTE PTR [PEND_LEN],0
        JMP     SWC_FIN

SWC_QUEUE_REST:
        MOV     DI,OFFSET PEND_BUF
        XOR     CH,CH
SWC_Q_LP:
        LODSB
        MOV     [DI],AL
        OR      AL,AL
        JZ      SWC_Q_DONE
        INC     DI
        INC     CH
        CMP     CH,126
        JB      SWC_Q_LP
        MOV     BYTE PTR [DI],0
SWC_Q_DONE:
        MOV     [PEND_LEN],CH

SWC_FIN:
        MOV     ES:[BP+1],CL
        MOV     BYTE PTR ES:[BX],0Dh
        RET
STORE_WITH_CHAIN ENDP

DEQUEUE_AND_RETURN PROC NEAR
        ; Echo queued command so user sees it on console, then return first segment
        MOV     DL,ES:[BP]
        DEC     DL
        CMP     DL,126
        JBE     DAR_OK
        MOV     DL,126
DAR_OK:
        XOR     CL,CL
        MOV     BX,BP
        ADD     BX,2
DAR_LP:
        LODSB
        OR      AL,AL
        JZ      DAR_CLEAR_Q
        CMP     AL,0Dh
        JE      DAR_SHIFT_Q
        CMP     CL,DL
        JAE     DAR_LP
        MOV     ES:[BX],AL
        INC     BX
        INC     CL
        CALL    CON_PUTC
        JMP     DAR_LP

DAR_CLEAR_Q:
        MOV     BYTE PTR [PEND_LEN],0
        JMP     DAR_DONE

DAR_SHIFT_Q:
        MOV     DI,OFFSET PEND_BUF
        XOR     CH,CH
DAR_SH_LP:
        MOV     AL,[SI]
        MOV     [DI],AL
        OR      AL,AL
        JZ      DAR_SH_DONE
        INC     SI
        INC     DI
        INC     CH
        JMP     DAR_SH_LP
DAR_SH_DONE:
        MOV     [PEND_LEN],CH

DAR_DONE:
        MOV     AL,0Dh
        CALL    CON_PUTC
        MOV     ES:[BP+1],CL
        MOV     BYTE PTR ES:[BX],0Dh
        RET
DEQUEUE_AND_RETURN ENDP

FINAL_TSR_STUB:
        ; Zero extra pool above default 1024 bytes (if /BUFSIZE > 512) and stay resident
        CMP     CX,0
        JE      FTS_STAY
        REP     STOSB
FTS_STAY:
        MOV     AX,3100h
        INT     21h

        EVEN
RES_POOL        LABEL   BYTE
        DB      1024 DUP (0)            ; default 512B history + 512B macro pool

; ===========================================================================
; TRANSIENT CLI / INSTALLER CODE
; ===========================================================================

INIT_MAIN:
        CLD
        PUSH    CS
        POP     DS
        PUSH    CS
        POP     ES
        ASSUME  DS:CODE, ES:CODE

        ; Copy PSP command tail (80h) into CMD_TAIL before anything touches memory
        MOV     SI,0080h
        MOV     CL,[SI]
        XOR     CH,CH
        INC     SI
        MOV     DI,OFFSET CMD_TAIL
        REP     MOVSB
        XOR     AL,AL
        STOSB

        ; Parse switches & macro definition in CMD_TAIL
        CALL    PARSE_CLI
        CMP     BYTE PTR [SW_HELP],0
        JE      CLI_NO_HELP
        MOV     DX,OFFSET MSG_HELP
        MOV     AH,09h
        INT     21h
        MOV     AX,4C00h
        INT     21h

CLI_NO_HELP:
        CMP     BYTE PTR [SW_BAD],0
        JE      CLI_SW_OK
        MOV     DX,OFFSET MSG_BAD_SW
        MOV     AH,09h
        INT     21h
        MOV     AX,4C01h
        INT     21h

CLI_SW_OK:
        ; Check if CK-DOS DOSKEY is already resident via INT 2Fh AX=4800h
        XOR     BX,BX
        MOV     ES,BX
        CMP     BYTE PTR [SW_REINSTALL],0
        JNE     CLI_NEED_INSTALL

        MOV     AX,4800h
        INT     2Fh
        CMP     AL,0FFh
        JNE     CLI_NEED_INSTALL
        CMP     WORD PTR ES:[RES_SIG],4B44h
        JE      CLI_HAVE_RESIDENT

CLI_NEED_INSTALL:
        MOV     BYTE PTR [DO_STAY_RES],1
        PUSH    CS
        POP     ES                      ; target our own segment prior to TSR!

CLI_HAVE_RESIDENT:
        ; ES = resident DOSKEY segment (either existing TSR or CS when installing)
        CMP     BYTE PTR [DO_STAY_RES],0
        JE      CLI_APPLY_MODES
        ; Configure buffer sizes in CS
        MOV     AX,[REQ_BUFSIZE]
        MOV     [HIST_CAP],AX
        MOV     [MAC_CAP],AX
        MOV     AX,OFFSET RES_POOL
        MOV     [MAC_OFS],AX
        ADD     AX,[REQ_BUFSIZE]
        MOV     [HIST_OFS],AX
        MOV     WORD PTR [HIST_USED],0
        MOV     WORD PTR [HIST_IDX],0
        MOV     WORD PTR [MAC_USED],0

CLI_APPLY_MODES:
        CMP     BYTE PTR [SW_SET_INS],0
        JE      CLI_CHK_MACRO
        MOV     AL,[SW_INS_VAL]
        MOV     ES:[DEF_INSERT],AL

CLI_CHK_MACRO:
        CMP     WORD PTR [MAC_DEF_PTR],0
        JE      CLI_CHK_LISTS
        CALL    APPLY_MACRO_DEF

CLI_CHK_LISTS:
        CMP     BYTE PTR [SW_HISTORY],0
        JE      CLI_CHK_MACLIST
        CALL    DUMP_RESIDENT_HIST
CLI_CHK_MACLIST:
        CMP     BYTE PTR [SW_MACROS],0
        JE      CLI_FINISH
        CALL    DUMP_RESIDENT_MACS

CLI_FINISH:
        CMP     BYTE PTR [DO_STAY_RES],0
        JNE     INSTALL_TSR_NOW
        MOV     AX,4C00h
        INT     21h

INSTALL_TSR_NOW:
        ; Free environment block (PSP:002Ch) so zero conventional/UMB leak occurs
        MOV     AX,DS:[002Ch]
        OR      AX,AX
        JZ      TSR_HOOK_VECS
        PUSH    ES
        MOV     ES,AX
        MOV     AH,49h
        INT     21h
        POP     ES
        MOV     WORD PTR DS:[002Ch],0

TSR_HOOK_VECS:
        ; Save and hook INT 21h and INT 2Fh
        MOV     AX,3521h
        INT     21h
        MOV     WORD PTR [OLD_INT21],BX
        MOV     WORD PTR [OLD_INT21+2],ES

        MOV     AX,352Fh
        INT     21h
        MOV     WORD PTR [OLD_INT2F],BX
        MOV     WORD PTR [OLD_INT2F+2],ES

        MOV     DX,OFFSET NEW_INT21
        MOV     AX,2521h
        INT     21h

        MOV     DX,OFFSET NEW_INT2F
        MOV     AX,252Fh
        INT     21h

        MOV     DX,OFFSET MSG_INSTALLED
        MOV     AH,09h
        INT     21h

        ; Resident paragraphs = (HIST_OFS + HIST_CAP + 15) >> 4
        MOV     DX,[HIST_OFS]
        ADD     DX,[HIST_CAP]
        ADD     DX,15
        MOV     CL,4
        SHR     DX,CL
        MOV     AX,3100h
        INT     21h

; ---------------------------------------------------------------------------
; APPLY_MACRO_DEF: ES = resident segment, [MAC_DEF_PTR] -> "NAME=[BODY]"
; ---------------------------------------------------------------------------
APPLY_MACRO_DEF PROC    NEAR
        PUSH    ES
        MOV     SI,[MAC_DEF_PTR]
        MOV     DI,OFFSET TMP_MNAME
        XOR     CX,CX
AMD_NAME_LP:
        LODSB
        OR      AL,AL
        JNZ     AMD_NM_CONT
        JMP     AMD_DONE
AMD_NM_CONT:
        CMP     AL,'='
        JE      AMD_GOT_EQ
        CALL    TO_UPPER_AL
        CMP     CX,31
        JAE     AMD_NAME_LP
        MOV     [DI],AL
        INC     DI
        INC     CX
        JMP     AMD_NAME_LP

AMD_GOT_EQ:
        MOV     BYTE PTR [DI],0
        OR      CX,CX
        JNZ     AMD_EQ_CONT
        JMP     AMD_DONE
AMD_EQ_CONT:
        MOV     [MAC_BODY_PTR],SI       ; SI -> body (ASCIIZ, possibly empty)

        ; First remove any existing macro with TMP_MNAME in ES:[MAC_OFS..+MAC_USED]
        MOV     DI,ES:[MAC_OFS]
        MOV     BX,DI
        ADD     BX,ES:[MAC_USED]
AMD_FIND_LP:
        CMP     DI,BX
        JAE     AMD_ADD_NEW
        PUSH    DI
        MOV     SI,OFFSET TMP_MNAME
AMD_CMP_LP:
        MOV     AL,[SI]
        CMP     AL,ES:[DI]
        JNE     AMD_SKIP_ENT
        INC     SI
        INC     DI
        OR      AL,AL
        JNZ     AMD_CMP_LP
        ; Found matching macro starting at [SP]! Advance DI past body+NUL and compact.
AMD_SK_BODY:
        CMP     BYTE PTR ES:[DI],0
        JE      AMD_DO_DEL
        INC     DI
        JMP     AMD_SK_BODY
AMD_DO_DEL:
        INC     DI                      ; DI = next entry
        POP     AX                      ; AX = start of deleted entry
        MOV     CX,BX
        SUB     CX,DI                   ; trailing bytes to shift down
        MOV     DX,DI
        SUB     DX,AX                   ; deleted byte count
        SUB     ES:[MAC_USED],DX
        PUSH    DS
        PUSH    ES
        POP     DS
        MOV     SI,DI
        MOV     DI,AX
        JCXZ    AMD_DEL_DONE
        REP     MOVSB
AMD_DEL_DONE:
        POP     DS
        JMP     AMD_ADD_NEW

AMD_SKIP_ENT:
        POP     DI
AMD_SK1:
        CMP     BYTE PTR ES:[DI],0
        JE      AMD_SK2S
        INC     DI
        JMP     AMD_SK1
AMD_SK2S:
        INC     DI
AMD_SK2:
        CMP     BYTE PTR ES:[DI],0
        JE      AMD_SK_END
        INC     DI
        JMP     AMD_SK2
AMD_SK_END:
        INC     DI
        JMP     AMD_FIND_LP

AMD_ADD_NEW:
        ; If body is empty, deletion only -> done!
        MOV     SI,[MAC_BODY_PTR]
        CMP     BYTE PTR [SI],0
        JE      AMD_DONE

        ; Measure name_len+1 + body_len+1
        MOV     SI,OFFSET TMP_MNAME
        XOR     CX,CX
AMD_NL:
        INC     CX
        LODSB
        OR      AL,AL
        JNZ     AMD_NL
        MOV     SI,[MAC_BODY_PTR]
AMD_BL:
        INC     CX
        LODSB
        OR      AL,AL
        JNZ     AMD_BL

        MOV     AX,ES:[MAC_USED]
        ADD     AX,CX
        CMP     AX,ES:[MAC_CAP]
        JA      AMD_FULL

        MOV     DI,ES:[MAC_OFS]
        ADD     DI,ES:[MAC_USED]
        ADD     ES:[MAC_USED],CX
        MOV     SI,OFFSET TMP_MNAME
AMD_CP_N:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     AMD_CP_N
        MOV     SI,[MAC_BODY_PTR]
AMD_CP_B:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     AMD_CP_B
        JMP     AMD_DONE

AMD_FULL:
        MOV     DX,OFFSET MSG_MAC_FULL
        MOV     AH,09h
        INT     21h
AMD_DONE:
        POP     ES
        RET
APPLY_MACRO_DEF ENDP

; ---------------------------------------------------------------------------
; /HISTORY and /MACROS Dump Helpers (writes to stdout handle 1)
; ---------------------------------------------------------------------------
DUMP_RESIDENT_HIST PROC NEAR
        MOV     SI,ES:[HIST_OFS]
        MOV     BX,SI
        ADD     BX,ES:[HIST_USED]
DRH_LP:
        CMP     SI,BX
        JAE     DRH_DONE
DRH_CH:
        MOV     AL,ES:[SI]
        INC     SI
        OR      AL,AL
        JZ      DRH_NL
        CALL    STDOUT_CHAR
        JMP     DRH_CH
DRH_NL:
        MOV     AL,0Dh
        CALL    STDOUT_CHAR
        MOV     AL,0Ah
        CALL    STDOUT_CHAR
        JMP     DRH_LP
DRH_DONE:
        RET
DUMP_RESIDENT_HIST ENDP

DUMP_RESIDENT_MACS PROC NEAR
        MOV     SI,ES:[MAC_OFS]
        MOV     BX,SI
        ADD     BX,ES:[MAC_USED]
DRM_LP:
        CMP     SI,BX
        JAE     DRM_DONE
DRM_NM:
        MOV     AL,ES:[SI]
        INC     SI
        OR      AL,AL
        JZ      DRM_EQ
        CALL    STDOUT_CHAR
        JMP     DRM_NM
DRM_EQ:
        MOV     AL,'='
        CALL    STDOUT_CHAR
DRM_BD:
        MOV     AL,ES:[SI]
        INC     SI
        OR      AL,AL
        JZ      DRM_NL
        CALL    STDOUT_CHAR
        JMP     DRM_BD
DRM_NL:
        MOV     AL,0Dh
        CALL    STDOUT_CHAR
        MOV     AL,0Ah
        CALL    STDOUT_CHAR
        JMP     DRM_LP
DRM_DONE:
        RET
DUMP_RESIDENT_MACS ENDP

STDOUT_CHAR     PROC    NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     [ONE_CHAR],AL
        MOV     BX,1
        MOV     CX,1
        MOV     DX,OFFSET ONE_CHAR
        MOV     AH,40h
        INT     21h
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
STDOUT_CHAR     ENDP

; ---------------------------------------------------------------------------
; PARSE_CLI: parses switches and optional macro definition in CMD_TAIL
; ---------------------------------------------------------------------------
PARSE_CLI       PROC    NEAR
        MOV     SI,OFFSET CMD_TAIL
PC_LOOP:
        MOV     AL,[SI]
        OR      AL,AL
        JNZ     PC_NOT_END
        RET
PC_NOT_END:
        CMP     AL,0Dh
        JNE     PC_NOT_CR
        MOV     BYTE PTR [SI],0
        RET
PC_NOT_CR:
        CMP     AL,' '
        JE      PC_WS
        CMP     AL,9
        JNE     PC_ITEM
PC_WS:
        INC     SI
        JMP     PC_LOOP

PC_ITEM:
        CMP     AL,'/'
        JE      PC_SWITCH

        ; Non-switch: treat rest of line (trimmed of trailing CR) as macro def NAME=[BODY]
        MOV     [MAC_DEF_PTR],SI
PC_TRIM_CR:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PC_CHK_EQ
        CMP     AL,0Dh
        JE      PC_ZERO_CR
        INC     SI
        JMP     PC_TRIM_CR
PC_ZERO_CR:
        MOV     BYTE PTR [SI],0
PC_CHK_EQ:
        ; Verify '=' exists in [MAC_DEF_PTR]
        MOV     SI,[MAC_DEF_PTR]
PC_EQ_SCAN:
        LODSB
        OR      AL,AL
        JZ      PC_BAD_DEF
        CMP     AL,'='
        JNE     PC_EQ_SCAN
        RET
PC_BAD_DEF:
        MOV     BYTE PTR [SW_BAD],1
        RET

PC_SWITCH:
        INC     SI                      ; skip '/'
        MOV     AL,[SI]
        CMP     AL,'?'
        JNE     PC_SW_LETTER
        MOV     BYTE PTR [SW_HELP],1
        RET

PC_SW_LETTER:
        CALL    TO_UPPER_AL
        CMP     AL,'R'                  ; /REINSTALL
        JE      PC_SW_REINST
        CMP     AL,'M'                  ; /MACROS or /M
        JE      PC_SW_MACS
        CMP     AL,'H'                  ; /HISTORY or /H
        JE      PC_SW_HIST
        CMP     AL,'I'                  ; /INSERT
        JE      PC_SW_INS
        CMP     AL,'O'                  ; /OVERSTRIKE
        JE      PC_SW_OVS
        CMP     AL,'B'                  ; /BUFSIZE=n
        JE      PC_SW_BUF
        MOV     BYTE PTR [SW_BAD],1
        RET

PC_SW_REINST:
        MOV     BYTE PTR [SW_REINSTALL],1
        JMP     PC_SKIP_SW_WORD
PC_SW_MACS:
        MOV     BYTE PTR [SW_MACROS],1
        JMP     PC_SKIP_SW_WORD
PC_SW_HIST:
        MOV     BYTE PTR [SW_HISTORY],1
        JMP     PC_SKIP_SW_WORD
PC_SW_INS:
        MOV     BYTE PTR [SW_SET_INS],1
        MOV     BYTE PTR [SW_INS_VAL],1
        JMP     PC_SKIP_SW_WORD
PC_SW_OVS:
        MOV     BYTE PTR [SW_SET_INS],1
        MOV     BYTE PTR [SW_INS_VAL],0

PC_SKIP_SW_WORD:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PC_SW_RET
        CMP     AL,0Dh
        JE      PC_SW_RET
        CMP     AL,' '
        JE      PC_GO_LOOP
        CMP     AL,9
        JE      PC_GO_LOOP
        CMP     AL,'/'
        JE      PC_GO_LOOP
        INC     SI
        JMP     PC_SKIP_SW_WORD
PC_GO_LOOP:
        JMP     PC_LOOP
PC_SW_RET:
        RET

PC_SW_BUF:
        ; Advance to '=' or ':' then parse decimal integer
PC_BUF_EQ:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PC_BUF_BAD
        CMP     AL,0Dh
        JE      PC_BUF_BAD
        INC     SI
        CMP     AL,'='
        JE      PC_BUF_NUM
        CMP     AL,':'
        JNE     PC_BUF_EQ
PC_BUF_NUM:
        XOR     AX,AX
        XOR     CX,CX
PC_BN_LP:
        MOV     BL,[SI]
        CMP     BL,'0'
        JB      PC_BN_DONE
        CMP     BL,'9'
        JA      PC_BN_DONE
        SUB     BL,'0'
        XOR     BH,BH
        MOV     DX,10
        MUL     DX
        ADD     AX,BX
        INC     SI
        INC     CX
        JMP     PC_BN_LP
PC_BN_DONE:
        OR      CX,CX
        JNZ     PC_BN_OK
PC_BUF_BAD:
        JMP     PC_BAD_DEF
PC_BN_OK:
        CMP     AX,256
        JAE     PC_BN_MAX
        MOV     AX,256
PC_BN_MAX:
        CMP     AX,4096
        JBE     PC_BN_SAVE
        MOV     AX,4096
PC_BN_SAVE:
        MOV     [REQ_BUFSIZE],AX
        JMP     PC_LOOP
PARSE_CLI       ENDP

; ---------------------------------------------------------------------------
; Transient Messages & Variables
; ---------------------------------------------------------------------------
MSG_INSTALLED   DB      'DOSKey installed.',13,10,'$'
MSG_BAD_SW      DB      'Invalid switch or macro definition. Type DOSKEY /? for help.',13,10,'$'
MSG_MAC_FULL    DB      'Out of macro space.',13,10,'$'
MSG_HELP        DB      'Edits command lines, recalls CK-DOS commands, and creates macros.',13,10,13,10
                DB      'DOSKEY [/REINSTALL] [/BUFSIZE=size] [/MACROS] [/HISTORY]',13,10
                DB      '       [/INSERT | /OVERSTRIKE] [macroname=[text]]',13,10,13,10
                DB      '  /REINSTALL    Installs a new copy of DOSKey.',13,10
                DB      '  /BUFSIZE=size Sets size of command history and macro buffer.',13,10
                DB      '  /MACROS       Displays all DOSKey macros.',13,10
                DB      '  /HISTORY      Displays all commands stored in memory.',13,10
                DB      '  /INSERT       Inserts new characters into line when typing.',13,10
                DB      '  /OVERSTRIKE   Overwrites characters on line when typing.',13,10
                DB      '  macroname     Specifies a name for a macro you create.',13,10
                DB      '  text          Specifies commands you want to record.',13,10,'$'

SW_HELP         DB      0
SW_BAD          DB      0
SW_REINSTALL    DB      0
SW_MACROS       DB      0
SW_HISTORY      DB      0
SW_SET_INS      DB      0
SW_INS_VAL      DB      1
DO_STAY_RES     DB      0
ONE_CHAR        DB      0

REQ_BUFSIZE     DW      512
MAC_DEF_PTR     DW      0
MAC_BODY_PTR    DW      0

TMP_MNAME       DB      36 DUP (0)
CMD_TAIL        DB      132 DUP (0)

HOST_RESP_BUF   LABEL   BYTE

CODE    ENDS
        END     START
