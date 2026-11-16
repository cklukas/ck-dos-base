; Copyright (c) 2026 C. Klukas
; SPDX-License-Identifier: MIT
;
; EXPAND.EXE — Clean-room CK-DOS 5.0 SZDD and KWAJ file decompressor.
;
; Supports:
;   - SZDD signature (53 5A 44 44 88 F0 27 33, mode 41h LZSS, missing-ext char)
;   - KWAJ signature (4B 57 41 4A 88 F0 27 D1, methods 0=copy, 1=XOR FFh, 2=LZSS,
;     optional uncompressed size and embedded filename/extension)
;   - Pass-through copy for uncompressed files
;   - Single file (EXPAND src dest), multiple files to directory, and /?

.8086

CODE    SEGMENT PARA PUBLIC 'CODE'
        ASSUME  CS:CODE, DS:CODE, ES:CODE

START:
        CLD
        MOV     AX,CODE
        MOV     ES,AX
        ; Copy command tail from PSP (DS:80h) into CS:CMD_BUF
        MOV     SI,0080h
        MOV     CL,[SI]
        XOR     CH,CH
        INC     SI
        MOV     DI,OFFSET CMD_BUF
        REP     MOVSB
        XOR     AL,AL
        STOSB

        PUSH    CS
        POP     DS

        CALL    PARSE_ARGS
        CMP     BYTE PTR [OPT_HELP],0
        JE      CHK_ARGC

        MOV     DX,OFFSET MSG_HELP
        CALL    PRINT_STR
        MOV     AX,4C00h
        INT     21h

CHK_ARGC:
        CMP     WORD PTR [ARG_COUNT],2
        JAE     ARGS_OK
        MOV     DX,OFFSET MSG_SYNTAX
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

ARGS_OK:
        ; Last argument is destination (file or directory)
        MOV     BX,[ARG_COUNT]
        DEC     BX
        MOV     [SRC_LIMIT],BX
        SHL     BX,1
        MOV     SI,[ARG_PTRS+BX]
        MOV     [DEST_ARG],SI
        CALL    CHECK_IS_DIR
        MOV     [DEST_IS_DIR],AL

        CMP     WORD PTR [SRC_LIMIT],1
        JBE     START_EXPAND_LOOP
        CMP     BYTE PTR [DEST_IS_DIR],0
        JNE     START_EXPAND_LOOP
        MOV     DX,OFFSET MSG_MULTI_NODIR
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

START_EXPAND_LOOP:
        MOV     WORD PTR [CUR_IDX],0
EXPAND_NEXT_SRC:
        MOV     BX,[CUR_IDX]
        CMP     BX,[SRC_LIMIT]
        JAE     ALL_DONE_OK
        SHL     BX,1
        MOV     SI,[ARG_PTRS+BX]
        MOV     [CUR_SRC],SI
        CALL    EXPAND_ONE_FILE
        JC      EXIT_ERR
        INC     WORD PTR [CUR_IDX]
        JMP     SHORT EXPAND_NEXT_SRC

ALL_DONE_OK:
        MOV     AX,4C00h
        INT     21h

EXIT_ERR:
        MOV     AX,4C01h
        INT     21h

; ---------------------------------------------------------------------------
; EXPAND_ONE_FILE: expands [CUR_SRC] to [DEST_ARG] (or dir + derived name)
; ---------------------------------------------------------------------------
EXPAND_ONE_FILE PROC    NEAR
        MOV     WORD PTR [IN_HANDLE],0FFFFh
        MOV     WORD PTR [OUT_HANDLE],0FFFFh

        MOV     DX,[CUR_SRC]
        MOV     AX,3D00h                ; open read-only
        INT     21h
        JNC     EOF_OPEN_OK
        MOV     DX,OFFSET MSG_OPEN_ERR
        CALL    PRINT_STR
        MOV     SI,[CUR_SRC]
        CALL    PRINT_ASCIIZ
        CALL    PRINT_CRLF
        STC
        RET

EOF_OPEN_OK:
        MOV     [IN_HANDLE],AX
        MOV     WORD PTR [IN_POS],0
        MOV     WORD PTR [IN_LEN],0
        MOV     BYTE PTR [IN_EOF],0
        MOV     WORD PTR [OUT_POS],0

        ; Read first 16 bytes (or file length if < 16) to inspect magic
        MOV     DI,OFFSET HDR_BUF
        MOV     CX,16
        XOR     BX,BX
