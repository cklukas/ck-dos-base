; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; EXPAND.EXE — Clean-room CK-DOS 5.0 SZDD (mode 41h LZSS) file decompressor.
;
; Supports:
;   - SZDD signature (53 5A 44 44 88 F0 27 33, mode 41h LZSS, 4-byte length)
;   - Pass-through copy for uncompressed files (with status diagnostic)
;   - Packed DOS file date/time preservation (INT 21h AX=5700h / 5701h)
;   - Canonicalized self-overwrite protection (INT 21h AH=60h)
;   - Partial-output cleanup on write / disk-full failure
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

SHOW_HELP_ERR:
        MOV     SI,OFFSET MSG_HELP
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C01h
        INT     21h

CHK_ARGC:
        CMP     WORD PTR [ARG_COUNT],0
        JE      SHOW_HELP_ERR
        CMP     WORD PTR [ARG_COUNT],1
        JA      ARGS_TWO_OR_MORE

        ; Single argument: if not an existing directory, report missing dest dir
        MOV     SI,[ARG_PTRS]
        CALL    CHECK_IS_DIR
        OR      AL,AL
        JNZ     SHOW_HELP_ERR
        MOV     SI,OFFSET MSG_NODIR_PFX
        CALL    WRITE_STDOUT_Z
        MOV     SI,[ARG_PTRS]
        CALL    WRITE_STDOUT_Z
        CALL    WRITE_CRLF
        MOV     AX,4C01h
        INT     21h

ARGS_TWO_OR_MORE:
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
        MOV     SI,OFFSET MSG_NODIR_PFX
        CALL    WRITE_STDOUT_Z
        MOV     SI,[DEST_ARG]
        CALL    WRITE_STDOUT_Z
        CALL    WRITE_CRLF
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
; EXPAND_ONE_FILE: expands [CUR_SRC] to [DEST_ARG] (or dir + original basename)
; ---------------------------------------------------------------------------
EXPAND_ONE_FILE PROC    NEAR
        MOV     WORD PTR [IN_HANDLE],0FFFFh
        MOV     WORD PTR [OUT_HANDLE],0FFFFh
        MOV     BYTE PTR [HAS_SRC_TS],0

        ; Reject wildcards explicitly (DOS 5 EXPAND does not expand wildcards)
        MOV     SI,[CUR_SRC]
        CALL    HAS_WILDCARD
        JC      EOF_OPEN_FAIL

        MOV     DX,[CUR_SRC]
        MOV     AX,3D00h                ; open read-only
        INT     21h
        JNC     EOF_OPEN_OK

EOF_OPEN_FAIL:
        MOV     SI,OFFSET MSG_OPEN_IN
        CALL    WRITE_STDOUT_Z
        MOV     SI,[CUR_SRC]
        CALL    WRITE_STDOUT_Z
        CALL    WRITE_CRLF
        STC
        RET

EOF_OPEN_OK:
        MOV     [IN_HANDLE],AX
        MOV     BX,AX
        MOV     AX,5700h                ; get file's last-written time & date
        INT     21h
        JC      EOF_NO_TS
        MOV     [SRC_TIME],CX
        MOV     [SRC_DATE],DX
        MOV     BYTE PTR [HAS_SRC_TS],1
EOF_NO_TS:
        CALL    BUILD_DEST_PATH

        ; Prevent truncating source file onto itself before AH=3Ch create
        CALL    CHECK_SAME_FILE
        JNC     EOF_NOT_SAME
        CALL    CLOSE_HANDLES
        MOV     SI,OFFSET MSG_SELF_COPY
        CALL    WRITE_STDOUT_Z
        MOV     SI,[CUR_SRC]
        CALL    WRITE_STDOUT_Z
        CALL    WRITE_CRLF
        STC
        RET

EOF_NOT_SAME:
        MOV     WORD PTR [IN_POS],0
        MOV     WORD PTR [IN_LEN],0
        MOV     BYTE PTR [IN_EOF],0
        MOV     WORD PTR [OUT_POS],0

        ; Read first 14 bytes (or file length if < 14) to inspect SZDD header
        MOV     DI,OFFSET HDR_BUF
        MOV     CX,14
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

        ; Rewind input file to 0 so format handler reads cleanly
        MOV     BX,[IN_HANDLE]
        XOR     CX,CX
        XOR     DX,DX
        MOV     AX,4200h
        INT     21h
        MOV     WORD PTR [IN_POS],0
        MOV     WORD PTR [IN_LEN],0
        MOV     BYTE PTR [IN_EOF],0

        ; Check 9-byte SZDD mode-41h header: 53 5A 44 44 88 F0 27 33 41
        CMP     WORD PTR [HDR_READ],14
        JB      EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+0],5A53h
        JNE     EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+2],4444h
        JNE     EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+4],0F088h
        JNE     EOF_RAW_COPY
        CMP     WORD PTR [HDR_BUF+6],3327h
        JNE     EOF_RAW_COPY
        CMP     BYTE PTR [HDR_BUF+8],41h
        JNE     EOF_RAW_COPY
        JMP     SHORT HANDLE_SZDD

