; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; SETVER.EXE — Clean-room CK-DOS 5.0 dual-mode version table driver & utility.
;
; Modes of operation:
;   1. Hosted CK-DOS Mode (INT 2Fh AX=D700h BX=434Bh detected):
;      Invokes CK-DOS Extension API service 0005h (AX=D701h BX=0005h) to list,
;      add/update, or delete per-program reported-version overrides in both the
;      live kernel and persistent session profile with zero resident memory.
;   2. Boot / Standalone DOS Mode (CONFIG.SYS DEVICE=SETVER.EXE & disk table):
;      Character device 'SETVERXX' ('CKSV') installs CS:EMB_TABLE into DOS 5+
;      SysInitVars+34h (SYSI_SETVER_TAB); CLI updates live SYSI_SETVER_TAB and
;      persists changes to SETVER.EXE on disk, failing cleanly (exit 6) if
;      neither the CK-DOS service nor a valid SETVER.EXE table is present.

.8086

CODE    SEGMENT PARA PUBLIC 'CODE'
        ASSUME  CS:CODE, DS:CODE

; ---------------------------------------------------------------------------
; Offset 0000h: Standard DOS character device header (used by AX=4B03h load)
; ---------------------------------------------------------------------------
DEV_NEXT        DD      -1
DEV_ATTR        DW      8000h
DEV_STRAT       DW      OFFSET DEV_STRATEGY
DEV_INTR        DW      OFFSET DEV_INTERRUPT
DEV_NAME        DB      'SETVERXX'

SIG_MAGIC       DB      'CKSV'
EMB_CAPACITY    DW      1024

EMB_TABLE       LABEL   BYTE
                DB      10,'WIN200.BIN',3,40
                DB      12,'IBMCACHE.SYS',3,40
                DB      0
                DB      (1024 - ($ - EMB_TABLE)) DUP (0)

REQ_PTR         DD      0

DEV_STRATEGY    PROC    FAR
        MOV     WORD PTR CS:[REQ_PTR],BX
        MOV     WORD PTR CS:[REQ_PTR+2],ES
        RET
DEV_STRATEGY    ENDP

DEV_INTERRUPT   PROC    FAR
        PUSH    AX
        PUSH    BX
        PUSH    DX
        PUSH    DI
        PUSH    DS
        PUSH    ES

        LES     DI,CS:[REQ_PTR]
        CMP     BYTE PTR ES:[DI+2],0    ; INIT command?
        JNE     DEV_DONE

        ; Set break address immediately after RESIDENT_END
        MOV     WORD PTR ES:[DI+14],OFFSET RESIDENT_END
        MOV     WORD PTR ES:[DI+16],CS

        ; Point DOS 5+ SETVER table pointer at CS:EMB_TABLE:
        ;   - Booted CK-DOS 5.0 stores SYSI_SETVER_TAB at ES:[BX+34h] (pre-pointed into DOSGROUP = ES)
        ;   - Retail MS-DOS 5.00 stores SETVER pointer at ES:[BX+37h] (after 3B SPLICE/special fields)
        PUSH    ES
        PUSH    DI
        MOV     AH,30h
        INT     21h
        CMP     AL,5
        JB      DEV_NO_SYSVARS
        MOV     AH,52h
        INT     21h
        MOV     AX,ES
        CMP     WORD PTR ES:[BX+36h],AX
        JE      DEV_USE_34
        MOV     WORD PTR ES:[BX+37h],OFFSET EMB_TABLE
        MOV     WORD PTR ES:[BX+39h],CS
        JMP     SHORT DEV_NO_SYSVARS
DEV_USE_34:
        MOV     WORD PTR ES:[BX+34h],OFFSET EMB_TABLE
        MOV     WORD PTR ES:[BX+36h],CS
DEV_NO_SYSVARS:
        POP     DI
        POP     ES

DEV_DONE:
        MOV     WORD PTR ES:[DI+3],0100h ; Done
        POP     ES
        POP     DS
        POP     DI
        POP     DX
        POP     BX
        POP     AX
        RET
DEV_INTERRUPT   ENDP

        EVEN
RESIDENT_END    LABEL   BYTE

; ---------------------------------------------------------------------------
; Command-line utility entry point (MZ CS:IP)
; ---------------------------------------------------------------------------
EXE_START:
        CLD
        MOV     AX,CODE
        MOV     ES,AX
        MOV     WORD PTR ES:[PSP_SEG],DS

        ; Copy command tail DS:0081h..0081h+[80h] to ES:CMD_LINE
        MOV     SI,80h
        LODSB
        XOR     CH,CH
        MOV     CL,AL
        MOV     DI,OFFSET CMD_LINE
        JCXZ    TAIL_DONE
TAIL_COPY:
        LODSB
        CMP     AL,0Dh
        JE      TAIL_DONE
        STOSB
        LOOP    TAIL_COPY
TAIL_DONE:
        XOR     AL,AL
        STOSB

        PUSH    ES
        POP     DS                      ; DS = ES = CODE

        CALL    PARSE_TAIL
        JNC     CHECK_HELP
        JMP     ERR_BAD_SWITCH

CHECK_HELP:
        CMP     BYTE PTR [OPT_HELP],0
        JE      CHECK_QUIET_RULE
        MOV     SI,OFFSET MSG_HELP
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C00h
        INT     21h

CHECK_QUIET_RULE:
        ; /QUIET is only permitted with /DELETE
        CMP     BYTE PTR [OPT_QUIET],0
        JE      DETECT_HOSTED
        CMP     BYTE PTR [OPT_DELETE],0
        JNE     DETECT_HOSTED
        CMP     BYTE PTR [POS_COUNT],0
        JNE     ERR_TOO_MANY_JMP
        JMP     ERR_BAD_NAME
ERR_TOO_MANY_JMP:
        JMP     ERR_TOO_MANY

