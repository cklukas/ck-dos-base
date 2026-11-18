; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; DOSSHELL.COM — Clean-room 16-bit CK-DOS 5.0 Shell Entry Stub.
;
; Shrinks its own MCB to ~700 bytes, locates DOSSHELL.EXE via DOSSHELL=,
; startup argv[0] directory, current directory, C:\DOS, or PATH=, forwards
; the caller's exact PSP command tail and environment through INT 21h/4B00h,
; and propagates DOSSHELL.EXE's return code.

.8086

CODE    SEGMENT PARA PUBLIC 'CODE'
        ASSUME  CS:CODE, DS:CODE, ES:CODE, SS:CODE
        ORG     100h

START:
        JMP     MAIN

SAVED_SS        DW      0
SAVED_SP        DW      0

EXEC_BLK        LABEL   BYTE
EB_ENV          DW      0
EB_CMD_OFF      DW      0080h
EB_CMD_SEG      DW      0
EB_FCB1_OFF     DW      005Ch
EB_FCB1_SEG     DW      0
EB_FCB2_OFF     DW      006Ch
EB_FCB2_SEG     DW      0

EXE_LEAF        DB      'DOSSHELL.EXE', 0
FALLBACK_DOS    DB      'C:\DOS\DOSSHELL.EXE', 0
KEY_DOSSHELL    DB      'DOSSHELL=', 0
KEY_PATH        DB      'PATH=', 0
MSG_MISSING     DB      'Cannot find DOSSHELL.EXE', 13, 10, '$'

MAIN:
        CLD
        CLI
        MOV     AX, CS
        MOV     SS, AX
        MOV     SP, OFFSET TOP_OF_STACK
        STI

        MOV     BX, OFFSET END_IMAGE + 15
        MOV     CL, 4
        SHR     BX, CL
        PUSH    CS
        POP     ES
        MOV     AH, 4Ah
        INT     21h

        ; 1. Try DOSSHELL=<dir>\DOSSHELL.EXE from environment
        MOV     SI, OFFSET KEY_DOSSHELL
        CALL    FIND_ENV_VAL
        JC      TRY_ARGV0
        CALL    BUILD_DIR_CAND
        CALL    CHECK_FILE_EXISTS
        JC      TRY_ARGV0
        JMP     FOUND_EXE

TRY_ARGV0:
        ; 2. Try directory of DOS 3+ argv[0] trailer in environment
        MOV     ES, DS:[002Ch]
        MOV     AX, ES
        OR      AX, AX
        JZ      TRY_LOCAL
        XOR     DI, DI
SCAN_ENV_END:
        CMP     BYTE PTR ES:[DI], 0
        JE      AT_STR_END
        INC     DI
        CMP     DI, 7FF0h
        JB      SCAN_ENV_END
        JMP     SHORT TRY_LOCAL
AT_STR_END:
        INC     DI
        CMP     BYTE PTR ES:[DI], 0
        JNE     SCAN_ENV_END
        INC     DI
        CMP     WORD PTR ES:[DI], 1
        JNE     TRY_LOCAL
        ADD     DI, 2
        ; Copy argv[0] to CAND_BUF and replace leaf with DOSSHELL.EXE
        MOV     BX, OFFSET CAND_BUF
        MOV     DX, BX                  ; DX tracks position after last '\' or ':'
COPY_ARGV0:
        MOV     AL, ES:[DI]
        MOV     [BX], AL
        OR      AL, AL
        JZ      ARGV0_COPIED
        INC     DI
        INC     BX
        CMP     AL, '\'
        JE      MARK_SEP
        CMP     AL, '/'
        JE      MARK_SEP
        CMP     AL, ':'
        JNE     CHK_A0_LEN
MARK_SEP:
        MOV     DX, BX
CHK_A0_LEN:
        CMP     BX, OFFSET CAND_BUF + 110
        JB      COPY_ARGV0
        JMP     SHORT TRY_LOCAL
ARGV0_COPIED:
        PUSH    CS
        POP     ES
        MOV     DI, DX
        MOV     SI, OFFSET EXE_LEAF
        CALL    STRCPY_NEAR
        CALL    CHECK_FILE_EXISTS
        JC      TRY_LOCAL
        JMP     FOUND_EXE

TRY_LOCAL:
        PUSH    CS
        POP     ES
        ; 3. Try DOSSHELL.EXE in current directory
        MOV     DI, OFFSET CAND_BUF
        MOV     SI, OFFSET EXE_LEAF
        CALL    STRCPY_NEAR
        CALL    CHECK_FILE_EXISTS
        JNC     FOUND_EXE

        ; 4. Try C:\DOS\DOSSHELL.EXE
        MOV     DI, OFFSET CAND_BUF
        MOV     SI, OFFSET FALLBACK_DOS
        CALL    STRCPY_NEAR
        CALL    CHECK_FILE_EXISTS
        JNC     FOUND_EXE

        ; 5. Search PATH= entries
        MOV     SI, OFFSET KEY_PATH
        CALL    FIND_ENV_VAL
        JC      NOT_FOUND
PATH_LOOP:
        CMP     BYTE PTR ES:[DI], 0
        JE      NOT_FOUND
        CMP     BYTE PTR ES:[DI], ';'
        JNE     PATH_SEG
        INC     DI
        JMP     SHORT PATH_LOOP
PATH_SEG:
        PUSH    DI
        CALL    BUILD_PATH_ELEM
        POP     DI
        ; Advance ES:DI past current ';' element before testing