; --- Uncompressed pass-through copy ---
EOF_RAW_COPY:
        CALL    OPEN_DEST_FILE
        JNC     RAW_ANNOUNCE
        JMP     EOF_OPEN_OUT_FAIL
RAW_ANNOUNCE:
        MOV     SI,OFFSET MSG_ALREADY_1
        CALL    WRITE_STDOUT_Z
        MOV     SI,[CUR_SRC]
        CALL    WRITE_STDOUT_Z
        MOV     SI,OFFSET MSG_ALREADY_2
        CALL    WRITE_STDOUT_Z
RAW_LP:
        CALL    GET_BYTE
        JC      EOF_FINISH_OK
        CALL    PUT_BYTE
        JNC     RAW_LP
        JMP     SHORT EOF_WRITE_ERR

; --- SZDD Mode 41h Decompressor ---
HANDLE_SZDD:
        MOV     AX,WORD PTR [HDR_BUF+10]
        MOV     [REM_OUT_LO],AX
        MOV     AX,WORD PTR [HDR_BUF+12]
        MOV     [REM_OUT_HI],AX
        MOV     BYTE PTR [HAS_OUT_LIMIT],1

        CALL    OPEN_DEST_FILE
        JNC     SZ_SKIP_HDR_INIT
        JMP     SHORT EOF_OPEN_OUT_FAIL

SZ_SKIP_HDR_INIT:
        ; Consume 14-byte SZDD header
        MOV     CX,14
SZ_SKIP_HDR:
        CALL    GET_BYTE
        LOOP    SZ_SKIP_HDR

        MOV     WORD PTR [RING_INIT_POS],0FF0h  ; 4096 - 16
        CALL    DECOMPRESS_LZSS
        JC      EOF_WRITE_ERR

EOF_FINISH_OK:
        CALL    FLUSH_OUT
        JC      EOF_WRITE_ERR
        CMP     BYTE PTR [HAS_SRC_TS],0
        JE      EOF_CLOSE_OK
        MOV     BX,[OUT_HANDLE]
        MOV     CX,[SRC_TIME]
        MOV     DX,[SRC_DATE]
        MOV     AX,5701h                ; preserve source timestamp
        INT     21h
EOF_CLOSE_OK:
        CALL    CLOSE_HANDLES
        CLC
        RET

EOF_OPEN_OUT_FAIL:
        CALL    CLOSE_HANDLES
        MOV     SI,OFFSET MSG_OPEN_OUT
        CALL    WRITE_STDOUT_Z
        MOV     SI,OFFSET DEST_PATH
        CALL    WRITE_STDOUT_Z
        CALL    WRITE_CRLF
        STC
        RET

EOF_WRITE_ERR:
        CALL    CLOSE_HANDLES
        ; Remove partial destination file on write / disk-full failure
        MOV     DX,OFFSET DEST_PATH
        MOV     AH,41h
        INT     21h
        MOV     SI,OFFSET MSG_DISK_FULL
        CALL    WRITE_STDOUT_Z
        MOV     SI,OFFSET DEST_PATH
        CALL    WRITE_STDOUT_Z
        CALL    WRITE_CRLF
        STC
        RET
EXPAND_ONE_FILE ENDP

; ---------------------------------------------------------------------------
; DECOMPRESS_LZSS: 4096-byte ring buffer LZSS (SZDD mode 41h)
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
        MOV     CX,8192
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
        CMP     BX,8192
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
        MOV     SI,OFFSET DEST_PATH
        CALL    HAS_WILDCARD
        JC      ODF_FAIL
        MOV     DX,OFFSET DEST_PATH
        XOR     CX,CX
        MOV     AH,3Ch
        INT     21h
        JC      ODF_FAIL
        MOV     [OUT_HANDLE],AX
        CLC
        RET
ODF_FAIL:
        STC
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
; Destination Path Builder & Self-Copy Protection
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
        JE      BDP_FROM_SRC
        MOV     AL,[DI-1]
        CMP     AL,'\'
        JE      BDP_FROM_SRC
        CMP     AL,'/'
        JE      BDP_FROM_SRC
        CMP     AL,':'
        JE      BDP_FROM_SRC
        MOV     AL,'\'
        STOSB

BDP_FROM_SRC:
        ; Keep original source basename (MS-DOS 5.00 EXPAND behavior)
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
        POP     DI
        POP     SI
        RET
BUILD_DEST_PATH ENDP