EOF_HDR_LP:
        CALL    GET_BYTE
        JC      EOF_HDR_DONE
        MOV     [DI],AL
        INC     DI
        INC     BX
        LOOP    EOF_HDR_LP
EOF_HDR_DONE:
        MOV     [HDR_READ],BX

        ; Rewind input file to 0 so each format handler reads cleanly
        MOV     BX,[IN_HANDLE]
        XOR     CX,CX
        XOR     DX,DX
        MOV     AX,4200h
        INT     21h
        MOV     WORD PTR [IN_POS],0
        MOV     WORD PTR [IN_LEN],0
        MOV     BYTE PTR [IN_EOF],0

        ; Clear optional embedded name/missing char
        MOV     BYTE PTR [MISSING_EXT_CH],0
        MOV     BYTE PTR [KWAJ_NAME],0

        ; Check SZDD magic: 53 5A 44 44 88 F0 27 33
        CMP     WORD PTR [HDR_READ],14
        JB      EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+0],5A53h
        JNE     EOF_CHK_KWAJ
        CMP     WORD PTR [HDR_BUF+2],4444h
        JNE     EOF_CHK_KWAJ
        CMP     WORD PTR [HDR_BUF+4],0F088h
        JNE     EOF_CHK_KWAJ
        CMP     WORD PTR [HDR_BUF+6],3327h
        JNE     EOF_CHK_KWAJ
        JMP     HANDLE_SZDD

EOF_CHK_KWAJ:
        ; Check KWAJ magic: 4B 57 41 4A 88 F0 27 D1
        CMP     WORD PTR [HDR_BUF+0],574Bh
        JNE     EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+2],4A41h
        JNE     EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+4],0F088h
        JNE     EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+6],0D127h
        JNE     EOF_RAW_COPY
        JMP     HANDLE_KWAJ

; --- Uncompressed pass-through copy ---
EOF_RAW_COPY:
        CALL    BUILD_DEST_PATH
        CALL    OPEN_DEST_FILE
        JNC     RAW_LP
        JMP     EOF_FAIL_CLOSE
RAW_LP:
        CALL    GET_BYTE
        JNC     RAW_PUT
        JMP     EOF_FINISH_OK
RAW_PUT:
        CALL    PUT_BYTE
        JNC     RAW_LP
        JMP     EOF_WRITE_ERR

; --- SZDD Decompressor ---
HANDLE_SZDD:
        CMP     BYTE PTR [HDR_BUF+8],41h
        JE      SZ_MODE_OK
        JMP     EOF_BAD_FMT
SZ_MODE_OK:
        MOV     AL,[HDR_BUF+9]
        MOV     [MISSING_EXT_CH],AL
        MOV     AX,WORD PTR [HDR_BUF+10]
        MOV     [REM_OUT_LO],AX
        MOV     AX,WORD PTR [HDR_BUF+12]
        MOV     [REM_OUT_HI],AX
        MOV     BYTE PTR [HAS_OUT_LIMIT],1

        ; Skip 14-byte SZDD header
        MOV     CX,14
SZ_SKIP_HDR:
        CALL    GET_BYTE
        LOOP    SZ_SKIP_HDR

        CALL    BUILD_DEST_PATH
        CALL    OPEN_DEST_FILE
        JNC     SZ_DO_LZ
        JMP     EOF_FAIL_CLOSE
SZ_DO_LZ:
        MOV     WORD PTR [RING_INIT_POS],0FF0h  ; 4096 - 16
        CALL    DECOMPRESS_LZSS
        JNC     SZ_DONE_JMP
        JMP     EOF_WRITE_ERR
SZ_DONE_JMP:
        JMP     EOF_FINISH_OK

; --- KWAJ Decompressor ---
HANDLE_KWAJ:
        MOV     AX,WORD PTR [HDR_BUF+8]
        MOV     [KWAJ_METHOD],AX
        MOV     AX,WORD PTR [HDR_BUF+10]
        MOV     [KWAJ_DATA_OFF],AX
        MOV     AX,WORD PTR [HDR_BUF+12]
        MOV     [KWAJ_FLAGS],AX
        MOV     BYTE PTR [HAS_OUT_LIMIT],0

        ; Consume 14-byte base header
        MOV     CX,14
