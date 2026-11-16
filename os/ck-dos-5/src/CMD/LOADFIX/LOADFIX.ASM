; Copyright (c) 2026 C. Klukas
; SPDX-License-Identifier: MIT
;
; LOADFIX.COM — Clean-room CK-DOS 5.0 conventional memory loader above 64 KB.
;
; Ensures a program is loaded above the first 64 KB of conventional memory
; (segment >= 1000h) to avoid the EXEPACK A20 wraparound bug when DOS=HIGH.

.8086

CODE    SEGMENT PARA PUBLIC 'CODE'
        ASSUME  CS:CODE, DS:CODE, ES:CODE, SS:CODE
        ORG     100h

START:
        JMP     MAIN

SAVED_SS        DW      0
SAVED_SP        DW      0
OLD_STRAT       DB      0
OLD_UMB         DB      0
EXEC_FAILED     DB      0
PAD_COUNT       DW      0
PAD_SEGS        DW      32 DUP (0)

EXEC_BLK        LABEL   BYTE
EB_ENV          DW      0               ; 0 = inherit parent environment
EB_CMD_OFF      DW      OFFSET CHILD_TAIL
EB_CMD_SEG      DW      0
EB_FCB1_OFF     DW      OFFSET CHILD_FCB1
EB_FCB1_SEG     DW      0
EB_FCB2_OFF     DW      OFFSET CHILD_FCB2
EB_FCB2_SEG     DW      0

MAIN:
        CLD
        ; Set up local stack and shrink our MCB so memory above us is free
        MOV     SP,OFFSET TOP_OF_STACK
        MOV     BX,OFFSET END_IMAGE + 15
        MOV     CL,4
        SHR     BX,CL
        PUSH    CS
        POP     ES
        MOV     AH,4Ah
        INT     21h

        ; Parse command line at PSP:81h
        MOV     SI,0081h
SKIP_WS:
        MOV     AL,[SI]
        CMP     AL,' '
        JE      ADV_WS
        CMP     AL,9
        JE      ADV_WS
        JMP     SHORT CHK_ARG
ADV_WS:
        INC     SI
        JMP     SHORT SKIP_WS

CHK_ARG:
        CMP     AL,0Dh
        JE      ERR_NO_CMD
        OR      AL,AL
        JZ      ERR_NO_CMD
        CMP     AL,'/'
        JNE     GOT_CMD_START
        CMP     BYTE PTR [SI+1],'?'
        JNE     GOT_CMD_START

        MOV     DX,OFFSET MSG_HELP
        MOV     AH,09h
        INT     21h
        MOV     AX,4C00h
        INT     21h

ERR_NO_CMD:
        MOV     DX,OFFSET MSG_SYNTAX
        MOV     AH,09h
        INT     21h
        MOV     AX,4C01h
        INT     21h

GOT_CMD_START:
        ; Extract program token into PROG_TOKEN
        MOV     DI,OFFSET PROG_TOKEN
        MOV     CX,78
TOK_LP:
        MOV     AL,[SI]
        CMP     AL,0Dh
        JE      TOK_DONE
        OR      AL,AL
        JZ      TOK_DONE
        CMP     AL,' '
        JE      TOK_DONE
        CMP     AL,9
        JE      TOK_DONE
        CMP     AL,'/'
        JE      TOK_DONE
        INC     SI
        STOSB
        LOOP    TOK_LP
TOK_DONE:
        XOR     AL,AL
        STOSB

        ; Copy remainder of command line (starting at SI) into CHILD_TAIL
        MOV     DI,OFFSET CHILD_TAIL + 1
        XOR     CX,CX
TAIL_LP:
        MOV     AL,[SI]
        CMP     AL,0Dh
        JE      TAIL_DONE
        OR      AL,AL
        JZ      TAIL_DONE
        INC     SI
        STOSB
        INC     CL
        CMP     CL,125
        JB      TAIL_LP