DETECT_HOSTED:
        ; Probe CK-DOS Extension API (INT 2Fh AX=D700h BX=434Bh)
        MOV     BYTE PTR [IS_HOSTED_API],0
        MOV     AX,0D700h
        MOV     BX,434Bh
        INT     2Fh
        JC      PROBE_LIVE_TAB
        CMP     AX,0D7FFh
        JNE     PROBE_LIVE_TAB
        CMP     CX,434Bh
        JNE     PROBE_LIVE_TAB
        MOV     BYTE PTR [IS_HOSTED_API],1

PROBE_LIVE_TAB:
        PUSH    CS
        POP     ES
        MOV     WORD PTR [LIVE_TAB],0
        MOV     WORD PTR [LIVE_TAB+2],0
        MOV     WORD PTR [LIVE_CAP],0
        CMP     BYTE PTR [IS_HOSTED_API],0
        JNE     DISPATCH_CMD

        ; Locate DOS 5+ SETVER table pointer (+34h on CK-DOS 5.0, +37h on retail MS-DOS 5.00)
        MOV     AH,30h
        INT     21h
        CMP     AL,5
        JB      DISPATCH_CMD
        PUSH    ES
        MOV     AH,52h
        INT     21h
        MOV     SI,34h
        MOV     CX,ES
        CMP     WORD PTR ES:[BX+36h],CX
        JE      READ_LOL_PTR
        CMP     WORD PTR ES:[BX+34h],OFFSET EMB_TABLE
        JNE     TRY_LOL_37
        MOV     DX,ES:[BX+36h]
        OR      DX,DX
        JZ      TRY_LOL_37
        PUSH    ES
        MOV     ES,DX
        CMP     WORD PTR ES:[SIG_MAGIC],4B43h
        POP     ES
        JE      READ_LOL_PTR
TRY_LOL_37:
        MOV     SI,37h
READ_LOL_PTR:
        MOV     AX,ES:[BX+SI]
        MOV     DX,ES:[BX+SI+2]
        MOV     WORD PTR [LIVE_TAB],AX
        MOV     WORD PTR [LIVE_TAB+2],DX
        MOV     WORD PTR [LIVE_CAP],256
        CMP     AX,OFFSET EMB_TABLE
        JNE     GOT_LIVE_CAP
        OR      DX,DX
        JZ      GOT_LIVE_CAP
        MOV     ES,DX
        CMP     WORD PTR ES:[SIG_MAGIC],4B43h   ; 'CK'
        JNE     GOT_LIVE_CAP
        CMP     WORD PTR ES:[SIG_MAGIC+2],5653h ; 'SV'
        JNE     GOT_LIVE_CAP
        MOV     CX,ES:[EMB_CAPACITY]
        MOV     WORD PTR [LIVE_CAP],CX
GOT_LIVE_CAP:
        POP     ES

DISPATCH_CMD:
        MOV     AL,[POS_COUNT]
        CMP     BYTE PTR [OPT_DELETE],0
        JE      NOT_DEL_CMD
        JMP     DO_DELETE_CMD

NOT_DEL_CMD:
        OR      AL,AL
        JZ      DO_LIST_0
        CMP     AL,1
        JNE     NDC_CHK2
        JMP     DO_LIST_1
NDC_CHK2:
        CMP     AL,2
        JNE     NDC_CHK3
        JMP     DO_ADD_2
NDC_CHK3:
        CMP     AL,3
        JNE     NDC_TOO_MANY
        JMP     DO_ADD_3
NDC_TOO_MANY:
        JMP     ERR_TOO_MANY

; ---------------------------------------------------------------------------
; LIST version table
; ---------------------------------------------------------------------------
DO_LIST_0:
        CMP     BYTE PTR [IS_HOSTED_API],0
        JE      LIST_BOOT_0
        CALL    HOSTED_LIST
        JC      ERR_NO_FILE_JMP
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        CALL    PRINT_TABLE
        MOV     AX,4C00h
        INT     21h

LIST_BOOT_0:
        MOV     AX,WORD PTR [LIVE_TAB]
        OR      AX,WORD PTR [LIVE_TAB+2]
        JZ      LIST_BOOT_DISK_DEF
        LES     DI,[LIVE_TAB]
        CALL    PRINT_TABLE
        PUSH    CS
        POP     ES
        MOV     AX,4C00h
        INT     21h

LIST_BOOT_DISK_DEF:
        MOV     BYTE PTR [HAS_EXPLICIT_PATH],0
        CALL    RESOLVE_DEFAULT_PATH
        CALL    LOAD_DISK_TABLE
        JC      ERR_NO_FILE_JMP
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        CALL    PRINT_TABLE
        MOV     SI,OFFSET MSG_DEV_NOTE
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C00h
        INT     21h

ERR_NO_FILE_JMP:
        JMP     ERR_NO_FILE

DO_LIST_1:
        MOV     SI,OFFSET POS1
        CALL    BUILD_SETVER_PATH
        CMP     BYTE PTR [IS_HOSTED_API],0
        JE      LIST_BOOT_1
        ; Validate explicit SETVER.EXE path exists before querying Hosted API
        CALL    CHECK_FILE_EXISTS
        JC      ERR_NO_FILE_JMP
        CALL    HOSTED_LIST
        JC      ERR_NO_FILE_JMP
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        CALL    PRINT_TABLE
        MOV     AX,4C00h
        INT     21h

LIST_BOOT_1:
        CALL    LOAD_DISK_TABLE
        JC      ERR_NO_FILE_JMP
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        CALL    PRINT_TABLE
        MOV     AX,WORD PTR [LIVE_TAB]
        OR      AX,WORD PTR [LIVE_TAB+2]
        JNZ     LIST_1_EXIT
        MOV     SI,OFFSET MSG_DEV_NOTE
        CALL    WRITE_STDOUT_Z
LIST_1_EXIT:
        MOV     AX,4C00h
        INT     21h