KW_SKIP_BASE:
        CALL    GET_BYTE
        LOOP    KW_SKIP_BASE

        ; Optional fields per KWAJ_FLAGS
        TEST    WORD PTR [KWAJ_FLAGS],0001h     ; 4-byte uncompressed length
        JZ      KW_FLG_2
        CALL    GET_BYTE
        MOV     BYTE PTR [REM_OUT_LO],AL
        CALL    GET_BYTE
        MOV     BYTE PTR [REM_OUT_LO+1],AL
        CALL    GET_BYTE
        MOV     BYTE PTR [REM_OUT_HI],AL
        CALL    GET_BYTE
        MOV     BYTE PTR [REM_OUT_HI+1],AL
        MOV     BYTE PTR [HAS_OUT_LIMIT],1

KW_FLG_2:
        TEST    WORD PTR [KWAJ_FLAGS],0002h     ; 2-byte reserved word
        JZ      KW_FLG_4
        CALL    GET_BYTE
        CALL    GET_BYTE

KW_FLG_4:
        TEST    WORD PTR [KWAJ_FLAGS],0004h     ; length-prefixed extra data
        JZ      KW_FLG_8
        CALL    GET_BYTE
        MOV     CL,AL
        CALL    GET_BYTE
        MOV     CH,AL
        JCXZ    KW_FLG_8
KW_SKIP_EXTRA:
        CALL    GET_BYTE
        LOOP    KW_SKIP_EXTRA

KW_FLG_8:
        MOV     DI,OFFSET KWAJ_NAME
        TEST    WORD PTR [KWAJ_FLAGS],0008h     ; 1..8 char filename
        JZ      KW_FLG_10
        MOV     CX,9
KW_RD_NAME:
        CALL    GET_BYTE
        JC      KW_FLG_10
        OR      AL,AL
        JZ      KW_FLG_10
        CMP     CX,1
        JBE     KW_RD_NAME
        MOV     [DI],AL
        INC     DI
        DEC     CX
        JMP     SHORT KW_RD_NAME

KW_FLG_10:
        TEST    WORD PTR [KWAJ_FLAGS],0010h     ; 1..3 char extension
        JZ      KW_SEEK_DATA
        CMP     DI,OFFSET KWAJ_NAME
        JE      KW_SKIP_EXT_ONLY
        MOV     BYTE PTR [DI],'.'
        INC     DI
        MOV     CX,4
KW_RD_EXT:
        CALL    GET_BYTE
        JC      KW_SEEK_DATA
        OR      AL,AL
        JZ      KW_SEEK_DATA
        CMP     CX,1
        JBE     KW_RD_EXT
        MOV     [DI],AL
        INC     DI
        DEC     CX
        JMP     SHORT KW_RD_EXT

KW_SKIP_EXT_ONLY:
        CALL    GET_BYTE
        JC      KW_SEEK_DATA
        OR      AL,AL
        JNZ     KW_SKIP_EXT_ONLY

KW_SEEK_DATA:
        MOV     BYTE PTR [DI],0
        ; Seek directly to KWAJ_DATA_OFF
        MOV     BX,[IN_HANDLE]
        XOR     CX,CX
        MOV     DX,[KWAJ_DATA_OFF]
        MOV     AX,4200h
        INT     21h
        MOV     WORD PTR [IN_POS],0
        MOV     WORD PTR [IN_LEN],0
        MOV     BYTE PTR [IN_EOF],0

        CALL    BUILD_DEST_PATH
        CALL    OPEN_DEST_FILE
        JC      EOF_FAIL_CLOSE

        MOV     AX,[KWAJ_METHOD]
        CMP     AX,0
        JE      KW_METH_COPY
        CMP     AX,1
        JE      KW_METH_XOR
        CMP     AX,2
        JE      KW_METH_LZSS
        JMP     SHORT EOF_BAD_FMT