TAIL_DONE:
        MOV     AL,0Dh
        STOSB
        MOV     BYTE PTR [CHILD_TAIL],CL

        ; Build default FCB1 and FCB2 from CHILD_TAIL+1
        PUSH    SI
        MOV     SI,OFFSET CHILD_TAIL + 1
        MOV     DI,OFFSET CHILD_FCB1
        MOV     AX,2901h
        INT     21h
        MOV     DI,OFFSET CHILD_FCB2
        MOV     AX,2901h
        INT     21h
        POP     SI

        ; Resolve PROG_TOKEN -> EXEC_PATH (direct, +.COM/+.EXE, or via PATH=)
        CALL    RESOLVE_EXECUTABLE
        JNC     ALLOC_PADDING
        MOV     DX,OFFSET MSG_NOT_FOUND
        MOV     AH,09h
        INT     21h
        MOV     AX,4C02h
        INT     21h

ALLOC_PADDING:
        ; Save allocation strategy and UMB link; force low first-fit
        MOV     AX,5800h
        INT     21h
        MOV     [OLD_STRAT],AL
        MOV     AX,5802h
        INT     21h
        MOV     [OLD_UMB],AL

        MOV     AX,5803h
        XOR     BX,BX
        INT     21h
        MOV     AX,5801h
        XOR     BX,BX
        INT     21h

PAD_LOOP:
        CMP     WORD PTR [PAD_COUNT],32
        JAE     PAD_DONE
        MOV     BX,1
        MOV     AH,48h
        INT     21h
        JC      PAD_DONE
        CMP     AX,1000h
        JB      PAD_KEEP
        ; Already >= 1000h! Free immediately and stop padding.
        MOV     ES,AX
        MOV     AH,49h
        INT     21h
        JMP     SHORT PAD_DONE

PAD_KEEP:
        ; Grow block AX up toward 1000h
        MOV     ES,AX
        MOV     BX,1000h
        SUB     BX,AX
        MOV     AH,4Ah
        INT     21h
        JNC     PAD_RECORD
        ; BX holds max available size in this gap; claim all of BX
        OR      BX,BX
        JZ      PAD_RECORD
        MOV     AH,4Ah
        INT     21h
PAD_RECORD:
        MOV     DI,[PAD_COUNT]
        SHL     DI,1
        MOV     AX,ES
        MOV     [PAD_SEGS+DI],AX
        INC     WORD PTR [PAD_COUNT]
        JMP     SHORT PAD_LOOP

PAD_DONE:
        PUSH    CS
        POP     ES
        XOR     BH,BH
        MOV     BL,[OLD_STRAT]
        MOV     AX,5801h
        INT     21h
        XOR     BH,BH
        MOV     BL,[OLD_UMB]
        MOV     AX,5803h
        INT     21h

        ; Execute child above 1000h
        MOV     AX,CS
        MOV     [EB_CMD_SEG],AX
        MOV     [EB_FCB1_SEG],AX
        MOV     [EB_FCB2_SEG],AX
        MOV     [SAVED_SS],SS
        MOV     [SAVED_SP],SP

        MOV     DX,OFFSET EXEC_PATH
        MOV     BX,OFFSET EXEC_BLK
        MOV     AX,4B00h
        INT     21h
        CLI
        MOV     SS,CS:[SAVED_SS]
        MOV     SP,CS:[SAVED_SP]
        STI
        PUSH    CS
        POP     DS
        PUSH    CS
        POP     ES
        MOV     BYTE PTR [EXEC_FAILED],0
        JNC     FREE_PADS
        MOV     BYTE PTR [EXEC_FAILED],1

FREE_PADS:
        MOV     CX,[PAD_COUNT]
        JCXZ    AFTER_FREE
        XOR     SI,SI
FP_LP:
        MOV     ES,[PAD_SEGS+SI]
        MOV     AH,49h
        INT     21h
        ADD     SI,2
        LOOP    FP_LP
        PUSH    CS
        POP     ES