; ---------------------------------------------------------------------------
; ADD / UPDATE entry
; ---------------------------------------------------------------------------
DO_ADD_2:
        ; If POS1 is an existing directory (e.g. SETVER C:\DOS FOO.EXE), version is missing!
        MOV     SI,OFFSET POS1
        CALL    CHECK_IS_DIR_SIMPLE
        OR      AL,AL
        JZ      ADD_2_PARSE
        JMP     ERR_MISSING_PARAM

ADD_2_PARSE:
        MOV     SI,OFFSET POS1
        MOV     DI,OFFSET TARGET_NAME
        CALL    VALID_PROG_NAME
        JNC     ADD_2_VER
        JMP     ERR_BAD_NAME
ADD_2_VER:
        MOV     SI,OFFSET POS2
        CALL    PARSE_VER_STR
        JNC     ADD_2_GO
        JMP     ERR_BAD_VER
ADD_2_GO:
        MOV     BYTE PTR [HAS_EXPLICIT_PATH],0
        JMP     SHORT APPLY_ADD

DO_ADD_3:
        MOV     SI,OFFSET POS1
        CALL    BUILD_SETVER_PATH
        MOV     BYTE PTR [HAS_EXPLICIT_PATH],1
        MOV     SI,OFFSET POS2
        MOV     DI,OFFSET TARGET_NAME
        CALL    VALID_PROG_NAME
        JNC     ADD_3_VER
        JMP     ERR_BAD_NAME
ADD_3_VER:
        MOV     SI,OFFSET POS3
        CALL    PARSE_VER_STR
        JNC     APPLY_ADD
        JMP     ERR_BAD_VER

APPLY_ADD:
        CMP     BYTE PTR [IS_HOSTED_API],0
        JE      APPLY_ADD_BOOT
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JE      APPLY_ADD_HOSTED
        CALL    CHECK_FILE_EXISTS
        JNC     APPLY_ADD_HOSTED
        JMP     ERR_NO_FILE

APPLY_ADD_HOSTED:
        CALL    HOSTED_SET
        JC      ERR_NO_FILE_JMP2
        CMP     DL,0
        JE      HOSTED_ADD_OK
        CMP     DL,14
        JE      ERR_TABLE_FULL_JMP
        CMP     DL,2
        JE      ERR_BAD_NAME_JMP
        JMP     ERR_BAD_VER
HOSTED_ADD_OK:
        MOV     SI,OFFSET MSG_UPDATED
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C00h
        INT     21h

ERR_NO_FILE_JMP2:
        JMP     ERR_NO_FILE
ERR_TABLE_FULL_JMP:
        JMP     ERR_TABLE_FULL
ERR_BAD_NAME_JMP:
        JMP     ERR_BAD_NAME

APPLY_ADD_BOOT:
        MOV     BYTE PTR [TOUCHED_ANY],0
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JE      ADD_BOOT_LIVE
        CALL    LOAD_DISK_TABLE
        JC      ERR_NO_FILE_JMP2

ADD_BOOT_LIVE:
        MOV     AX,WORD PTR [LIVE_TAB]
        OR      AX,WORD PTR [LIVE_TAB+2]
        JZ      ADD_BOOT_DISK
        LES     DI,[LIVE_TAB]
        MOV     CX,[LIVE_CAP]
        CALL    TABLE_SET_ENTRY
        PUSH    CS
        POP     ES
        JNC     ADD_BOOT_LIVE_OK
        JMP     ERR_TABLE_FULL
ADD_BOOT_LIVE_OK:
        MOV     BYTE PTR [TOUCHED_ANY],1

ADD_BOOT_DISK:
        CALL    SYNC_DISK_ADD
        CMP     BYTE PTR [TOUCHED_ANY],0
        JNE     ADD_BOOT_MSG
        JMP     ERR_NO_FILE
ADD_BOOT_MSG:
        MOV     SI,OFFSET MSG_UPDATED
        CALL    WRITE_STDOUT_Z
        MOV     AX,WORD PTR [LIVE_TAB]
        OR      AX,WORD PTR [LIVE_TAB+2]
        JNZ     ADD_BOOT_DONE
        MOV     SI,OFFSET MSG_DEV_NOTE
        CALL    WRITE_STDOUT_Z
ADD_BOOT_DONE:
        MOV     AX,4C00h
        INT     21h

; ---------------------------------------------------------------------------
; DELETE entry
; ---------------------------------------------------------------------------
DO_DELETE_CMD:
        OR      AL,AL
        JNZ     DDC_CHK1
        JMP     ERR_BAD_NAME
DDC_CHK1:
        CMP     AL,1
        JE      DEL_1
        CMP     AL,2
        JE      DEL_2
        JMP     ERR_TOO_MANY

DEL_1:
        MOV     BYTE PTR [HAS_EXPLICIT_PATH],0
        MOV     SI,OFFSET POS1
        JMP     SHORT DEL_COMMON
DEL_2:
        MOV     SI,OFFSET POS1
        CALL    BUILD_SETVER_PATH
        MOV     BYTE PTR [HAS_EXPLICIT_PATH],1
        MOV     SI,OFFSET POS2
DEL_COMMON:
        MOV     DI,OFFSET TARGET_NAME
        CALL    VALID_PROG_NAME
        JNC     DEL_CHK_HOSTED
        JMP     ERR_BAD_NAME

DEL_CHK_HOSTED:
        CMP     BYTE PTR [IS_HOSTED_API],0
        JE      DEL_BOOT
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JE      DEL_HOSTED_CALL
        CALL    CHECK_FILE_EXISTS
        JNC     DEL_HOSTED_CALL
        JMP     ERR_NO_FILE

DEL_HOSTED_CALL:
        CALL    HOSTED_DELETE
        JNC     DEL_HOSTED_RC
        JMP     ERR_NO_FILE
DEL_HOSTED_RC:
        CMP     DL,0
        JE      DEL_REPORT_OK
        JMP     ERR_NOT_FOUND

DEL_BOOT:
        MOV     BYTE PTR [FOUND_ANY],0
        MOV     BYTE PTR [HAVE_STORE],0
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JE      DEL_BOOT_LIVE
        CALL    LOAD_DISK_TABLE
        JNC     DEL_BOOT_LIVE
        JMP     ERR_NO_FILE