KW_METH_COPY:
        CALL    CHECK_OUT_REMAINING
        JZ      EOF_FINISH_OK
        CALL    GET_BYTE
        JC      EOF_FINISH_OK
        CALL    EMIT_COUNTED_BYTE
        JC      EOF_WRITE_ERR
        JMP     SHORT KW_METH_COPY

KW_METH_XOR:
        CALL    CHECK_OUT_REMAINING
        JZ      EOF_FINISH_OK
        CALL    GET_BYTE
        JC      EOF_FINISH_OK
        XOR     AL,0FFh
        CALL    EMIT_COUNTED_BYTE
        JC      EOF_WRITE_ERR
        JMP     SHORT KW_METH_XOR

KW_METH_LZSS:
        MOV     WORD PTR [RING_INIT_POS],0FEFh  ; 4096 - 17 for KWAJ method 2
        CALL    DECOMPRESS_LZSS
        JC      EOF_WRITE_ERR

EOF_FINISH_OK:
        CALL    FLUSH_OUT
        JC      EOF_WRITE_ERR
        CALL    CLOSE_HANDLES
        ; Print "src -> dst"
        MOV     SI,[CUR_SRC]
        CALL    PRINT_ASCIIZ
        MOV     DX,OFFSET MSG_ARROW
        CALL    PRINT_STR
        MOV     SI,OFFSET DEST_PATH
        CALL    PRINT_ASCIIZ
        CALL    PRINT_CRLF
        CLC
        RET

EOF_BAD_FMT:
        MOV     DX,OFFSET MSG_BAD_FMT
        CALL    PRINT_STR
        JMP     SHORT EOF_FAIL_CLOSE

EOF_WRITE_ERR:
        MOV     DX,OFFSET MSG_WRITE_ERR
        CALL    PRINT_STR

EOF_FAIL_CLOSE:
        CALL    CLOSE_HANDLES
        STC
        RET
EXPAND_ONE_FILE ENDP

; ---------------------------------------------------------------------------
; DECOMPRESS_LZSS: 4096-byte ring buffer LZSS (SZDD & KWAJ method 2)
; ---------------------------------------------------------------------------
DECOMPRESS_LZSS PROC    NEAR
        ; Fill RING_BUF (4096 bytes) with 20h (' ')
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET RING_BUF
        MOV     CX,2048
        MOV     AX,2020h
        REP     STOSW

        MOV     SI,[RING_INIT_POS]      ; SI = ring write index r (0..4095)

LZ_CTRL_LOOP:
        CALL    CHECK_OUT_REMAINING
        JZ      LZ_DONE
        CALL    GET_BYTE
        JC      LZ_DONE
        MOV     BL,AL                   ; BL = 8 flag bits
        MOV     BH,8

LZ_BIT_LOOP:
        CALL    CHECK_OUT_REMAINING
        JZ      LZ_DONE
        SHR     BL,1
        JNC     LZ_MATCH

        ; Literal byte
        CALL    GET_BYTE
        JC      LZ_DONE
        MOV     [RING_BUF+SI],AL
        INC     SI
        AND     SI,0FFFh
        CALL    EMIT_COUNTED_BYTE
        JC      LZ_ERR
        JMP     SHORT LZ_NEXT_BIT

LZ_MATCH:
        CALL    GET_BYTE
        JC      LZ_DONE
        MOV     DL,AL                   ; b1
        CALL    GET_BYTE
        JC      LZ_DONE
        MOV     DH,AL                   ; b2
        ; pos = b1 | ((b2 & 0F0h) << 4)
        MOV     AH,DH
        MOV     CL,4
        SHR     AH,CL
        MOV     AL,DL
        MOV     DI,AX                   ; DI = match pos (0..4095)
        MOV     CL,DH
        AND     CL,0Fh
        ADD     CL,3
        XOR     CH,CH                   ; CX = match length (3..18)

LZ_COPY_MATCH:
        CALL    CHECK_OUT_REMAINING
        JZ      LZ_DONE
        MOV     AL,[RING_BUF+DI]
        INC     DI
        AND     DI,0FFFh
        MOV     [RING_BUF+SI],AL
        INC     SI
        AND     SI,0FFFh
        PUSH    CX
        PUSH    DI
        CALL    EMIT_COUNTED_BYTE
        POP     DI
        POP     CX
        JC      LZ_ERR
        LOOP    LZ_COPY_MATCH