AFTER_FREE:
        CMP     BYTE PTR [EXEC_FAILED],0
        JE      GET_CHILD_RC
        MOV     DX,OFFSET MSG_EXEC_ERR
        MOV     AH,09h
        INT     21h
        MOV     AX,4C01h
        INT     21h

GET_CHILD_RC:
        MOV     AH,4Dh
        INT     21h
        MOV     AH,4Ch
        INT     21h

; ---------------------------------------------------------------------------
; RESOLVE_EXECUTABLE: resolves PROG_TOKEN into EXEC_PATH
; ---------------------------------------------------------------------------
RESOLVE_EXECUTABLE PROC NEAR
        ; Check if PROG_TOKEN contains '.' (explicit extension) or '\' / ':'
        MOV     BYTE PTR [HAS_EXT],0
        MOV     BYTE PTR [HAS_DIR],0
        MOV     SI,OFFSET PROG_TOKEN
RE_SCAN:
        LODSB
        OR      AL,AL
        JZ      RE_SCAN_DONE
        CMP     AL,':'
        JE      RE_SET_DIR
        CMP     AL,'\'
        JE      RE_SET_DIR
        CMP     AL,'/'
        JE      RE_SET_DIR
        CMP     AL,'.'
        JNE     RE_SCAN
        MOV     BYTE PTR [HAS_EXT],1
        JMP     SHORT RE_SCAN
RE_SET_DIR:
        MOV     BYTE PTR [HAS_DIR],1
        MOV     BYTE PTR [HAS_EXT],0
        JMP     SHORT RE_SCAN

RE_SCAN_DONE:
        ; Try current directory / given path first
        MOV     SI,OFFSET EMPTY_STR
        CALL    TRY_CANDIDATE_DIR
        JC      RE_CHK_DIR
        CLC
        RET
RE_CHK_DIR:
        CMP     BYTE PTR [HAS_DIR],0
        JE      RE_SEARCH_PATH
        STC
        RET
RE_SEARCH_PATH:

        ; Search PATH= in environment (PSP:[2Ch])
        MOV     AX,DS:[002Ch]
        OR      AX,AX
        JNZ     RE_HAS_ENV
        STC
        RET
RE_HAS_ENV:
        MOV     ES,AX
        XOR     DI,DI
RE_ENV_LP:
        CMP     BYTE PTR ES:[DI],0
        JE      RE_ENV_END
        ; Check "PATH="
        MOV     AL,ES:[DI]
        AND     AL,0DFh
        CMP     AL,'P'
        JNE     RE_NEXT_VAR
        MOV     AL,ES:[DI+1]
        AND     AL,0DFh
        CMP     AL,'A'
        JNE     RE_NEXT_VAR
        MOV     AL,ES:[DI+2]
        AND     AL,0DFh
        CMP     AL,'T'
        JNE     RE_NEXT_VAR
        MOV     AL,ES:[DI+3]
        AND     AL,0DFh
        CMP     AL,'H'
        JNE     RE_NEXT_VAR
        CMP     BYTE PTR ES:[DI+4],'='
        JE      RE_GOT_PATH
RE_NEXT_VAR:
        CMP     BYTE PTR ES:[DI],0
        JE      RE_AFTER_NUL
        INC     DI
        JMP     SHORT RE_NEXT_VAR
RE_AFTER_NUL:
        INC     DI
        JMP     SHORT RE_ENV_LP

RE_GOT_PATH:
        ADD     DI,5                    ; ES:DI -> PATH value
RE_PATH_ENTRY:
        CMP     BYTE PTR ES:[DI],0
        JE      RE_ENV_END
        MOV     BX,OFFSET DIR_BUF
RE_CP_DIR:
        MOV     AL,ES:[DI]
        OR      AL,AL
        JZ      RE_END_DIR
        INC     DI
        CMP     AL,';'
        JE      RE_END_DIR
        MOV     [BX],AL
        INC     BX
        JMP     SHORT RE_CP_DIR