DEL_BOOT_LIVE:
        MOV     AX,WORD PTR [LIVE_TAB]
        OR      AX,WORD PTR [LIVE_TAB+2]
        JZ      DEL_BOOT_DISK
        MOV     BYTE PTR [HAVE_STORE],1
        LES     DI,[LIVE_TAB]
        MOV     CX,[LIVE_CAP]
        CALL    TABLE_DEL_ENTRY
        PUSH    CS
        POP     ES
        JC      DEL_BOOT_DISK
        MOV     BYTE PTR [FOUND_ANY],1

DEL_BOOT_DISK:
        CALL    SYNC_DISK_DEL
        CMP     BYTE PTR [HAVE_STORE],0
        JNE     DEL_BOOT_CHK_FOUND
        JMP     ERR_NO_FILE
DEL_BOOT_CHK_FOUND:
        CMP     BYTE PTR [FOUND_ANY],0
        JNE     DEL_REPORT_OK
        JMP     ERR_NOT_FOUND

DEL_REPORT_OK:
        CMP     BYTE PTR [OPT_QUIET],0
        JNE     DEL_EXIT_OK
        MOV     SI,OFFSET MSG_DELETED
        CALL    WRITE_STDOUT_Z
DEL_EXIT_OK:
        MOV     AX,4C00h
        INT     21h

; ---------------------------------------------------------------------------
; Error Exits (matching MS-DOS 5.00 SETVER errorlevels)
; ---------------------------------------------------------------------------
ERR_BAD_SWITCH:
        MOV     SI,OFFSET MSG_BAD_SW
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C01h
        INT     21h

ERR_BAD_NAME:
        MOV     SI,OFFSET MSG_BAD_NAME
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C02h
        INT     21h

ERR_BAD_VER:
        MOV     SI,OFFSET MSG_BAD_VER
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C04h
        INT     21h

ERR_NOT_FOUND:
        MOV     SI,OFFSET MSG_NOT_FOUND
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C05h
        INT     21h

ERR_NO_FILE:
        MOV     SI,OFFSET MSG_NO_FILE
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C06h
        INT     21h

ERR_TOO_MANY:
        MOV     SI,OFFSET MSG_TOO_MANY
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C08h
        INT     21h

ERR_MISSING_PARAM:
        MOV     SI,OFFSET MSG_MISSING
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C09h
        INT     21h

ERR_TABLE_FULL:
        MOV     SI,OFFSET MSG_FULL
        CALL    WRITE_STDOUT_Z
        MOV     AX,4C0Eh
        INT     21h

; ---------------------------------------------------------------------------
; Hosted CK-DOS Extension API Service 0005h Helpers
; ---------------------------------------------------------------------------
HOSTED_LIST     PROC    NEAR
        PUSH    CS
        POP     ES
        MOV     BYTE PTR [EXT_REQ],00h
        MOV     AX,0D701h
        MOV     BX,0005h
        MOV     CX,1
        MOV     DX,OFFSET EXT_REQ
        MOV     SI,1024
        MOV     DI,OFFSET DISK_TAB_BUF
        INT     2Fh
        RET
HOSTED_LIST     ENDP

HOSTED_SET      PROC    NEAR
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET EXT_REQ
        MOV     AL,01h
        STOSB
        MOV     AL,[TARGET_LEN]
        STOSB
        XOR     CH,CH
        MOV     CL,AL
        MOV     SI,OFFSET TARGET_NAME
        REP     MOVSB
        MOV     AL,[TARGET_MAJ]
        STOSB
        MOV     AL,[TARGET_MIN]
        STOSB
        MOV     CX,DI
        SUB     CX,OFFSET EXT_REQ
        MOV     AX,0D701h
        MOV     BX,0005h
        MOV     DX,OFFSET EXT_REQ
        MOV     SI,16
        MOV     DI,OFFSET EXT_RESP
        INT     2Fh
        RET
HOSTED_SET      ENDP

HOSTED_DELETE   PROC    NEAR
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET EXT_REQ
        MOV     AL,02h
        STOSB
        MOV     AL,[TARGET_LEN]
        STOSB
        XOR     CH,CH
        MOV     CL,AL
        MOV     SI,OFFSET TARGET_NAME
        REP     MOVSB
        MOV     CX,DI
        SUB     CX,OFFSET EXT_REQ
        MOV     AX,0D701h
        MOV     BX,0005h
        MOV     DX,OFFSET EXT_REQ
        MOV     SI,16
        MOV     DI,OFFSET EXT_RESP
        INT     2Fh
        RET
HOSTED_DELETE   ENDP

; ---------------------------------------------------------------------------
; Print all entries in ES:DI
; ---------------------------------------------------------------------------
PRINT_TABLE     PROC    NEAR
        MOV     AL,ES:[DI]
        OR      AL,AL
        JNZ     PT_LOOP
        MOV     SI,OFFSET MSG_EMPTY
        CALL    WRITE_STDOUT_Z
        RET
PT_LOOP:
        MOV     AL,ES:[DI]
        OR      AL,AL
        JZ      PT_DONE
        CMP     AL,0FFh
        JE      PT_DONE
        XOR     CH,CH
        MOV     CL,AL
        INC     DI
        MOV     BX,16                   ; pad column to 16 chars
PT_NAME_CH:
        MOV     AL,ES:[DI]
        INC     DI
        CALL    WRITE_CHAR_STDOUT
        DEC     BX
        LOOP    PT_NAME_CH
PT_PAD:
        CMP     BX,0
        JLE     PT_VER
        MOV     AL,' '
        CALL    WRITE_CHAR_STDOUT
        DEC     BX
        JMP     SHORT PT_PAD