; Sets CF=1 if CUR_SRC and DEST_PATH resolve to the same file, else CF=0
CHECK_SAME_FILE PROC    NEAR
        PUSH    SI
        PUSH    DI
        PUSH    CS
        POP     ES
        MOV     SI,[CUR_SRC]
        MOV     DI,OFFSET TRUE_SRC
        MOV     AH,60h
        INT     21h
        JC      CSF_RAW
        MOV     SI,OFFSET DEST_PATH
        MOV     DI,OFFSET TRUE_DST
        MOV     AH,60h
        INT     21h
        JC      CSF_RAW
        MOV     SI,OFFSET TRUE_SRC
        MOV     DI,OFFSET TRUE_DST
        JMP     SHORT CSF_CMP
CSF_RAW:
        MOV     SI,[CUR_SRC]
        MOV     DI,OFFSET DEST_PATH
CSF_CMP:
        CALL    STRICMP_Z
        JZ      CSF_SAME
        CLC
        JMP     SHORT CSF_RET
CSF_SAME:
        STC
CSF_RET:
        POP     DI
        POP     SI
        RET
CHECK_SAME_FILE ENDP

; ZF=1 if ASCIIZ strings at DS:SI and DS:DI match case-insensitively
STRICMP_Z       PROC    NEAR
SCZ_LP:
        MOV     AL,[SI]
        MOV     AH,[DI]
        INC     SI
        INC     DI
        CMP     AL,'a'
        JB      SCZ_U1
        CMP     AL,'z'
        JA      SCZ_U1
        SUB     AL,20h
SCZ_U1:
        CMP     AH,'a'
        JB      SCZ_U2
        CMP     AH,'z'
        JA      SCZ_U2
        SUB     AH,20h
SCZ_U2:
        CMP     AL,'/'
        JNE     SCZ_S1
        MOV     AL,'\'
SCZ_S1:
        CMP     AH,'/'
        JNE     SCZ_S2
        MOV     AH,'\'
SCZ_S2:
        CMP     AL,AH
        JNE     SCZ_RET
        OR      AL,AL
        JNZ     SCZ_LP
SCZ_RET:
        RET
STRICMP_Z       ENDP

; Sets CF=1 if DS:SI contains '*' or '?', else CF=0
HAS_WILDCARD    PROC    NEAR
        PUSH    SI
HW_LP:
        LODSB
        OR      AL,AL
        JZ      HW_NO
        CMP     AL,'*'
        JE      HW_YES
        CMP     AL,'?'
        JE      HW_YES
        JMP     SHORT HW_LP
HW_YES:
        POP     SI
        STC
        RET
HW_NO:
        POP     SI
        CLC
        RET
HAS_WILDCARD    ENDP

; Input: DS:SI -> path -> Output: AL=1 if existing directory / valid drive root, else 0
CHECK_IS_DIR    PROC    NEAR
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    CS
        POP     ES

        CALL    HAS_WILDCARD
        JNC     CID_COPY_INIT
        JMP     CID_NO

CID_COPY_INIT:
        ; Copy path to TMP_DIR_BUF and compute length in CX
        MOV     DI,OFFSET TMP_DIR_BUF
        XOR     CX,CX
CID_CP:
        LODSB
        STOSB
        OR      AL,AL
        JZ      CID_CP_DONE
        INC     CX
        CMP     CX,126
        JB      CID_CP
        MOV     BYTE PTR [DI],0
CID_CP_DONE:
        OR      CX,CX
        JNZ     CID_CHK_ROOT
        JMP     CID_NO

CID_CHK_ROOT:
        ; Check "\" or "/" (root of current drive)
        CMP     CX,1
        JNE     CID_CHK_DRV
        MOV     AL,[TMP_DIR_BUF]
        CMP     AL,'\'
        JNE     CID_CHK_ROOT_SL
        JMP     CID_YES
CID_CHK_ROOT_SL:
        CMP     AL,'/'
        JNE     CID_CHK_DRV
        JMP     CID_YES

CID_CHK_DRV:
        ; Check "X:" or "X:\" or "X:/"
        CMP     BYTE PTR [TMP_DIR_BUF+1],':'
        JNE     CID_STRIP_SEP
        CMP     CX,2
        JE      CID_TEST_DRV
        CMP     CX,3
        JNE     CID_STRIP_SEP
        MOV     AL,[TMP_DIR_BUF+2]
        CMP     AL,'\'
        JE      CID_TEST_DRV
        CMP     AL,'/'
        JNE     CID_STRIP_SEP

CID_TEST_DRV:
        MOV     AL,[TMP_DIR_BUF]
        CMP     AL,'a'
        JB      CID_DRV_UP
        CMP     AL,'z'
        JA      CID_DRV_UP
        SUB     AL,20h