RE_END_DIR:
        MOV     BYTE PTR [BX],0
        CMP     BX,OFFSET DIR_BUF
        JE      RE_PATH_ENTRY
        PUSH    ES
        PUSH    DI
        PUSH    CS
        POP     ES
        MOV     SI,OFFSET DIR_BUF
        CALL    TRY_CANDIDATE_DIR
        POP     DI
        POP     ES
        JNC     RE_FOUND_FIX_ES
        JMP     SHORT RE_PATH_ENTRY

RE_FOUND_FIX_ES:
        PUSH    CS
        POP     ES
RE_FOUND:
        CLC
        RET

RE_ENV_END:
        PUSH    CS
        POP     ES
RE_NOT_FOUND:
        STC
        RET
RESOLVE_EXECUTABLE ENDP

; Input: DS:SI -> directory prefix ("" for current dir), ES = CS
TRY_CANDIDATE_DIR PROC NEAR
        CMP     BYTE PTR [HAS_EXT],0
        JE      TCD_TRY_EXTS
        MOV     BX,OFFSET EMPTY_STR
        CALL    BUILD_AND_CHECK
        RET
TCD_TRY_EXTS:
        PUSH    SI
        MOV     BX,OFFSET EXT_COM
        CALL    BUILD_AND_CHECK
        POP     SI
        JNC     TCD_OK
        MOV     BX,OFFSET EXT_EXE
        CALL    BUILD_AND_CHECK
TCD_OK:
        RET
TRY_CANDIDATE_DIR ENDP

; Input: DS:SI -> dir, DS:BX -> ext -> writes EXEC_PATH and checks file exists
BUILD_AND_CHECK PROC    NEAR
        MOV     DI,OFFSET EXEC_PATH
BC_DIR:
        LODSB
        OR      AL,AL
        JZ      BC_SEP
        STOSB
        JMP     SHORT BC_DIR
BC_SEP:
        CMP     DI,OFFSET EXEC_PATH
        JE      BC_NAME
        MOV     AL,[DI-1]
        CMP     AL,'\'
        JE      BC_NAME
        CMP     AL,'/'
        JE      BC_NAME
        CMP     AL,':'
        JE      BC_NAME
        MOV     AL,'\'
        STOSB
BC_NAME:
        MOV     SI,OFFSET PROG_TOKEN
BC_NM_LP:
        LODSB
        OR      AL,AL
        JZ      BC_EXT
        STOSB
        JMP     SHORT BC_NM_LP
BC_EXT:
        MOV     SI,BX
BC_EX_LP:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     BC_EX_LP

        ; Check file attributes (AH=43h AL=00h) and ensure not a directory
        MOV     DX,OFFSET EXEC_PATH
        MOV     AX,4300h
        INT     21h
        JC      BC_MISS
        TEST    CX,10h                  ; directory attribute?
        JNZ     BC_MISS
        CLC
        RET
BC_MISS:
        STC
        RET
BUILD_AND_CHECK ENDP

EMPTY_STR       DB      0
EXT_COM         DB      '.COM',0
EXT_EXE         DB      '.EXE',0

MSG_HELP        DB      'Loads a program above the first 64K of memory, and runs the program.',13,10,13,10
                DB      'LOADFIX [drive:][path]filename [program-parameters]',13,10,'$'
MSG_SYNTAX      DB      'Required parameter missing. Type LOADFIX /? for help.',13,10,'$'
MSG_NOT_FOUND   DB      'Error: File not found.',13,10,'$'
MSG_EXEC_ERR    DB      'Error loading program.',13,10,'$'

HAS_EXT         DB      0
HAS_DIR         DB      0
PROG_TOKEN      DB      80 DUP (0)
DIR_BUF         DB      80 DUP (0)
EXEC_PATH       DB      128 DUP (0)
CHILD_TAIL      DB      130 DUP (0)
CHILD_FCB1      DB      37 DUP (0)
CHILD_FCB2      DB      37 DUP (0)

        EVEN
        DW      128 DUP (0)
TOP_OF_STACK    LABEL   WORD
END_IMAGE       LABEL   BYTE

CODE    ENDS
        END     START