PT_VER:
        MOV     AL,ES:[DI]              ; major
        INC     DI
        CALL    PRINT_DEC_BYTE
        MOV     AL,'.'
        CALL    WRITE_CHAR_STDOUT
        MOV     AL,ES:[DI]              ; minor (2 digits)
        INC     DI
        XOR     AH,AH
        MOV     BL,10
        DIV     BL
        PUSH    AX
        ADD     AL,'0'
        CALL    WRITE_CHAR_STDOUT
        POP     AX
        MOV     AL,AH
        ADD     AL,'0'
        CALL    WRITE_CHAR_STDOUT
        MOV     SI,OFFSET MSG_CRLF
        CALL    WRITE_STDOUT_Z
        JMP     SHORT PT_LOOP
PT_DONE:
        RET
PRINT_TABLE     ENDP

PRINT_DEC_BYTE  PROC    NEAR
        XOR     AH,AH
        CMP     AL,10
        JB      PDB_ONE
        MOV     BL,10
        DIV     BL
        PUSH    AX
        ADD     AL,'0'
        CALL    WRITE_CHAR_STDOUT
        POP     AX
        MOV     AL,AH
PDB_ONE:
        ADD     AL,'0'
        CALL    WRITE_CHAR_STDOUT
        RET
PRINT_DEC_BYTE  ENDP

; ---------------------------------------------------------------------------
; Mutate table at ES:DI (capacity CX): set or delete TARGET_NAME
; ---------------------------------------------------------------------------
TABLE_DEL_ENTRY PROC    NEAR
        MOV     BX,DI
        ADD     CX,DI
TD_SCAN:
        CMP     DI,CX
        JAE     TD_MISS
        MOV     AL,ES:[DI]
        OR      AL,AL
        JZ      TD_MISS
        CMP     AL,0FFh
        JE      TD_MISS
        XOR     AH,AH
        CMP     AL,[TARGET_LEN]
        JNE     TD_NEXT
        PUSH    DI
        PUSH    SI
        PUSH    CX
        INC     DI
        MOV     SI,OFFSET TARGET_NAME
        MOV     CL,AL
        XOR     CH,CH
        REPE    CMPSB
        POP     CX
        POP     SI
        POP     DI
        JE      TD_FOUND
        MOV     AL,ES:[DI]
        XOR     AH,AH
TD_NEXT:
        ADD     DI,AX
        ADD     DI,3
        JMP     SHORT TD_SCAN
TD_FOUND:
        MOV     AL,ES:[DI]
        XOR     AH,AH
        MOV     SI,DI
        ADD     SI,AX
        ADD     SI,3
        MOV     BX,SI
TD_END_SCAN:
        CMP     BX,CX
        JAE     TD_COMPACT
        MOV     AL,ES:[BX]
        INC     BX
        OR      AL,AL
        JZ      TD_COMPACT
        XOR     AH,AH
        ADD     BX,AX
        ADD     BX,2
        JMP     SHORT TD_END_SCAN
TD_COMPACT:
        PUSH    DS
        PUSH    ES
        POP     DS
        MOV     CX,BX
        SUB     CX,SI
        REP     MOVSB
        MOV     BYTE PTR ES:[DI],0
        POP     DS
        CLC
        RET
TD_MISS:
        STC
        RET
TABLE_DEL_ENTRY ENDP

TABLE_SET_ENTRY PROC    NEAR
        PUSH    DI
        PUSH    CX
        CALL    TABLE_DEL_ENTRY
        POP     CX
        POP     DI
        MOV     DX,DI
        ADD     DX,CX
TS_FIND_END:
        CMP     DI,DX
        JAE     TS_FULL
        MOV     AL,ES:[DI]
        OR      AL,AL
        JZ      TS_AT_END
        XOR     AH,AH
        ADD     DI,AX
        ADD     DI,3
        JMP     SHORT TS_FIND_END
TS_AT_END:
        XOR     AH,AH
        MOV     AL,[TARGET_LEN]
        MOV     BX,DI
        ADD     BX,AX
        ADD     BX,4
        CMP     BX,DX
        JA      TS_FULL
        STOSB
        MOV     SI,OFFSET TARGET_NAME
        MOV     CL,AL
        XOR     CH,CH
        REP     MOVSB
        MOV     AL,[TARGET_MAJ]
        STOSB
        MOV     AL,[TARGET_MIN]
        STOSB
        MOV     BYTE PTR ES:[DI],0
        CLC
        RET
TS_FULL:
        STC
        RET
TABLE_SET_ENTRY ENDP

; ---------------------------------------------------------------------------
; Disk persistence in SETVER.EXE (locates 'CKSV' signature and updates table)
; ---------------------------------------------------------------------------
CHECK_FILE_EXISTS PROC  NEAR
        MOV     DX,OFFSET FILE_PATH
        MOV     AX,4300h
        INT     21h
        JC      CFE_NO
        TEST    CX,10h
        JNZ     CFE_NO
        CLC
        RET
CFE_NO:
        STC
        RET
CHECK_FILE_EXISTS ENDP

CHECK_IS_DIR_SIMPLE PROC NEAR
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET TMP_PATH
        XOR     CX,CX
CIDS_CP:
        LODSB
        STOSB
        OR      AL,AL
        JZ      CIDS_END
        INC     CX
        CMP     CX,76
        JB      CIDS_CP
        MOV     BYTE PTR [DI],0
CIDS_END:
        JCXZ    CIDS_NO
        CMP     CX,1
        JNE     CIDS_DRV
        MOV     AL,[TMP_PATH]
        CMP     AL,'\'
        JE      CIDS_YES
        CMP     AL,'/'
        JE      CIDS_YES
CIDS_DRV:
        CMP     BYTE PTR [TMP_PATH+1],':'
        JNE     CIDS_STRIP
        CMP     CX,2
        JE      CIDS_CHK_DRV
        CMP     CX,3
        JNE     CIDS_STRIP
        MOV     AL,[TMP_PATH+2]
        CMP     AL,'\'
        JE      CIDS_CHK_DRV
        CMP     AL,'/'
        JNE     CIDS_STRIP