ADV_ELEM:
        MOV     AL, ES:[DI]
        OR      AL, AL
        JZ      TEST_PATH_CAND
        INC     DI
        CMP     AL, ';'
        JNE     ADV_ELEM
TEST_PATH_CAND:
        PUSH    ES
        PUSH    DI
        PUSH    CS
        POP     ES
        CALL    CHECK_FILE_EXISTS
        POP     DI
        POP     ES
        JNC     FOUND_EXE
        JMP     SHORT PATH_LOOP

NOT_FOUND:
        PUSH    CS
        POP     DS
        MOV     DX, OFFSET MSG_MISSING
        MOV     AH, 09h
        INT     21h
        MOV     AX, 4C01h
        INT     21h

FOUND_EXE:
        PUSH    CS
        POP     DS
        PUSH    CS
        POP     ES
        MOV     AX, CS
        MOV     EB_CMD_SEG, AX
        MOV     EB_FCB1_SEG, AX
        MOV     EB_FCB2_SEG, AX
        MOV     SAVED_SS, SS
        MOV     SAVED_SP, SP

        MOV     DX, OFFSET CAND_BUF
        MOV     BX, OFFSET EXEC_BLK
        MOV     AX, 4B00h
        INT     21h

        CLI
        MOV     SS, CS:SAVED_SS
        MOV     SP, CS:SAVED_SP
        STI
        PUSH    CS
        POP     DS
        JC      NOT_FOUND

        MOV     AX, 4D00h
        INT     21h
        MOV     AH, 4Ch
        INT     21h

; ---------------------------------------------------------------------------
; Helpers
; ---------------------------------------------------------------------------

; Copy DS:SI -> ES:DI including NUL
STRCPY_NEAR PROC NEAR
SC_LP:
        LODSB
        STOSB
        OR      AL, AL
        JNZ     SC_LP
        RET
STRCPY_NEAR ENDP

; Check CAND_BUF exists as a regular file (CF=0 ok, CF=1 missing/dir)
CHECK_FILE_EXISTS PROC NEAR
        PUSH    AX
        PUSH    CX
        PUSH    DX
        MOV     DX, OFFSET CAND_BUF
        MOV     AX, 4300h
        INT     21h
        JC      CFE_FAIL
        TEST    CX, 18h                 ; directory or volume label
        JNZ     CFE_FAIL
        CLC
        JMP     SHORT CFE_OUT
CFE_FAIL:
        STC
CFE_OUT:
        POP     DX
        POP     CX
        POP     AX
        RET
CHECK_FILE_EXISTS ENDP

; Find environment prefix at DS:SI ("KEY="). Returns CF=0 and ES:DI pointing
; to value inside environment segment, or CF=1.
FIND_ENV_VAL PROC NEAR
        MOV     ES, DS:[002Ch]
        MOV     AX, ES
        OR      AX, AX
        JZ      FEV_NONE
        XOR     DI, DI
FEV_ENTRY:
        CMP     BYTE PTR ES:[DI], 0
        JE      FEV_NONE
        PUSH    SI
        PUSH    DI
FEV_CMP:
        MOV     AL, [SI]
        OR      AL, AL
        JZ      FEV_MATCH
        MOV     AH, ES:[DI]
        CMP     AH, 'a'
        JB      FEV_UP_DONE
        CMP     AH, 'z'
        JA      FEV_UP_DONE
        SUB     AH, 20h
FEV_UP_DONE:
        CMP     AL, AH
        JNE     FEV_NEXT
        INC     SI
        INC     DI
        JMP     SHORT FEV_CMP
FEV_MATCH:
        POP     AX                      ; discard saved entry start
        POP     SI
        CLC
        RET
FEV_NEXT:
        POP     DI
        POP     SI
FEV_SKIP:
        INC     DI
        CMP     DI, 7FF0h
        JAE     FEV_NONE
        CMP     BYTE PTR ES:[DI-1], 0
        JNE     FEV_SKIP
        JMP     SHORT FEV_ENTRY
FEV_NONE:
        STC
        RET
FIND_ENV_VAL ENDP

; Build "<ES:DI up to NUL or ';'>\DOSSHELL.EXE" into CS:CAND_BUF
BUILD_DIR_CAND PROC NEAR
BUILD_PATH_ELEM LABEL NEAR
        MOV     BX, OFFSET CAND_BUF
BPE_LP:
        MOV     AL, ES:[DI]
        OR      AL, AL
        JZ      BPE_SEP
        CMP     AL, ';'
        JE      BPE_SEP
        MOV     [BX], AL
        INC     DI
        INC     BX
        CMP     BX, OFFSET CAND_BUF + 110
        JB      BPE_LP
BPE_SEP:
        CMP     BX, OFFSET CAND_BUF
        JE      BPE_LEAF
        MOV     AL, [BX-1]
        CMP     AL, '\'
        JE      BPE_LEAF
        CMP     AL, '/'
        JE      BPE_LEAF
        CMP     AL, ':'
        JE      BPE_LEAF
        MOV     BYTE PTR [BX], '\'
        INC     BX
BPE_LEAF:
        PUSH    ES
        PUSH    DI
        PUSH    CS
        POP     ES
        MOV     DI, BX
        MOV     SI, OFFSET EXE_LEAF
        CALL    STRCPY_NEAR
        POP     DI
        POP     ES
        RET
BUILD_DIR_CAND ENDP

CAND_BUF        DB      132 DUP (0)
                DW      128 DUP (0)
TOP_OF_STACK    LABEL   WORD
END_IMAGE       LABEL   BYTE

CODE    ENDS
        END     START