CID_DRV_UP:
        CMP     AL,'A'
        JB      CID_NO
        CMP     AL,'Z'
        JA      CID_NO
        SUB     AL,'A'-1
        MOV     DL,AL
        MOV     AH,36h                  ; Get Disk Free Space (validates drive)
        INT     21h
        CMP     AX,0FFFFh
        JE      CID_NO
        JMP     SHORT CID_YES

CID_STRIP_SEP:
        ; Strip trailing '\' or '/' on non-root path before AX=4300h
        MOV     BX,CX
        DEC     BX
        MOV     AL,[TMP_DIR_BUF+BX]
        CMP     AL,'\'
        JE      CID_DO_STRIP
        CMP     AL,'/'
        JNE     CID_ATTR
CID_DO_STRIP:
        MOV     BYTE PTR [TMP_DIR_BUF+BX],0

CID_ATTR:
        MOV     DX,OFFSET TMP_DIR_BUF
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
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        RET
CHECK_IS_DIR    ENDP

; ---------------------------------------------------------------------------
; Argument Parser & Handle-1 Output
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
        JNE     PA_TOK
        MOV     BYTE PTR [OPT_HELP],1
        RET
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
        CMP     AL,'/'
        JNE     PA_TOK_NEXT
        CMP     BYTE PTR [SI+1],'?'
        JNE     PA_TOK_NEXT
        MOV     BYTE PTR [OPT_HELP],1
        RET
PA_TOK_NEXT:
        INC     SI
        JMP     SHORT PA_TOK_CH
PA_TERM:
        MOV     BYTE PTR [SI],0
        INC     SI
        JMP     SHORT PA_SKIP
PA_DONE:
        RET
PARSE_ARGS      ENDP

; Writes NUL-terminated string at DS:SI to stdout (handle 1)
WRITE_STDOUT_Z  PROC    NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     DX,SI
        XOR     CX,CX
WSZ_LEN:
        LODSB
        OR      AL,AL
        JZ      WSZ_DO
        INC     CX
        JMP     SHORT WSZ_LEN
WSZ_DO:
        JCXZ    WSZ_DONE
        MOV     BX,1
        MOV     AH,40h
        INT     21h
WSZ_DONE:
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
WRITE_STDOUT_Z  ENDP

WRITE_CRLF      PROC    NEAR
        MOV     SI,OFFSET MSG_CRLF
        CALL    WRITE_STDOUT_Z
        RET
WRITE_CRLF      ENDP

; ---------------------------------------------------------------------------
; Messages and Buffers
; ---------------------------------------------------------------------------
MSG_HELP        DB      'Expands one or more compressed files.',13,10,13,10
                DB      'EXPAND [drive:][path]filename [[drive1:][path1]filename[ ...]] destination',13,10,13,10
                DB      '  [drive:][path]filename  Compressed source file(s) to expand (no wildcards).',13,10
                DB      '  destination             Target file (single source) or existing directory.',13,10,0
MSG_NODIR_PFX   DB      'Destination directory does not exist: ',0
MSG_OPEN_IN     DB      'Can',27h,'t open input file: ',0
MSG_OPEN_OUT    DB      'Can',27h,'t open output file: ',0
MSG_ALREADY_1   DB      'Input file ',27h,0
MSG_ALREADY_2   DB      27h,' already in expanded format',13,10,0
MSG_DISK_FULL   DB      'Out of disk space while writing output file: ',0
MSG_SELF_COPY   DB      'File cannot be copied onto itself: ',0
MSG_CRLF        DB      13,10,0

OPT_HELP        DB      0
DEST_IS_DIR     DB      0
IN_EOF          DB      0
HAS_OUT_LIMIT   DB      0
HAS_SRC_TS      DB      0

ARG_COUNT       DW      0
SRC_LIMIT       DW      0
CUR_IDX         DW      0
CUR_SRC         DW      0
DEST_ARG        DW      0
IN_HANDLE       DW      0FFFFh
OUT_HANDLE      DW      0FFFFh
SRC_TIME        DW      0
SRC_DATE        DW      0
IN_POS          DW      0
IN_LEN          DW      0
OUT_POS         DW      0
HDR_READ        DW      0
RING_INIT_POS   DW      0FF0h
REM_OUT_LO      DW      0
REM_OUT_HI      DW      0

ARG_PTRS        DW      32 DUP (0)
CMD_BUF         DB      132 DUP (0)
HDR_BUF         DB      20 DUP (0)
DEST_PATH       DB      132 DUP (0)
TMP_DIR_BUF     DB      132 DUP (0)
TRUE_SRC        DB      132 DUP (0)
TRUE_DST        DB      132 DUP (0)

RING_BUF        DB      4096 DUP (0)
IN_BUF          DB      8192 DUP (0)
OUT_BUF         DB      8192 DUP (0)

CODE    ENDS

STACK   SEGMENT PARA STACK 'STACK'
        DB      512 DUP (?)
STACK   ENDS

        END     START