CIDS_CHK_DRV:
        MOV     AL,[TMP_PATH]
        SUB     AL,'A'-1
        MOV     DL,AL
        MOV     AH,36h
        INT     21h
        CMP     AX,0FFFFh
        JE      CIDS_NO
        JMP     SHORT CIDS_YES
CIDS_STRIP:
        MOV     BX,CX
        DEC     BX
        MOV     AL,[TMP_PATH+BX]
        CMP     AL,'\'
        JE      CIDS_ZERO_TAIL
        CMP     AL,'/'
        JNE     CIDS_ATTR
CIDS_ZERO_TAIL:
        MOV     BYTE PTR [TMP_PATH+BX],0
CIDS_ATTR:
        MOV     DX,OFFSET TMP_PATH
        MOV     AX,4300h
        INT     21h
        JC      CIDS_NO
        TEST    CX,10h
        JNZ     CIDS_YES
CIDS_NO:
        XOR     AL,AL
        JMP     SHORT CIDS_RET
CIDS_YES:
        MOV     AL,1
CIDS_RET:
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        RET
CHECK_IS_DIR_SIMPLE ENDP

RESOLVE_DEFAULT_PATH PROC NEAR
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JNE     RDP_OK
        PUSH    CS
        POP     ES
        MOV     SI,OFFSET DEF_SETVER_PATH
        MOV     DI,OFFSET FILE_PATH
RDP_CP:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     RDP_CP
RDP_OK:
        RET
RESOLVE_DEFAULT_PATH ENDP

BUILD_SETVER_PATH PROC NEAR
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET FILE_PATH
BSP_CP:
        LODSB
        OR      AL,AL
        JZ      BSP_APPEND
        STOSB
        JMP     SHORT BSP_CP
BSP_APPEND:
        CMP     DI,OFFSET FILE_PATH
        JE      BSP_NAME
        MOV     AL,[DI-1]
        CMP     AL,'\'
        JE      BSP_NAME
        CMP     AL,'/'
        JE      BSP_NAME
        CMP     AL,':'
        JE      BSP_NAME
        MOV     AL,'\'
        STOSB
BSP_NAME:
        MOV     SI,OFFSET STR_SETVER_EXE
BSP_N2:
        LODSB
        STOSB
        OR      AL,AL
        JNZ     BSP_N2
        RET
BUILD_SETVER_PATH ENDP

LOAD_DISK_TABLE PROC    NEAR
        MOV     AX,3D00h
        MOV     DX,OFFSET FILE_PATH
        INT     21h
        JC      LDT_FAIL
        MOV     BX,AX
        MOV     AH,3Fh
        MOV     CX,4096
        MOV     DX,OFFSET IO_BUF
        INT     21h
        PUSHF
        PUSH    AX
        MOV     AH,3Eh
        INT     21h
        POP     CX
        POPF
        JC      LDT_FAIL
        CALL    FIND_CKSV_IN_IOBUF
        JC      LDT_FAIL
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        MOV     CX,1024
        REP     MOVSB
        CLC
        RET
LDT_FAIL:
        STC
        RET
LOAD_DISK_TABLE ENDP

FIND_CKSV_IN_IOBUF PROC NEAR
        CMP     CX,1060
        JB      FCI_FAIL
        SUB     CX,1030
        MOV     SI,OFFSET IO_BUF
FCI_LP:
        CMP     WORD PTR [SI],4B43h     ; 'CK'
        JNE     FCI_NX
        CMP     WORD PTR [SI+2],5653h   ; 'SV'
        JNE     FCI_NX
        CMP     WORD PTR [SI+4],1024
        JE      FCI_OK
FCI_NX:
        INC     SI
        LOOP    FCI_LP
FCI_FAIL:
        STC
        RET
FCI_OK:
        MOV     AX,SI
        SUB     AX,OFFSET IO_BUF
        ADD     AX,6
        MOV     [DISK_TAB_OFF],AX
        ADD     SI,6
        CLC
        RET
FIND_CKSV_IN_IOBUF ENDP

SYNC_DISK_ADD   PROC    NEAR
        CALL    RESOLVE_DEFAULT_PATH
        CALL    LOAD_DISK_TABLE
        JNC     SDA_HAVE
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JNE     SDA_ERR
        RET
SDA_HAVE:
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        MOV     CX,1024
        CALL    TABLE_SET_ENTRY
        JC      SDA_FULL
        CALL    WRITE_DISK_TABLE
        JC      SDA_RET
        MOV     BYTE PTR [TOUCHED_ANY],1
SDA_RET:
        RET
SDA_ERR:
        POP     AX
        JMP     ERR_NO_FILE
SDA_FULL:
        POP     AX
        JMP     ERR_TABLE_FULL
SYNC_DISK_ADD   ENDP

SYNC_DISK_DEL   PROC    NEAR
        CALL    RESOLVE_DEFAULT_PATH
        CALL    LOAD_DISK_TABLE
        JNC     SDD_HAVE
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JNE     SDA_ERR
        RET
SDD_HAVE:
        MOV     BYTE PTR [HAVE_STORE],1
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        MOV     CX,1024
        CALL    TABLE_DEL_ENTRY
        JC      SDD_RET
        MOV     BYTE PTR [FOUND_ANY],1
        CALL    WRITE_DISK_TABLE
SDD_RET:
        RET
SYNC_DISK_DEL   ENDP

WRITE_DISK_TABLE PROC   NEAR
        MOV     AX,3D02h
        MOV     DX,OFFSET FILE_PATH
        INT     21h
        JC      WDT_FAIL
        MOV     BX,AX
        XOR     CX,CX
        MOV     DX,[DISK_TAB_OFF]
        MOV     AX,4200h
        INT     21h
        JC      WDT_CLOSE_ERR
        MOV     AH,40h
        MOV     CX,1024
        MOV     DX,OFFSET DISK_TAB_BUF
        INT     21h
        JC      WDT_CLOSE_ERR
        CMP     AX,1024
        JNE     WDT_CLOSE_ERR
        MOV     AH,3Eh
        INT     21h
        CLC
        RET