LZ_NEXT_BIT:
        DEC     BH
        JNZ     LZ_BIT_LOOP
        JMP     LZ_CTRL_LOOP

LZ_DONE:
        CLC
        RET
LZ_ERR:
        STC
        RET
DECOMPRESS_LZSS ENDP

; Returns ZF=1 when HAS_OUT_LIMIT != 0 and REM_OUT == 0; else ZF=0
CHECK_OUT_REMAINING PROC NEAR
        CMP     BYTE PTR [HAS_OUT_LIMIT],0
        JE      COR_MORE
        MOV     AX,[REM_OUT_LO]
        OR      AX,[REM_OUT_HI]
        RET
COR_MORE:
        OR      SP,SP                   ; clear ZF
        RET
CHECK_OUT_REMAINING ENDP

EMIT_COUNTED_BYTE PROC  NEAR
        CMP     BYTE PTR [HAS_OUT_LIMIT],0
        JE      ECB_PUT
        SUB     WORD PTR [REM_OUT_LO],1
        SBB     WORD PTR [REM_OUT_HI],0
ECB_PUT:
        CALL    PUT_BYTE
        RET
EMIT_COUNTED_BYTE ENDP

; ---------------------------------------------------------------------------
; Buffered File I/O (8 KB input buffer, 8 KB output buffer)
; ---------------------------------------------------------------------------
GET_BYTE        PROC    NEAR
        PUSH    BX
        MOV     BX,[IN_POS]
        CMP     BX,[IN_LEN]
        JB      GB_HAVE
        CMP     BYTE PTR [IN_EOF],0
        JNE     GB_EOF
        PUSH    CX
        PUSH    DX
        MOV     BX,[IN_HANDLE]
        MOV     DX,OFFSET IN_BUF
        MOV     CX,4096
        MOV     AH,3Fh
        INT     21h
        POP     DX
        POP     CX
        JC      GB_EOF
        OR      AX,AX
        JZ      GB_SET_EOF
        MOV     [IN_LEN],AX
        XOR     BX,BX
        MOV     [IN_POS],BX
        JMP     SHORT GB_HAVE
GB_SET_EOF:
        MOV     BYTE PTR [IN_EOF],1
GB_EOF:
        POP     BX
        STC
        RET
GB_HAVE:
        MOV     AL,[IN_BUF+BX]
        INC     BX
        MOV     [IN_POS],BX
        POP     BX
        CLC
        RET
GET_BYTE        ENDP

PUT_BYTE        PROC    NEAR
        PUSH    BX
        MOV     BX,[OUT_POS]
        MOV     [OUT_BUF+BX],AL
        INC     BX
        MOV     [OUT_POS],BX
        CMP     BX,4096
        POP     BX
        JB      PB_OK
        CALL    FLUSH_OUT
        RET
PB_OK:
        CLC
        RET
PUT_BYTE        ENDP

FLUSH_OUT       PROC    NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     CX,[OUT_POS]
        JCXZ    FO_OK
        MOV     BX,[OUT_HANDLE]
        MOV     DX,OFFSET OUT_BUF
        MOV     AH,40h
        INT     21h
        JC      FO_ERR
        CMP     AX,CX
        JNE     FO_ERR
        MOV     WORD PTR [OUT_POS],0
FO_OK:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        CLC
        RET
FO_ERR:
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        STC
        RET
FLUSH_OUT       ENDP

OPEN_DEST_FILE  PROC    NEAR
        MOV     DX,OFFSET DEST_PATH
        XOR     CX,CX
        MOV     AH,3Ch
        INT     21h
        MOV     [OUT_HANDLE],AX
        RET
OPEN_DEST_FILE  ENDP

CLOSE_HANDLES   PROC    NEAR
        MOV     BX,[OUT_HANDLE]
        CMP     BX,0FFFFh
        JE      CH_IN
        MOV     AH,3Eh
        INT     21h
        MOV     WORD PTR [OUT_HANDLE],0FFFFh
CH_IN:
        MOV     BX,[IN_HANDLE]
        CMP     BX,0FFFFh
        JE      CH_DONE
        MOV     AH,3Eh
        INT     21h
        MOV     WORD PTR [IN_HANDLE],0FFFFh
