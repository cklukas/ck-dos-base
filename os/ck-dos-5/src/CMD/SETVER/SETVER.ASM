; Copyright (c) 2026 C. Klukas
; SPDX-License-Identifier: MIT
;
; SETVER.EXE — Clean-room CK-DOS 5.0 version table device driver and utility.
;
; Dual device-driver + MZ executable:
;   - DEVICE=C:\DOS\SETVER.EXE in CONFIG.SYS installs the 1024-byte version
;     table and points SysInitVars+34h (SYSI_SETVER_TAB) at it.
;   - Running SETVER at the DOS prompt lists, adds/updates, or deletes entries
;     in both the live SYSI_SETVER_TAB table and SETVER.EXE on disk.

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

        ; Point SysInitVars+34h (SYSI_SETVER_TAB) at CS:EMB_TABLE
        PUSH    ES
        PUSH    DI
        MOV     AH,52h
        INT     21h
        MOV     WORD PTR ES:[BX+34h],OFFSET EMB_TABLE
        MOV     WORD PTR ES:[BX+36h],CS
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
        JC       USAGE_ERR

        CMP     BYTE PTR [OPT_HELP],0
        JE      NOT_HELP
        MOV     DX,OFFSET MSG_HELP
        CALL    PRINT_STR
        MOV     AX,4C00h
        INT     21h

USAGE_ERR:
        MOV     DX,OFFSET MSG_SYNTAX
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

NOT_HELP:
        ; Obtain live SYSI_SETVER_TAB pointer from INT 21h AH=52h
        PUSH    ES
        MOV     AH,52h
        INT     21h
        MOV     AX,ES:[BX+34h]
        MOV     DX,ES:[BX+36h]
        MOV     WORD PTR [LIVE_TAB],AX
        MOV     WORD PTR [LIVE_TAB+2],DX
        ; Determine live capacity: 1024 if resident SETVERXX, else 256
        MOV     WORD PTR [LIVE_CAP],256
        CMP     AX,OFFSET EMB_TABLE
        JNE     GOT_LIVE_CAP
        MOV     ES,DX
        CMP     WORD PTR ES:[SIG_MAGIC],4B43h   ; 'CK'
        JNE     GOT_LIVE_CAP
        CMP     WORD PTR ES:[SIG_MAGIC+2],5653h ; 'SV'
        JNE     GOT_LIVE_CAP
        MOV     CX,ES:[EMB_CAPACITY]
        MOV     WORD PTR [LIVE_CAP],CX
GOT_LIVE_CAP:
        POP     ES

        MOV     AL,[POS_COUNT]
        CMP     BYTE PTR [OPT_DELETE],0
        JE      NOT_DEL_CMD
        JMP     DO_DELETE_CMD
NOT_DEL_CMD:

        OR      AL,AL
        JZ      DO_LIST_LIVE
        CMP     AL,1
        JE      DO_LIST_PATH
        CMP     AL,2
        JE      DO_ADD_2
        CMP     AL,3
        JE      DO_ADD_3
        JMP     USAGE_ERR

; ---------------------------------------------------------------------------
; LIST live version table
; ---------------------------------------------------------------------------
DO_LIST_LIVE:
        LES     DI,[LIVE_TAB]
        CALL    PRINT_TABLE
        PUSH    CS
        POP     ES
        MOV     AX,4C00h
        INT     21h

DO_LIST_PATH:
        MOV     SI,OFFSET POS1
        CALL    BUILD_SETVER_PATH
        CALL    LOAD_DISK_TABLE
        JNC     LIST_DISK_OK
        JMP     FILE_ERR
LIST_DISK_OK:
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET DISK_TAB_BUF
        CALL    PRINT_TABLE
        MOV     AX,4C00h
        INT     21h

; ---------------------------------------------------------------------------
; ADD / UPDATE entry
; ---------------------------------------------------------------------------
DO_ADD_2:
        MOV     SI,OFFSET POS1
        MOV     DI,OFFSET TARGET_NAME
        CALL    VALID_PROG_NAME
        JC      ADD_BAD_NAME
        MOV     SI,OFFSET POS2
        CALL    PARSE_VER_STR
        JC      ADD_BAD_VER
        MOV     BYTE PTR [HAS_EXPLICIT_PATH],0
        JMP     SHORT APPLY_ADD