WDT_CLOSE_ERR:
        MOV     AH,3Eh
        INT     21h
WDT_FAIL:
        STC
        RET
WRITE_DISK_TABLE ENDP

; ---------------------------------------------------------------------------
; Argument parsing & validation helpers
; ---------------------------------------------------------------------------
PARSE_TAIL      PROC    NEAR
        MOV     SI,OFFSET CMD_LINE
        MOV     BYTE PTR [POS_COUNT],0
        MOV     BYTE PTR [OPT_HELP],0
        MOV     BYTE PTR [OPT_DELETE],0
        MOV     BYTE PTR [OPT_QUIET],0
PTL_NEXT:
        CALL    SKIP_WS
        CMP     BYTE PTR [SI],0
        JNE     PTL_CHK_SW
        JMP     PTL_OK
PTL_CHK_SW:
        CMP     BYTE PTR [SI],'/'
        JE      PTL_SWITCH
        MOV     AL,[POS_COUNT]
        CMP     AL,4
        JB      PTL_STORE_POS
        INC     BYTE PTR [POS_COUNT]
        JMP     SHORT PTL_SKIP_EXTRA
PTL_STORE_POS:
        MOV     DI,OFFSET POS1
        CMP     AL,0
        JE      PTL_COPY_POS
        MOV     DI,OFFSET POS2
        CMP     AL,1
        JE      PTL_COPY_POS
        MOV     DI,OFFSET POS3
        CMP     AL,2
        JE      PTL_COPY_POS
        MOV     DI,OFFSET POS4
PTL_COPY_POS:
        INC     BYTE PTR [POS_COUNT]
        MOV     CX,63
PTL_CP_LP:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PTL_CP_END
        CMP     AL,' '
        JBE     PTL_CP_END
        CMP     AL,'/'
        JE      PTL_CP_END
        CALL    TO_UPPER
        STOSB
        INC     SI
        LOOP    PTL_CP_LP
PTL_CP_END:
        MOV     BYTE PTR [DI],0
PTL_BACK:
        JMP     PTL_NEXT

PTL_SKIP_EXTRA:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PTL_BACK
        CMP     AL,' '
        JBE     PTL_BACK
        CMP     AL,'/'
        JE      PTL_BACK
        INC     SI
        JMP     SHORT PTL_SKIP_EXTRA

PTL_SWITCH:
        INC     SI
        MOV     AL,[SI]
        CALL    TO_UPPER
        CMP     AL,'?'
        JNE     PTL_SW_D
        INC     SI
        MOV     BYTE PTR [OPT_HELP],1
        JMP     SHORT PTL_BACK
PTL_SW_D:
        CMP     AL,'D'
        JNE     PTL_SW_Q
        MOV     BYTE PTR [OPT_DELETE],1
PTL_EAT_SW:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PTL_BACK
        CMP     AL,' '
        JBE     PTL_BACK
        CMP     AL,'/'
        JE      PTL_BACK
        INC     SI
        JMP     SHORT PTL_EAT_SW
PTL_SW_Q:
        CMP     AL,'Q'
        JNE     PTL_ERR
        MOV     BYTE PTR [OPT_QUIET],1
        JMP     SHORT PTL_EAT_SW

PTL_OK:
        CLC
        RET
PTL_ERR:
        STC
        RET
PARSE_TAIL      ENDP

SKIP_WS         PROC    NEAR
SW_LP:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      SW_RET
        CMP     AL,' '
        JA      SW_RET
        INC     SI
        JMP     SHORT SW_LP
SW_RET:
        RET
SKIP_WS         ENDP

TO_UPPER        PROC    NEAR
        CMP     AL,'a'
        JB      TU_RET
        CMP     AL,'z'
        JA      TU_RET
        SUB     AL,20h
TU_RET:
        RET
TO_UPPER        ENDP

; Validates strict 8.3 DOS filename (1..8 stem, optional '.' + 1..3 ext, no wildcards/separators)
VALID_PROG_NAME PROC    NEAR
        XOR     CL,CL                   ; total length
        XOR     CH,CH                   ; stem/ext counter
        MOV     BYTE PTR [SEEN_DOT],0
VPN_LP:
        LODSB
        OR      AL,AL
        JZ      VPN_END
        CMP     AL,'.'
        JE      VPN_DOT
        CMP     AL,20h
        JBE     VPN_BAD
        PUSH    DI
        PUSH    CX
        MOV     DI,OFFSET BAD_CHARS
        MOV     CX,15
        REPNE   SCASB
        POP     CX
        POP     DI
        JE      VPN_BAD
        INC     CH
        CMP     BYTE PTR [SEEN_DOT],0
        JNE     VPN_CHK_EXT
        CMP     CH,8
        JA      VPN_BAD
        JMP     SHORT VPN_STORE
VPN_CHK_EXT:
        CMP     CH,3
        JA      VPN_BAD
VPN_STORE:
        INC     CL
        CMP     CL,12
        JA      VPN_BAD
        STOSB
        JMP     SHORT VPN_LP
VPN_DOT:
        CMP     BYTE PTR [SEEN_DOT],0
        JNE     VPN_BAD
        OR      CH,CH
        JZ      VPN_BAD
        MOV     BYTE PTR [SEEN_DOT],1
        XOR     CH,CH
        INC     CL
        CMP     CL,12
        JA      VPN_BAD
        STOSB
        JMP     SHORT VPN_LP
VPN_END:
        OR      CL,CL
        JZ      VPN_BAD
        CMP     BYTE PTR [SEEN_DOT],0
        JE      VPN_OK
        OR      CH,CH
        JZ      VPN_BAD
VPN_OK:
        MOV     [TARGET_LEN],CL
        CLC
        RET
VPN_BAD:
        STC
        RET
VALID_PROG_NAME ENDP

BAD_CHARS       DB      '\/:*?"<>|;,+=[]'