CH_DONE:
        RET
CLOSE_HANDLES   ENDP

; ---------------------------------------------------------------------------
; Destination Path Builder
; ---------------------------------------------------------------------------
BUILD_DEST_PATH PROC    NEAR
        PUSH    SI
        PUSH    DI
        MOV     SI,[DEST_ARG]
        MOV     DI,OFFSET DEST_PATH
        CMP     BYTE PTR [DEST_IS_DIR],0
        JNE     BDP_DIR
BDP_CP_DIRECT:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     BDP_CP_DIRECT
        POP     DI
        POP     SI
        RET

BDP_DIR:
        LODSB
        OR      AL,AL
        JZ      BDP_ADD_SEP
        STOSB
        JMP     SHORT BDP_DIR
BDP_ADD_SEP:
        CMP     DI,OFFSET DEST_PATH
        JE      BDP_PICK_NAME
        MOV     AL,[DI-1]
        CMP     AL,'\'
        JE      BDP_PICK_NAME
        CMP     AL,'/'
        JE      BDP_PICK_NAME
        CMP     AL,':'
        JE      BDP_PICK_NAME
        MOV     AL,'\'
        STOSB

BDP_PICK_NAME:
        CMP     BYTE PTR [KWAJ_NAME],0
        JE      BDP_FROM_SRC
        MOV     SI,OFFSET KWAJ_NAME
BDP_KW_CP:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     BDP_KW_CP
        POP     DI
        POP     SI
        RET

BDP_FROM_SRC:
        ; Find basename of CUR_SRC
        MOV     SI,[CUR_SRC]
        MOV     BX,SI
BDP_SCAN_BASE:
        LODSB
        OR      AL,AL
        JZ      BDP_GOT_BASE
        CMP     AL,'\'
        JE      BDP_SET_BASE
        CMP     AL,'/'
        JE      BDP_SET_BASE
        CMP     AL,':'
        JNE     BDP_SCAN_BASE
BDP_SET_BASE:
        MOV     BX,SI
        JMP     SHORT BDP_SCAN_BASE
BDP_GOT_BASE:
        MOV     SI,BX
BDP_BASE_CP:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     BDP_BASE_CP
        ; If MISSING_EXT_CH != 0 and DEST_PATH ends with '_', replace '_'!
        MOV     AL,[MISSING_EXT_CH]
        OR      AL,AL
        JZ      BDP_DONE
        CMP     BYTE PTR [DI-2],'_'
        JNE     BDP_DONE
        MOV     [DI-2],AL
BDP_DONE:
        POP     DI
        POP     SI
        RET
BUILD_DEST_PATH ENDP

; Input: DS:SI -> path -> Output: AL=1 if existing directory or ends in \ / :, else 0
CHECK_IS_DIR    PROC    NEAR
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     DX,SI
        XOR     CX,CX
CID_END:
        LODSB
        OR      AL,AL
        JZ      CID_AT_END
        MOV     CL,AL
        JMP     SHORT CID_END
CID_AT_END:
        CMP     CL,'\'
        JE      CID_YES
        CMP     CL,'/'
        JE      CID_YES
        CMP     CL,':'
        JE      CID_YES
        MOV     AX,4300h
        INT     21h
        JC      CID_NO
        TEST    CX,10h
        JNZ     CID_YES
CID_NO:
        XOR     AL,AL
        JMP     SHORT CID_RET
CID_YES:
        MOV     AL,1
CID_RET:
        POP     SI
        POP     DX
        POP     CX
        RET
CHECK_IS_DIR    ENDP

; ---------------------------------------------------------------------------
; Argument Parser & Console Output
; ---------------------------------------------------------------------------
PARSE_ARGS      PROC    NEAR
        MOV     WORD PTR [ARG_COUNT],0
        MOV     BYTE PTR [OPT_HELP],0
        MOV     SI,OFFSET CMD_BUF
PA_SKIP:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PA_DONE
        CMP     AL,0Dh
        JE      PA_DONE
        CMP     AL,' '
        JE      PA_ADV
        CMP     AL,9
        JE      PA_ADV
        CMP     AL,'/'
        JNE     PA_TOK
        CMP     BYTE PTR [SI+1],'?'
        JNE     PA_SKIP_SW
        MOV     BYTE PTR [OPT_HELP],1
        RET