DO_ADD_3:
        MOV     SI,OFFSET POS1
        CALL    BUILD_SETVER_PATH
        MOV     BYTE PTR [HAS_EXPLICIT_PATH],1
        MOV     SI,OFFSET POS2
        MOV     DI,OFFSET TARGET_NAME
        CALL    VALID_PROG_NAME
        JC      ADD_BAD_NAME
        MOV     SI,OFFSET POS3
        CALL    PARSE_VER_STR
        JC      ADD_BAD_VER

APPLY_ADD:
        ; Update live table if available
        MOV     AX,WORD PTR [LIVE_TAB]
        OR      AX,WORD PTR [LIVE_TAB+2]
        JZ      ADD_TRY_DISK
        LES     DI,[LIVE_TAB]
        MOV     CX,[LIVE_CAP]
        CALL    TABLE_SET_ENTRY
        PUSH    CS
        POP     ES
        JC      TABLE_FULL_ERR

ADD_TRY_DISK:
        CALL    SYNC_DISK_ADD
        CMP     BYTE PTR [OPT_QUIET],0
        JNE     EXIT_OK
        MOV     DX,OFFSET MSG_UPDATED
        CALL    PRINT_STR
EXIT_OK:
        MOV     AX,4C00h
        INT     21h

ADD_BAD_NAME:
        MOV     DX,OFFSET MSG_BAD_NAME
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

ADD_BAD_VER:
        MOV     DX,OFFSET MSG_BAD_VER
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

TABLE_FULL_ERR:
        MOV     DX,OFFSET MSG_FULL
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

FILE_ERR:
        MOV     DX,OFFSET MSG_NO_FILE
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

; ---------------------------------------------------------------------------
; DELETE entry
; ---------------------------------------------------------------------------
DO_DELETE_CMD:
        CMP     AL,1
        JE      DEL_1
        CMP     AL,2
        JE      DEL_2
        JMP     USAGE_ERR

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
        JC      ADD_BAD_NAME

        MOV     BYTE PTR [FOUND_ANY],0
        MOV     AX,WORD PTR [LIVE_TAB]
        OR      AX,WORD PTR [LIVE_TAB+2]
        JZ      DEL_TRY_DISK
        LES     DI,[LIVE_TAB]
        MOV     CX,[LIVE_CAP]
        CALL    TABLE_DEL_ENTRY
        PUSH    CS
        POP     ES
        JC      DEL_TRY_DISK
        MOV     BYTE PTR [FOUND_ANY],1

DEL_TRY_DISK:
        CALL    SYNC_DISK_DEL
        CMP     BYTE PTR [FOUND_ANY],0
        JE      NOT_FOUND_ERR
        CMP     BYTE PTR [OPT_QUIET],0
        JNE     DEL_EXIT_OK
        MOV     DX,OFFSET MSG_DELETED
        CALL    PRINT_STR
DEL_EXIT_OK:
        MOV     AX,4C00h
        INT     21h

NOT_FOUND_ERR:
        MOV     DX,OFFSET MSG_NOT_FOUND
        CALL    PRINT_STR
        MOV     AX,4C01h
        INT     21h

; ---------------------------------------------------------------------------
; Print all entries in ES:DI
; ---------------------------------------------------------------------------
PRINT_TABLE     PROC    NEAR
        MOV     AL,ES:[DI]
        OR      AL,AL
        JNZ     PT_LOOP
        MOV     DX,OFFSET MSG_EMPTY
        CALL    PRINT_STR
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
        MOV     DL,ES:[DI]
        INC     DI
        MOV     AH,02h
        INT     21h
        DEC     BX
        LOOP    PT_NAME_CH
PT_PAD:
        CMP     BX,0
        JLE     PT_VER
        MOV     DL,' '
        MOV     AH,02h
        INT     21h
        DEC     BX
        JMP     SHORT PT_PAD