; Parses "n.nn" or "n.n" in 2.11 .. 9.99 (matching MS-DOS 5.00 SETVER range)
PARSE_VER_STR   PROC    NEAR
        LODSB
        CMP     AL,'2'
        JB      PVS_BAD
        CMP     AL,'9'
        JA      PVS_BAD
        SUB     AL,'0'
        MOV     [TARGET_MAJ],AL
        LODSB
        CMP     AL,'.'
        JNE     PVS_BAD
        LODSB
        CMP     AL,'0'
        JB      PVS_BAD
        CMP     AL,'9'
        JA      PVS_BAD
        SUB     AL,'0'
        MOV     BL,AL
        LODSB
        OR      AL,AL
        JZ      PVS_ONE_MINOR
        CMP     AL,'0'
        JB      PVS_BAD
        CMP     AL,'9'
        JA      PVS_BAD
        SUB     AL,'0'
        MOV     BH,AL
        CMP     BYTE PTR [SI],0
        JNE     PVS_BAD
        MOV     AL,BL
        MOV     CL,10
        MUL     CL
        ADD     AL,BH
        JMP     SHORT PVS_CHK_MIN
PVS_ONE_MINOR:
        MOV     AL,BL
        MOV     CL,10
        MUL     CL
PVS_CHK_MIN:
        MOV     [TARGET_MIN],AL
        CMP     BYTE PTR [TARGET_MAJ],2
        JNE     PVS_OK
        CMP     AL,11
        JB      PVS_BAD
PVS_OK:
        CLC
        RET
PVS_BAD:
        STC
        RET
PARSE_VER_STR   ENDP

WRITE_STDOUT_Z  PROC    NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        MOV     DX,SI
        XOR     CX,CX
WSZ_LP:
        LODSB
        OR      AL,AL
        JZ      WSZ_DO
        INC     CX
        JMP     SHORT WSZ_LP
WSZ_DO:
        JCXZ    WSZ_RET
        MOV     BX,1
        MOV     AH,40h
        INT     21h
WSZ_RET:
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
WRITE_STDOUT_Z  ENDP

WRITE_CHAR_STDOUT PROC  NEAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        MOV     [CHAR_TMP],AL
        MOV     BX,1
        MOV     CX,1
        MOV     DX,OFFSET CHAR_TMP
        MOV     AH,40h
        INT     21h
        POP     DX
        POP     CX
        POP     BX
        POP     AX
        RET
WRITE_CHAR_STDOUT ENDP

; ---------------------------------------------------------------------------
; Messages and working buffers
; ---------------------------------------------------------------------------
DEF_SETVER_PATH DB      'C:\DOS\SETVER.EXE',0
STR_SETVER_EXE  DB      'SETVER.EXE',0

MSG_HELP        DB      'Sets the version number that CK-DOS reports to a program.',13,10,13,10
                DB      'Display current version table:  SETVER [drive:path]',13,10
                DB      'Add entry:                      SETVER [drive:path] filename n.nn',13,10
                DB      'Delete entry:                   SETVER [drive:path] filename /DELETE [/QUIET]',13,10,0

MSG_BAD_SW      DB      13,10,'ERROR: Invalid switch.',13,10,'       Use "SETVER /?" for help',13,10,0
MSG_BAD_NAME    DB      13,10,'ERROR: Invalid filename.',13,10,'       Use "SETVER /?" for help',13,10,0
MSG_BAD_VER     DB      13,10,'ERROR: Invalid version number, format must be 2.11 - 9.99.',13,10,'       Use "SETVER /?" for help',13,10,0
MSG_NOT_FOUND   DB      13,10,'ERROR: Specified entry was not found in the version table.',13,10,'       Use "SETVER /?" for help',13,10,0
MSG_NO_FILE     DB      13,10,'ERROR: Could not find the file SETVER.EXE.',13,10,'       Use "SETVER /?" for help',13,10,0
MSG_TOO_MANY    DB      13,10,'ERROR: Too many command line parameters.',13,10,'       Use "SETVER /?" for help',13,10,0
MSG_MISSING     DB      13,10,'ERROR: Missing parameter.',13,10,'       Use "SETVER /?" for help',13,10,0
MSG_FULL        DB      13,10,'ERROR: Insufficient space in version table for new entry.',13,10,0
MSG_EMPTY       DB      'No entries found in version table.',13,10,0
MSG_UPDATED     DB      'Version table successfully updated.',13,10,0
MSG_DELETED     DB      'Version table entry successfully deleted.',13,10,0
MSG_DEV_NOTE    DB      13,10,'NOTE: SETVER device not loaded. Add DEVICE=SETVER.EXE to CONFIG.SYS.',13,10,0
MSG_CRLF        DB      13,10,0

CHAR_TMP        DB      0
IS_HOSTED_API   DB      0
SEEN_DOT        DB      0
PSP_SEG         DW      0
LIVE_TAB        DD      0
LIVE_CAP        DW      0
OPT_HELP        DB      0
OPT_DELETE      DB      0
OPT_QUIET       DB      0
POS_COUNT       DB      0
HAS_EXPLICIT_PATH DB    0
FOUND_ANY       DB      0
TOUCHED_ANY     DB      0
HAVE_STORE      DB      0
TARGET_LEN      DB      0
TARGET_MAJ      DB      0
TARGET_MIN      DB      0
TARGET_NAME     DB      16 DUP (0)
DISK_TAB_OFF    DW      0

EXT_REQ         DB      24 DUP (0)
EXT_RESP        DB      24 DUP (0)
CMD_LINE        DB      132 DUP (0)
POS1            DB      64 DUP (0)
POS2            DB      64 DUP (0)
POS3            DB      64 DUP (0)
POS4            DB      64 DUP (0)
FILE_PATH       DB      84 DUP (0)
TMP_PATH        DB      84 DUP (0)
DISK_TAB_BUF    DB      1024 DUP (0)
IO_BUF          DB      4096 DUP (0)

CODE    ENDS

STACK   SEGMENT PARA STACK 'STACK'
        DB      512 DUP (?)
STACK   ENDS

        END     EXE_START