PA_SKIP_SW:
        INC     SI
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PA_DONE
        CMP     AL,0Dh
        JE      PA_DONE
        CMP     AL,' '
        JE      PA_SKIP
        CMP     AL,9
        JE      PA_SKIP
        JMP     SHORT PA_SKIP_SW
PA_ADV:
        INC     SI
        JMP     SHORT PA_SKIP

PA_TOK:
        MOV     BX,[ARG_COUNT]
        CMP     BX,32
        JAE     PA_DONE
        SHL     BX,1
        MOV     [ARG_PTRS+BX],SI
        INC     WORD PTR [ARG_COUNT]
PA_TOK_CH:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PA_DONE
        CMP     AL,0Dh
        JE      PA_TERM
        CMP     AL,' '
        JE      PA_TERM
        CMP     AL,9
        JE      PA_TERM
        INC     SI
        JMP     SHORT PA_TOK_CH
PA_TERM:
        MOV     BYTE PTR [SI],0
        INC     SI
        JMP     SHORT PA_SKIP
PA_DONE:
        RET
PARSE_ARGS      ENDP

PRINT_STR       PROC    NEAR
        MOV     AH,09h
        INT     21h
        RET
PRINT_STR       ENDP

PRINT_ASCIIZ    PROC    NEAR
PAZ_LP:
        LODSB
        OR      AL,AL
        JZ      PAZ_RET
        MOV     DL,AL
        MOV     AH,02h
        INT     21h
        JMP     SHORT PAZ_LP
PAZ_RET:
        RET
PRINT_ASCIIZ    ENDP

PRINT_CRLF      PROC    NEAR
        MOV     DX,OFFSET MSG_CRLF
        CALL    PRINT_STR
        RET
PRINT_CRLF      ENDP

; ---------------------------------------------------------------------------
; Messages and Buffers
; ---------------------------------------------------------------------------
MSG_HELP        DB      'Expands one or more compressed files.',13,10,13,10
                DB      'EXPAND [drive:][path]filename [[drive:][path]filename[ ...]] destination',13,10,'$'
MSG_SYNTAX      DB      'Required parameter missing. Type EXPAND /? for help.',13,10,'$'
MSG_MULTI_NODIR DB      'Destination must be a directory when expanding multiple files.',13,10,'$'
MSG_OPEN_ERR    DB      'Cannot open input file: $'
MSG_WRITE_ERR   DB      'Error writing destination file.',13,10,'$'
MSG_BAD_FMT     DB      'Unrecognized compression method in input file.',13,10,'$'
MSG_ARROW       DB      ' -> $'
MSG_CRLF        DB      13,10,'$'

OPT_HELP        DB      0
DEST_IS_DIR     DB      0
IN_EOF          DB      0
HAS_OUT_LIMIT   DB      0
MISSING_EXT_CH  DB      0

ARG_COUNT       DW      0
SRC_LIMIT       DW      0
CUR_IDX         DW      0
CUR_SRC         DW      0
DEST_ARG        DW      0
IN_HANDLE       DW      0FFFFh
OUT_HANDLE      DW      0FFFFh
IN_POS          DW      0
IN_LEN          DW      0
OUT_POS         DW      0
HDR_READ        DW      0
RING_INIT_POS   DW      0FF0h
REM_OUT_LO      DW      0
REM_OUT_HI      DW      0
KWAJ_METHOD     DW      0
KWAJ_DATA_OFF   DW      0
KWAJ_FLAGS      DW      0

ARG_PTRS        DW      32 DUP (0)
CMD_BUF         DB      132 DUP (0)
HDR_BUF         DB      20 DUP (0)
KWAJ_NAME       DB      16 DUP (0)
DEST_PATH       DB      132 DUP (0)

RING_BUF        DB      4096 DUP (0)
IN_BUF          DB      4096 DUP (0)
OUT_BUF         DB      4096 DUP (0)

CODE    ENDS

STACK   SEGMENT PARA STACK 'STACK'
        DB      512 DUP (?)
STACK   ENDS

        END     START