PT_VER:
        MOV     AL,ES:[DI]              ; major
        INC     DI
        CALL    PRINT_DEC_BYTE
        MOV     DL,'.'
        MOV     AH,02h
        INT     21h
        MOV     AL,ES:[DI]              ; minor (2 digits)
        INC     DI
        XOR     AH,AH
        MOV     BL,10
        DIV     BL
        PUSH    AX
        MOV     DL,AL
        ADD     DL,'0'
        MOV     AH,02h
        INT     21h
        POP     AX
        MOV     DL,AH
        ADD     DL,'0'
        MOV     AH,02h
        INT     21h
        MOV     DX,OFFSET MSG_CRLF
        CALL    PRINT_STR
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
        MOV     DL,AL
        ADD     DL,'0'
        MOV     AH,02h
        INT     21h
        POP     AX
        MOV     AL,AH
PDB_ONE:
        MOV     DL,AL
        ADD     DL,'0'
        MOV     AH,02h
        INT     21h
        RET
PRINT_DEC_BYTE  ENDP

; ---------------------------------------------------------------------------
; Mutate table at ES:DI (capacity CX): set or delete TARGET_NAME
; ---------------------------------------------------------------------------
TABLE_DEL_ENTRY PROC    NEAR
        MOV     BX,DI                   ; BX = table start
        ADD     CX,DI                   ; CX = table limit
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
        ; Entry starts at ES:DI; next entry starts at ES:SI_NEXT
        MOV     AL,ES:[DI]
        XOR     AH,AH
        MOV     SI,DI
        ADD     SI,AX
        ADD     SI,3
        ; Find end of table (including terminating 0)
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
        ; First delete any existing entry with the same name
        PUSH    DI
        PUSH    CX
        CALL    TABLE_DEL_ENTRY
        POP     CX
        POP     DI
        ; Find terminating 0
        MOV     DX,DI
        ADD     DX,CX                   ; DX = limit
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
        ; Need TARGET_LEN + 4 bytes (len + name + major + minor + trailing 0)
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
RESOLVE_DEFAULT_PATH PROC NEAR
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JNE     RDP_OK
        ; Copy default C:\DOS\SETVER.EXE
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
        MOV     AX,3D00h                ; open read-only
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
        ; SI = offset of EMB_TABLE inside IO_BUF
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
        JC      SDA_RET
        CALL    WRITE_DISK_TABLE
SDA_RET:
        RET
SDA_ERR:
        POP     AX                      ; discard return addr
        JMP     FILE_ERR
SYNC_DISK_ADD   ENDP

SYNC_DISK_DEL   PROC    NEAR
        CALL    RESOLVE_DEFAULT_PATH
        CALL    LOAD_DISK_TABLE
        JNC     SDD_HAVE
        CMP     BYTE PTR [HAS_EXPLICIT_PATH],0
        JNE     SDA_ERR
        RET
SDD_HAVE:
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
        MOV     AX,3D02h                ; open read/write
        MOV     DX,OFFSET FILE_PATH
        INT     21h
        JC      WDT_DONE
        MOV     BX,AX
        XOR     CX,CX
        MOV     DX,[DISK_TAB_OFF]
        MOV     AX,4200h
        INT     21h
        JC      WDT_CLOSE
        MOV     AH,40h
        MOV     CX,1024
        MOV     DX,OFFSET DISK_TAB_BUF
        INT     21h
WDT_CLOSE:
        MOV     AH,3Eh
        INT     21h
WDT_DONE:
        RET
WRITE_DISK_TABLE ENDP

; ---------------------------------------------------------------------------
; Argument parsing helpers
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
        JE      PTL_OK
        CMP     BYTE PTR [SI],'/'
        JE      PTL_SWITCH
        ; Positional token
        MOV     AL,[POS_COUNT]
        CMP     AL,3
        JAE     PTL_ERR
        MOV     DI,OFFSET POS1
        CMP     AL,0
        JE      PTL_COPY_POS
        MOV     DI,OFFSET POS2
        CMP     AL,1
        JE      PTL_COPY_POS
        MOV     DI,OFFSET POS3
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
        JMP     SHORT PTL_NEXT

PTL_SWITCH:
        INC     SI
        MOV     AL,[SI]
        CALL    TO_UPPER
        CMP     AL,'?'
        JNE     PTL_SW_D
        INC     SI
        MOV     BYTE PTR [OPT_HELP],1
        JMP     SHORT PTL_NEXT
PTL_SW_D:
        CMP     AL,'D'
        JNE     PTL_SW_Q
        MOV     BYTE PTR [OPT_DELETE],1
PTL_EAT_SW:
        MOV     AL,[SI]
        OR      AL,AL
        JZ      PTL_NEXT
        CMP     AL,' '
        JBE     PTL_NEXT
        CMP     AL,'/'
        JE      PTL_NEXT
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

VALID_PROG_NAME PROC    NEAR
        XOR     CL,CL
VPN_LP:
        LODSB
        OR      AL,AL
        JZ      VPN_END
        CMP     AL,'\'
        JE      VPN_BAD
        CMP     AL,'/'
        JE      VPN_BAD
        CMP     AL,':'
        JE      VPN_BAD
        INC     CL
        CMP     CL,12
        JA      VPN_BAD
        STOSB
        JMP     SHORT VPN_LP
VPN_END:
        OR      CL,CL
        JZ      VPN_BAD
        MOV     [TARGET_LEN],CL
        CLC
        RET
VPN_BAD:
        STC
        RET
VALID_PROG_NAME ENDP

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
        JZ      PVS_ONE_MINOR           ; e.g. "4.0" -> minor 00
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
        MOV     [TARGET_MIN],AL
        CLC
        RET
PVS_ONE_MINOR:
        MOV     AL,BL
        MOV     CL,10
        MUL     CL
        MOV     [TARGET_MIN],AL
        CLC
        RET
PVS_BAD:
        STC
        RET
PARSE_VER_STR   ENDP

PRINT_STR       PROC    NEAR
        MOV     AH,09h
        INT     21h
        RET
PRINT_STR       ENDP

; ---------------------------------------------------------------------------
; Messages and working buffers
; ---------------------------------------------------------------------------
DEF_SETVER_PATH DB      'C:\DOS\SETVER.EXE',0
STR_SETVER_EXE  DB      'SETVER.EXE',0

MSG_HELP        DB      'Sets the version number that CK-DOS reports to a program.',13,10,13,10
                DB      'Display current version table:  SETVER [drive:path]',13,10
                DB      'Add entry:                      SETVER [drive:path] filename n.nn',13,10
                DB      'Delete entry:                   SETVER [drive:path] filename /DELETE [/QUIET]',13,10,'$'

MSG_SYNTAX      DB      'Invalid parameter. Type SETVER /? for help.',13,10,'$'
MSG_BAD_NAME    DB      'Invalid filename specified.',13,10,'$'
MSG_BAD_VER     DB      'Invalid version number (must be 2.00 through 9.99).',13,10,'$'
MSG_FULL        DB      'Insufficient space in version table for new entry.',13,10,'$'
MSG_NO_FILE     DB      'SETVER.EXE file not found at specified path.',13,10,'$'
MSG_NOT_FOUND   DB      'Specified entry was not found in the version table.',13,10,'$'
MSG_EMPTY       DB      'No entries found in version table.',13,10,'$'
MSG_UPDATED     DB      'Version table successfully updated.',13,10,'$'
MSG_DELETED     DB      'Version table entry successfully deleted.',13,10,'$'
MSG_CRLF        DB      13,10,'$'

PSP_SEG         DW      0
LIVE_TAB        DD      0
LIVE_CAP        DW      0
OPT_HELP        DB      0
OPT_DELETE      DB      0
OPT_QUIET       DB      0
POS_COUNT       DB      0
HAS_EXPLICIT_PATH DB    0
FOUND_ANY       DB      0
TARGET_LEN      DB      0
TARGET_MAJ      DB      0
TARGET_MIN      DB      0
TARGET_NAME     DB      16 DUP (0)
DISK_TAB_OFF    DW      0

CMD_LINE        DB      130 DUP (0)
POS1            DB      64 DUP (0)
POS2            DB      64 DUP (0)
POS3            DB      64 DUP (0)
FILE_PATH       DB      80 DUP (0)
DISK_TAB_BUF    DB      1024 DUP (0)
IO_BUF          DB      4096 DUP (0)

CODE    ENDS

STACK   SEGMENT PARA STACK 'STACK'
        DB      512 DUP (?)
STACK   ENDS

        END     EXE_START
