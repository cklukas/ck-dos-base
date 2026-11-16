; Copyright (c) 2026 C. Klukas
; SPDX-License-Identifier: MIT
;
; HIMEM.SYS — Clean-room Extended Memory Specification (XMS 3.0) Driver
; for CK-DOS 5.0.
;
; Implements:
;   - INT 2Fh AX=4300h (installation check -> AL=80h)
;   - INT 2Fh AX=4310h (driver entry address -> ES:BX)
;   - XMS functions:
;       AH=00h Get XMS Version (3.00)
;       AH=01h/02h Request / Release HMA (FFFF:0010..FFFF:FFFF)
;       AH=03h/04h Global Enable / Disable A20
;       AH=05h/06h Local Enable / Disable A20
;       AH=07h     Query A20 State
;       AH=08h..0Fh (+ 88h/89h) EMB Query, Alloc, Free, Move, Lock, Unlock,
;                  Get Handle Info, Realloc (hooks INT 15h AH=88h)
;       AH=10h/11h Request / Release Upper Memory Blocks (D000h..DFFFh)

.8086

CODE    SEGMENT PARA PUBLIC 'CODE'
        ASSUME  CS:CODE, DS:NOTHING, ES:NOTHING, SS:NOTHING
        ORG     0

; ---------------------------------------------------------------------------
; DOS Character Device Header (offset 0000h)
; ---------------------------------------------------------------------------
DEV_NEXT        DD      -1
DEV_ATTR        DW      8000h
DEV_STRAT       DW      OFFSET DEV_STRATEGY
DEV_INTR        DW      OFFSET DEV_INTERRUPT
DEV_NAME        DB      'XMSXXXX0'

; ---------------------------------------------------------------------------
; Resident State (kept compact so resident footprint is ~1.3 KB)
; ---------------------------------------------------------------------------
REQ_PTR         DD      0
OLD_INT2F       DD      0
OLD_INT15       DD      0

HMA_EXISTS      DB      0
HMA_USED        DB      0
GLOBAL_A20      DB      0
INSTALLED_OK    DB      0
LOCAL_A20_CNT   DW      0
POOL_BASE_KB    DW      1088            ; 1024 KB + 64 KB HMA = 1088 KB
POOL_END_KB     DW      1088            ; end of extended memory in KB

MAX_HANDLES     EQU     32
; Handle entry (8 bytes):
;   +0 BYTE  flags (0=unusedslot, 1=allocated, 2=free_hole)
;   +1 BYTE  lock_count
;   +2 WORD  base_kb
;   +4 WORD  size_kb
;   +6 WORD  reserved
EMB_TABLE       DB      (MAX_HANDLES * 8) DUP (0)

MAX_UMBS        EQU     8
; UMB entry (6 bytes):
;   +0 BYTE  state (0=empty, 1=free, 2=allocated)
;   +1 BYTE  pad
;   +2 WORD  seg_addr
;   +4 WORD  size_paras
UMB_TABLE       DB      (MAX_UMBS * 6) DUP (0)

; 48-byte GDT for INT 15h AH=87h block move
GDT_BUF         DB      48 DUP (0)

; ---------------------------------------------------------------------------
; Strategy & Interrupt (post-init resident stub)
; ---------------------------------------------------------------------------
DEV_STRATEGY    PROC    FAR
        MOV     WORD PTR CS:[REQ_PTR],BX
        MOV     WORD PTR CS:[REQ_PTR+2],ES
        RET
DEV_STRATEGY    ENDP

DEV_INTERRUPT   PROC    FAR
        CMP     BYTE PTR CS:[INSTALLED_OK],0
        JE      GO_DEV_INIT
        PUSH    ES
        PUSH    BX
        LES     BX,CS:[REQ_PTR]
        MOV     WORD PTR ES:[BX+3],0100h
        POP     BX
        POP     ES
        RET
GO_DEV_INIT:
        JMP     DEV_INIT_MAIN
DEV_INTERRUPT   ENDP

; ---------------------------------------------------------------------------
; INT 2Fh Multiplex Handler (AX=4300h / AX=4310h)
; ---------------------------------------------------------------------------
NEW_INT2F       PROC    FAR
        CMP     AX,4300h
        JE      I2F_INST
        CMP     AX,4310h
        JE      I2F_ADDR
        JMP     DWORD PTR CS:[OLD_INT2F]
I2F_INST:
        MOV     AL,80h
        IRET
I2F_ADDR:
        PUSH    CS
        POP     ES
        MOV     BX,OFFSET XMS_ENTRY
        IRET
NEW_INT2F       ENDP

; ---------------------------------------------------------------------------
; INT 15h Hook (claims AH=88h extended memory per XMS specification)
; ---------------------------------------------------------------------------
NEW_INT15       PROC    FAR
        CMP     AH,88h
        JE      I15_88
        JMP     DWORD PTR CS:[OLD_INT15]
I15_88:
        XOR     AX,AX
        PUSH    BP
        MOV     BP,SP
        AND     BYTE PTR [BP+6],NOT 1   ; clear CF on return
        POP     BP
        IRET
NEW_INT15       ENDP

; ---------------------------------------------------------------------------
; XMS Far Call Entry Point (starts with mandatory 5-byte hookable short jump)
; ---------------------------------------------------------------------------
XMS_ENTRY       PROC    FAR
        JMP     SHORT XMS_DISPATCH
        NOP
        NOP
        NOP
XMS_DISPATCH:
        CMP     AH,88h
        JE      GO_QUERY_EMB
        CMP     AH,89h
        JE      GO_ALLOC_EMB
        CMP     AH,11h
        JA      XMS_NOT_IMPL
        PUSH    SI
        PUSH    AX
        MOV     AL,AH
        XOR     AH,AH
        SHL     AX,1
        MOV     SI,AX
        POP     AX
        MOV     SI,CS:[XMS_JMP_TAB+SI]
        MOV     CS:[XMS_JMP_VEC],SI
        POP     SI
        JMP     WORD PTR CS:[XMS_JMP_VEC]

XMS_JMP_VEC     DW      0
        EVEN
XMS_JMP_TAB     DW      OFFSET FN_GET_VER
                DW      OFFSET FN_REQ_HMA
                DW      OFFSET FN_REL_HMA
                DW      OFFSET FN_GLOB_EN_A20
                DW      OFFSET FN_GLOB_DIS_A20
                DW      OFFSET FN_LOC_EN_A20
                DW      OFFSET FN_LOC_DIS_A20
                DW      OFFSET FN_QUERY_A20
                DW      OFFSET FN_QUERY_EMB
                DW      OFFSET FN_ALLOC_EMB
                DW      OFFSET FN_FREE_EMB
                DW      OFFSET FN_MOVE_EMB
                DW      OFFSET FN_LOCK_EMB
                DW      OFFSET FN_UNLOCK_EMB
                DW      OFFSET FN_INFO_EMB
                DW      OFFSET FN_REALLOC_EMB
                DW      OFFSET FN_REQ_UMB
                DW      OFFSET FN_REL_UMB

XMS_NOT_IMPL:
        XOR     AX,AX
        MOV     BL,80h                  ; function not implemented
        RET

GO_QUERY_EMB:   JMP     FN_QUERY_EMB
GO_ALLOC_EMB:   JMP     FN_ALLOC_EMB

; --- AH=00h: Get XMS Version ---
FN_GET_VER:
        MOV     AX,0300h                ; XMS 3.00
        MOV     BX,030Ah                ; internal driver version 3.10
        XOR     DX,DX
        MOV     DL,CS:[HMA_EXISTS]
        RET

; --- AH=01h: Request HMA ---
FN_REQ_HMA:
        CMP     BYTE PTR CS:[HMA_EXISTS],0
        JE      HMA_NO_EXIST
        CMP     BYTE PTR CS:[HMA_USED],0
        JNE     HMA_IN_USE
        MOV     BYTE PTR CS:[HMA_USED],1
        MOV     AX,1
        XOR     BL,BL
        RET
HMA_NO_EXIST:
        XOR     AX,AX
        MOV     BL,90h
        RET
HMA_IN_USE:
        XOR     AX,AX
        MOV     BL,91h
        RET

; --- AH=02h: Release HMA ---
FN_REL_HMA:
        CMP     BYTE PTR CS:[HMA_USED],0
        JE      HMA_NOT_ALLOC
        MOV     BYTE PTR CS:[HMA_USED],0
        MOV     AX,1
        XOR     BL,BL
        RET
HMA_NOT_ALLOC:
        XOR     AX,AX
        MOV     BL,93h
        RET

; --- AH=03h..07h: A20 Management ---
FN_GLOB_EN_A20:
        MOV     BYTE PTR CS:[GLOBAL_A20],1
        CALL    ENABLE_A20
        MOV     AX,1
        XOR     BL,BL
        RET

FN_GLOB_DIS_A20:
        MOV     BYTE PTR CS:[GLOBAL_A20],0
        CMP     WORD PTR CS:[LOCAL_A20_CNT],0
        JNE     A20_STILL_EN
        CALL    DISABLE_A20
        MOV     AX,1
        XOR     BL,BL
        RET
A20_STILL_EN:
        XOR     AX,AX
        MOV     BL,94h
        RET

FN_LOC_EN_A20:
        CMP     WORD PTR CS:[LOCAL_A20_CNT],0
        JNE     LOC_EN_INC
        CALL    ENABLE_A20
LOC_EN_INC:
        INC     WORD PTR CS:[LOCAL_A20_CNT]
        MOV     AX,1
        XOR     BL,BL
        RET

FN_LOC_DIS_A20:
        CMP     WORD PTR CS:[LOCAL_A20_CNT],0
        JE      LOC_DIS_DONE
        DEC     WORD PTR CS:[LOCAL_A20_CNT]
        JNZ     LOC_DIS_DONE
        CMP     BYTE PTR CS:[GLOBAL_A20],0
        JNE     LOC_DIS_DONE
        CALL    DISABLE_A20
LOC_DIS_DONE:
        MOV     AX,1
        XOR     BL,BL
        RET

FN_QUERY_A20:
        CALL    QUERY_A20
        XOR     BL,BL
        RET

; --- AH=08h / 88h: Query Free Extended Memory ---
FN_QUERY_EMB:
        PUSH    CX
        PUSH    SI
        XOR     AX,AX                   ; AX = max free block (KB)
        XOR     DX,DX                   ; DX = total free (KB)
        MOV     SI,OFFSET EMB_TABLE
        MOV     CX,MAX_HANDLES
QEMB_LP:
        CMP     BYTE PTR CS:[SI],2      ; 2 = free hole
        JNE     QEMB_NX
        ADD     DX,CS:[SI+4]
        CMP     CS:[SI+4],AX
        JBE     QEMB_NX
        MOV     AX,CS:[SI+4]
QEMB_NX:
        ADD     SI,8
        LOOP    QEMB_LP
        XOR     BL,BL
        OR      DX,DX
        JNZ     QEMB_OK
        MOV     BL,0A0h                 ; all extended memory allocated
QEMB_OK:
        XOR     CX,CX
        POP     SI
        POP     CX
        RET

; --- AH=09h / 89h: Allocate EMB (DX = size in KB) ---
FN_ALLOC_EMB:
        PUSH    CX
        PUSH    SI
        PUSH    DI
        OR      DX,DX
        JZ      AEMB_ZERO
        ; Find first free hole (flag=2) with size_kb >= DX
        MOV     SI,OFFSET EMB_TABLE
        MOV     CX,MAX_HANDLES
AEMB_FIND:
        CMP     BYTE PTR CS:[SI],2
        JNE     AEMB_FNX
        CMP     CS:[SI+4],DX
        JAE     AEMB_GOT_HOLE
AEMB_FNX:
        ADD     SI,8
        LOOP    AEMB_FIND
        POP     DI
        POP     SI
        POP     CX
        XOR     AX,AX
        MOV     BL,0A0h                 ; not enough extended memory
        RET

AEMB_ZERO:
        ; Allocate an unused slot (flag=0) with size 0
        CALL    FIND_UNUSED_SLOT
        JC      AEMB_NO_HANDLES
        MOV     BYTE PTR CS:[DI],1
        MOV     BYTE PTR CS:[DI+1],0
        MOV     WORD PTR CS:[DI+2],0
        MOV     WORD PTR CS:[DI+4],0
        MOV     DX,DI
        SUB     DX,OFFSET EMB_TABLE
        MOV     CL,3
        SHR     DX,CL
        INC     DX                      ; 1-based handle
        POP     DI
        POP     SI
        POP     CX
        MOV     AX,1
        XOR     BL,BL
        RET

AEMB_GOT_HOLE:
        JE      AEMB_EXACT
        ; Need an unused slot to split off the remainder hole
        CALL    FIND_UNUSED_SLOT
        JC      AEMB_NO_HANDLES
        MOV     AX,CS:[SI+2]
        ADD     AX,DX                   ; remainder base_kb
        MOV     CX,CS:[SI+4]
        SUB     CX,DX                   ; remainder size_kb
        MOV     BYTE PTR CS:[DI],2      ; free hole
        MOV     BYTE PTR CS:[DI+1],0
        MOV     CS:[DI+2],AX
        MOV     CS:[DI+4],CX
        MOV     CS:[SI+4],DX
AEMB_EXACT:
        MOV     BYTE PTR CS:[SI],1      ; allocated
        MOV     BYTE PTR CS:[SI+1],0    ; lock_count = 0
        MOV     DX,SI
        SUB     DX,OFFSET EMB_TABLE
        MOV     CL,3
        SHR     DX,CL
        INC     DX                      ; 1-based handle
        POP     DI
        POP     SI
        POP     CX
        MOV     AX,1
        XOR     BL,BL
        RET

AEMB_NO_HANDLES:
        POP     DI
        POP     SI
        POP     CX
        XOR     AX,AX
        MOV     BL,0A1h                 ; all XMS handles allocated
        RET

; --- AH=0Ah: Free EMB (DX = handle) ---
FN_FREE_EMB:
        PUSH    SI
        CALL    LOOKUP_HANDLE
        JC      FEMB_BAD
        CMP     BYTE PTR CS:[SI+1],0
        JNE     FEMB_LOCKED
        CMP     WORD PTR CS:[SI+4],0
        JE      FEMB_ZERO
        MOV     BYTE PTR CS:[SI],2      ; convert back to free hole
        CALL    COALESCE_EMB
        JMP     SHORT FEMB_OK
FEMB_ZERO:
        MOV     BYTE PTR CS:[SI],0
FEMB_OK:
        POP     SI
        MOV     AX,1
        XOR     BL,BL
        RET
FEMB_BAD:
        POP     SI
        XOR     AX,AX
        MOV     BL,0A2h
        RET
FEMB_LOCKED:
        POP     SI
        XOR     AX,AX
        MOV     BL,0ABh
        RET

; --- AH=0Ch: Lock EMB (DX = handle -> DX:BX = 32-bit linear addr) ---
FN_LOCK_EMB:
        PUSH    SI
        PUSH    CX
        CALL    LOOKUP_HANDLE
        JC      LK_BAD
        CMP     BYTE PTR CS:[SI+1],0FFh
        JE      LK_OVF
        INC     BYTE PTR CS:[SI+1]
        MOV     AX,CS:[SI+2]            ; base_kb
        ; 32-bit addr = base_kb * 1024 -> DX = base_kb >> 6, BX = (base_kb & 3Fh) << 10
        MOV     BX,AX
        MOV     CL,10
        SHL     BX,CL
        MOV     DX,AX
        MOV     CL,6
        SHR     DX,CL
        POP     CX
        POP     SI
        MOV     AX,1
        RET
LK_BAD:
        POP     CX
        POP     SI
        XOR     AX,AX
        MOV     BL,0A2h
        RET
LK_OVF:
        POP     CX
        POP     SI
        XOR     AX,AX
        MOV     BL,0ACh
        RET

; --- AH=0Dh: Unlock EMB (DX = handle) ---
FN_UNLOCK_EMB:
        PUSH    SI
        CALL    LOOKUP_HANDLE
        JC      ULK_BAD
        CMP     BYTE PTR CS:[SI+1],0
        JE      ULK_NOT_LOCKED
        DEC     BYTE PTR CS:[SI+1]
        POP     SI
        MOV     AX,1
        XOR     BL,BL
        RET
ULK_BAD:
        POP     SI
        XOR     AX,AX
        MOV     BL,0A2h
        RET
ULK_NOT_LOCKED:
        POP     SI
        XOR     AX,AX
        MOV     BL,0AAh
        RET

; --- AH=0Eh: Get EMB Handle Info (DX = handle) ---
FN_INFO_EMB:
        PUSH    SI
        PUSH    CX
        CALL    LOOKUP_HANDLE
        JC      INF_BAD
        MOV     BH,CS:[SI+1]            ; lock count
        MOV     DX,CS:[SI+4]            ; size_kb
        ; Count free slots (flag=0)
        XOR     BL,BL
        MOV     SI,OFFSET EMB_TABLE
        MOV     CX,MAX_HANDLES
INF_CNT:
        CMP     BYTE PTR CS:[SI],0
        JNE     INF_NX
        INC     BL
INF_NX:
        ADD     SI,8
        LOOP    INF_CNT
        POP     CX
        POP     SI
        MOV     AX,1
        RET
INF_BAD:
        POP     CX
        POP     SI
        XOR     AX,AX
        MOV     BL,0A2h
        RET

; --- AH=0Fh: Reallocate EMB (DX = handle, BX = new size KB) ---
FN_REALLOC_EMB:
        PUSH    SI
        PUSH    DI
        PUSH    CX
        CALL    LOOKUP_HANDLE
        JC      RA_BAD
        CMP     BYTE PTR CS:[SI+1],0
        JNE     RA_LOCKED
        CMP     BX,CS:[SI+4]
        JE      RA_OK
        JA      RA_FAIL_MEM             ; shrinking supported in-place; growing if adjacent free
        ; Shrink in-place
        MOV     CX,CS:[SI+4]
        SUB     CX,BX
        CALL    FIND_UNUSED_SLOT
        JC      RA_FAIL_MEM
        MOV     AX,CS:[SI+2]
        ADD     AX,BX
        MOV     BYTE PTR CS:[DI],2
        MOV     BYTE PTR CS:[DI+1],0
        MOV     CS:[DI+2],AX
        MOV     CS:[DI+4],CX
        MOV     CS:[SI+4],BX
        CALL    COALESCE_EMB
RA_OK:
        POP     CX
        POP     DI
        POP     SI
        MOV     AX,1
        XOR     BL,BL
        RET
RA_BAD:
        POP     CX
        POP     DI
        POP     SI
        XOR     AX,AX
        MOV     BL,0A2h
        RET
RA_LOCKED:
        POP     CX
        POP     DI
        POP     SI
        XOR     AX,AX
        MOV     BL,0ABh
        RET
RA_FAIL_MEM:
        POP     CX
        POP     DI
        POP     SI
        XOR     AX,AX
        MOV     BL,0A0h
        RET

; --- AH=0Bh: Move Extended Memory Block (DS:SI -> XMS_Move) ---
;   +0  DWORD Length (even)
;   +4  WORD  SourceHandle
;   +6  DWORD SourceOffset
;   +10 WORD  DestHandle
;   +12 DWORD DestOffset
FN_MOVE_EMB:
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    BP
        PUSH    ES

        TEST    BYTE PTR DS:[SI],1
        JZ      MV_EVEN_OK
        JMP     MV_ODD_LEN
MV_EVEN_OK:
        MOV     CX,DS:[SI]              ; low word of length
        MOV     DX,DS:[SI+2]            ; high word of length
        MOV     AX,CX
        OR      AX,DX
        JNZ     MV_NONZERO
        JMP     MV_DONE_OK
MV_NONZERO:
        ; Resolve source 32-bit physical address -> DI:BX_SRC
        MOV     AX,DS:[SI+4]            ; SourceHandle
        MOV     BX,DS:[SI+6]            ; SourceOffset low
        MOV     BP,DS:[SI+8]            ; SourceOffset high
        CALL    RESOLVE_XMS_ADDR        ; in: AX=handle, BP:BX=offset -> out: BP:BX=32-bit phys
        JNC     MV_SRC_OK
        JMP     MV_SRC_ERR
MV_SRC_OK:
        MOV     CS:[MV_SRC_LO],BX
        MOV     CS:[MV_SRC_HI],BP

        ; Resolve dest 32-bit physical address
        MOV     AX,DS:[SI+10]           ; DestHandle
        MOV     BX,DS:[SI+12]           ; DestOffset low
        MOV     BP,DS:[SI+14]           ; DestOffset high
        CALL    RESOLVE_XMS_ADDR
        JNC     MV_DST_OK
        JMP     MV_DST_ERR
MV_DST_OK:
        MOV     CS:[MV_DST_LO],BX
        MOV     CS:[MV_DST_HI],BP

        MOV     AX,DS:[SI]
        MOV     CS:[MV_REM_LO],AX
        MOV     AX,DS:[SI+2]
        MOV     CS:[MV_REM_HI],AX

MV_CHUNK_LP:
        MOV     AX,CS:[MV_REM_LO]
        MOV     DX,CS:[MV_REM_HI]
        MOV     BX,AX
        OR      BX,DX
        JNZ     MV_MORE_CHUNKS
        JMP     MV_FINISH
MV_MORE_CHUNKS:
        ; Chunk bytes = min(rem, 8000h)
        MOV     CX,8000h
        OR      DX,DX
        JNZ     MV_GOT_CHUNK
        CMP     AX,CX
        JAE     MV_GOT_CHUNK
        MOV     CX,AX
MV_GOT_CHUNK:
        ; Fill 48-byte INT 15h AH=87h GDT at CS:GDT_BUF
        PUSH    CS
        POP     ES
        MOV     DI,OFFSET GDT_BUF
        PUSH    CX
        XOR     AX,AX
        MOV     CX,24
        CLD
        REP     STOSW
        POP     CX

        ; Source descriptor at GDT_BUF+16
        MOV     AX,CX
        DEC     AX
        MOV     WORD PTR CS:[GDT_BUF+16],AX     ; limit
        MOV     AX,CS:[MV_SRC_LO]
        MOV     WORD PTR CS:[GDT_BUF+18],AX     ; base 0..15
        MOV     AX,CS:[MV_SRC_HI]
        MOV     BYTE PTR CS:[GDT_BUF+20],AL     ; base 16..23
        MOV     BYTE PTR CS:[GDT_BUF+21],93h    ; present, data R/W
        MOV     BYTE PTR CS:[GDT_BUF+23],AH     ; base 24..31

        ; Dest descriptor at GDT_BUF+24
        MOV     AX,CX
        DEC     AX
        MOV     WORD PTR CS:[GDT_BUF+24],AX     ; limit
        MOV     AX,CS:[MV_DST_LO]
        MOV     WORD PTR CS:[GDT_BUF+26],AX     ; base 0..15
        MOV     AX,CS:[MV_DST_HI]
        MOV     BYTE PTR CS:[GDT_BUF+28],AL     ; base 16..23
        MOV     BYTE PTR CS:[GDT_BUF+29],93h    ; present, data R/W
        MOV     BYTE PTR CS:[GDT_BUF+31],AH     ; base 24..31

        PUSH    CX
        SHR     CX,1                    ; words to move
        MOV     SI,OFFSET GDT_BUF
        MOV     AH,87h
        INT     15h
        POP     CX
        JC      MV_BIOS_ERR
        OR      AH,AH
        JNZ     MV_BIOS_ERR

        ; Re-assert A20 if it was enabled before BIOS INT 15h AH=87h
        MOV     AL,CS:[GLOBAL_A20]
        OR      AX,CS:[LOCAL_A20_CNT]
        JZ      MV_ADV
        CALL    ENABLE_A20

MV_ADV:
        ADD     CS:[MV_SRC_LO],CX
        ADC     WORD PTR CS:[MV_SRC_HI],0
        ADD     CS:[MV_DST_LO],CX
        ADC     WORD PTR CS:[MV_DST_HI],0
        SUB     CS:[MV_REM_LO],CX
        SBB     WORD PTR CS:[MV_REM_HI],0
        JMP     MV_CHUNK_LP

MV_FINISH:
MV_DONE_OK:
        POP     ES
        POP     BP
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        MOV     AX,1
        XOR     BL,BL
        RET

MV_ODD_LEN:
        MOV     AL,0A7h
        JMP     SHORT MV_RET_ERR
MV_SRC_ERR:
        MOV     AL,0A3h
        JMP     SHORT MV_RET_ERR
MV_DST_ERR:
        MOV     AL,0A5h
        JMP     SHORT MV_RET_ERR
MV_BIOS_ERR:
        MOV     AL,0A8h
MV_RET_ERR:
        POP     ES
        POP     BP
        POP     DI
        POP     SI
        POP     DX
        POP     CX
        POP     BX
        MOV     BL,AL
        XOR     AX,AX
        RET

MV_SRC_LO       DW      0
MV_SRC_HI       DW      0
MV_DST_LO       DW      0
MV_DST_HI       DW      0
MV_REM_LO       DW      0
MV_REM_HI       DW      0

; --- AH=10h: Request UMB (DX = size in paragraphs) ---
FN_REQ_UMB:
        PUSH    CX
        PUSH    SI
        PUSH    DI
        XOR     AX,AX                   ; AX = largest free UMB seen
        XOR     DI,DI                   ; DI = first matching slot
        MOV     SI,OFFSET UMB_TABLE
        MOV     CX,MAX_UMBS
RUMB_SCAN:
        CMP     BYTE PTR CS:[SI],1      ; 1 = free UMB
        JNE     RUMB_NX
        CMP     CS:[SI+4],AX
        JBE     RUMB_CHK_FIT
        MOV     AX,CS:[SI+4]
RUMB_CHK_FIT:
        OR      DI,DI
        JNZ     RUMB_NX
        CMP     CS:[SI+4],DX
        JB      RUMB_NX
        MOV     DI,SI
RUMB_NX:
        ADD     SI,6
        LOOP    RUMB_SCAN

        OR      DI,DI
        JZ      RUMB_FAIL
        ; Split UMB if remainder >= 2 paras and a slot is empty
        MOV     CX,CS:[DI+4]
        SUB     CX,DX
        CMP     CX,2
        JB      RUMB_WHOLE
        MOV     SI,OFFSET UMB_TABLE
        PUSH    AX
        MOV     AX,MAX_UMBS
RUMB_SLOT:
        CMP     BYTE PTR CS:[SI],0
        JE      RUMB_DO_SPLIT
        ADD     SI,6
        DEC     AX
        JNZ     RUMB_SLOT
        POP     AX
        JMP     SHORT RUMB_WHOLE

RUMB_DO_SPLIT:
        POP     AX
        MOV     AX,CS:[DI+2]
        ADD     AX,DX
        MOV     BYTE PTR CS:[SI],1      ; free remainder UMB
        MOV     CS:[SI+2],AX
        MOV     CS:[SI+4],CX
        MOV     CS:[DI+4],DX

RUMB_WHOLE:
        MOV     BYTE PTR CS:[DI],2      ; allocated
        MOV     BX,CS:[DI+2]            ; segment
        MOV     DX,CS:[DI+4]            ; actual size in paragraphs
        POP     DI
        POP     SI
        POP     CX
        MOV     AX,1
        RET

RUMB_FAIL:
        MOV     DX,AX                   ; DX = largest available UMB (0 if none)
        MOV     BL,0B0h                 ; smaller UMB available
        OR      AX,AX
        JNZ     RUMB_RET0
        MOV     BL,0B1h                 ; no UMBs available
RUMB_RET0:
        XOR     AX,AX
        POP     DI
        POP     SI
        POP     CX
        RET

; --- AH=11h: Release UMB (DX = segment) ---
FN_REL_UMB:
        PUSH    CX
        PUSH    SI
        MOV     SI,OFFSET UMB_TABLE
        MOV     CX,MAX_UMBS
FUMB_LP:
        CMP     BYTE PTR CS:[SI],2
        JNE     FUMB_NX
        CMP     CS:[SI+2],DX
        JE      FUMB_FOUND
FUMB_NX:
        ADD     SI,6
        LOOP    FUMB_LP
        POP     SI
        POP     CX
        XOR     AX,AX
        MOV     BL,0B2h
        RET
FUMB_FOUND:
        MOV     BYTE PTR CS:[SI],1
        POP     SI
        POP     CX
        MOV     AX,1
        XOR     BL,BL
        RET
XMS_ENTRY       ENDP

; ---------------------------------------------------------------------------
; Resident Helpers (EMB & A20)
; ---------------------------------------------------------------------------

; Input: DX = 1-based handle -> Output: CS:SI -> slot, CF=0 valid, CF=1 invalid
LOOKUP_HANDLE   PROC    NEAR
        OR      DX,DX
        JZ      LH_ERR
        CMP     DX,MAX_HANDLES
        JA      LH_ERR
        MOV     SI,DX
        DEC     SI
        SHL     SI,1
        SHL     SI,1
        SHL     SI,1
        ADD     SI,OFFSET EMB_TABLE
        CMP     BYTE PTR CS:[SI],1
        JNE     LH_ERR
        CLC
        RET
LH_ERR:
        STC
        RET
LOOKUP_HANDLE   ENDP

; Output: CS:DI -> unused slot (flag=0), CF=0 found, CF=1 full
FIND_UNUSED_SLOT PROC   NEAR
        PUSH    CX
        MOV     DI,OFFSET EMB_TABLE
        MOV     CX,MAX_HANDLES
FUS_LP:
        CMP     BYTE PTR CS:[DI],0
        JE      FUS_OK
        ADD     DI,8
        LOOP    FUS_LP
        POP     CX
        STC
        RET
FUS_OK:
        POP     CX
        CLC
        RET
FIND_UNUSED_SLOT ENDP

; Merge adjacent free EMB holes (flag=2)
COALESCE_EMB    PROC    NEAR
        PUSH    AX
        PUSH    CX
        PUSH    SI
        PUSH    DI
COAL_RESTART:
        MOV     SI,OFFSET EMB_TABLE
        MOV     CX,MAX_HANDLES
COAL_OUTER:
        CMP     BYTE PTR CS:[SI],2
        JNE     COAL_ONX
        MOV     AX,CS:[SI+2]
        ADD     AX,CS:[SI+4]            ; end_kb of hole SI
        MOV     DI,OFFSET EMB_TABLE
        PUSH    CX
        MOV     CX,MAX_HANDLES
COAL_INNER:
        CMP     DI,SI
        JE      COAL_INX
        CMP     BYTE PTR CS:[DI],2
        JNE     COAL_INX
        CMP     CS:[DI+2],AX
        JE      COAL_MERGE
COAL_INX:
        ADD     DI,8
        LOOP    COAL_INNER
        POP     CX
COAL_ONX:
        ADD     SI,8
        LOOP    COAL_OUTER
        POP     DI
        POP     SI
        POP     CX
        POP     AX
        RET
COAL_MERGE:
        POP     CX
        MOV     AX,CS:[DI+4]
        ADD     CS:[SI+4],AX
        MOV     BYTE PTR CS:[DI],0
        JMP     SHORT COAL_RESTART
COALESCE_EMB    ENDP

; Input: AX = handle (0 = real-mode seg:off in BP:BX), BP:BX = offset
; Output: BP:BX = 32-bit physical address, CF=0 ok, CF=1 invalid
RESOLVE_XMS_ADDR PROC   NEAR
        PUSH    CX
        PUSH    DX
        PUSH    SI
        OR      AX,AX
        JNZ     RXA_EMB
        ; Handle 0: BP = segment, BX = offset -> 32-bit = (BP << 4) + BX
        MOV     DX,BP
        MOV     CL,12
        SHR     DX,CL                   ; high 4 bits
        MOV     CL,4
        SHL     BP,CL
        ADD     BX,BP
        ADC     DX,0
        MOV     BP,DX
        POP     SI
        POP     DX
        POP     CX
        CLC
        RET
RXA_EMB:
        MOV     DX,AX
        CALL    LOOKUP_HANDLE
        JC      RXA_BAD
        MOV     AX,CS:[SI+2]            ; base_kb
        MOV     DX,AX
        MOV     CL,6
        SHR     DX,CL
        MOV     CL,10
        SHL     AX,CL
        ADD     BX,AX
        ADC     BP,DX
        POP     SI
        POP     DX
        POP     CX
        CLC
        RET
RXA_BAD:
        POP     SI
        POP     DX
        POP     CX
        STC
        RET
RESOLVE_XMS_ADDR ENDP

; Non-destructive hardware A20 query: returns AX=1 if A20 enabled, AX=0 if wrapped
QUERY_A20       PROC    NEAR
        PUSH    DS
        PUSH    ES
        PUSH    CX
        PUSH    SI
        PUSH    DI
        PUSHF
        CLI
        XOR     AX,AX
        MOV     DS,AX
        MOV     AX,0FFFFh
        MOV     ES,AX
        MOV     SI,0080h                ; 0000:0080
        MOV     DI,0090h                ; FFFF:0090 -> 100080h (or 000080h if wrapped)
        MOV     AX,DS:[SI]
        CMP     AX,ES:[DI]
        JNE     QA20_ON
        ; Temporarily flip DS:[SI] under CLI to prove whether ES:[DI] aliases it
        NOT     WORD PTR DS:[SI]
        MOV     CX,ES:[DI]
        MOV     DS:[SI],AX              ; restore immediately
        NOT     AX
        CMP     CX,AX
        JNE     QA20_ON
        POPF
        XOR     AX,AX
        JMP     SHORT QA20_EXIT
QA20_ON:
        POPF
        MOV     AX,1
QA20_EXIT:
        POP     DI
        POP     SI
        POP     CX
        POP     ES
        POP     DS
        RET
QUERY_A20       ENDP

ENABLE_A20      PROC    NEAR
        PUSH    AX
        CALL    QUERY_A20
        OR      AX,AX
        JNZ     EA20_DONE
        ; 1. Try BIOS INT 15h AX=2401h
        MOV     AX,2401h
        INT     15h
        CALL    QUERY_A20
        OR      AX,AX
        JNZ     EA20_DONE
        ; 2. PS/2 System Control Port A (92h), never setting bit 0 (reset)!
        IN      AL,92h
        OR      AL,02h
        AND     AL,0FEh
        OUT     92h,AL
        CALL    QUERY_A20
        OR      AX,AX
        JNZ     EA20_DONE
        ; 3. 8042 Keyboard Controller fallback (DFh = A20 on)
        MOV     AH,0DFh
        CALL    KBC_SET_A20
EA20_DONE:
        POP     AX
        RET
ENABLE_A20      ENDP

DISABLE_A20     PROC    NEAR
        PUSH    AX
        CALL    QUERY_A20
        OR      AX,AX
        JZ      DA20_DONE
        ; 1. Try BIOS INT 15h AX=2400h
        MOV     AX,2400h
        INT     15h
        CALL    QUERY_A20
        OR      AX,AX
        JZ      DA20_DONE
        ; 2. PS/2 System Control Port A (92h)
        IN      AL,92h
        AND     AL,0FCh                 ; clear bit 1 (A20) and bit 0 (reset)
        OUT     92h,AL
        CALL    QUERY_A20
        OR      AX,AX
        JZ      DA20_DONE
        ; 3. 8042 Keyboard Controller fallback (DDh = A20 off)
        MOV     AH,0DDh
        CALL    KBC_SET_A20
DA20_DONE:
        POP     AX
        RET
DISABLE_A20     ENDP

KBC_SET_A20     PROC    NEAR
        PUSH    CX
        PUSHF
        CLI
        CALL    KBC_WAIT_IBF
        MOV     AL,0D1h
        OUT     64h,AL
        CALL    KBC_WAIT_IBF
        MOV     AL,AH
        OUT     60h,AL
        CALL    KBC_WAIT_IBF
        MOV     AL,0FFh
        OUT     64h,AL
        CALL    KBC_WAIT_IBF
        POPF
        POP     CX
        RET
KBC_SET_A20     ENDP

KBC_WAIT_IBF    PROC    NEAR
        XOR     CX,CX
KW_LP:
        IN      AL,64h
        TEST    AL,02h
        LOOPNZ  KW_LP
        RET
KBC_WAIT_IBF    ENDP

        EVEN
RESIDENT_END    LABEL   BYTE

; ===========================================================================
; Discardable Initialization Section (freed on exit from DEVINIT)
; ===========================================================================
OPT_NOUMB       DB      0

MSG_BANNER      DB      'HIMEM: Clean-Room CK-DOS XMS Driver, Version 3.10',13,10,'$'
MSG_INSTALLED   DB      'Extended Memory Manager installed (64K HMA available).',13,10,'$'
MSG_NO_EXT      DB      'HIMEM: No extended memory found; driver not installed.',13,10,'$'

DEV_INIT_MAIN   PROC    FAR
        PUSH    AX
        PUSH    BX
        PUSH    CX
        PUSH    DX
        PUSH    SI
        PUSH    DI
        PUSH    BP
        PUSH    DS
        PUSH    ES

        PUSH    CS
        POP     DS
        ASSUME  DS:CODE

        MOV     DX,OFFSET MSG_BANNER
        MOV     AH,09h
        INT     21h

        ; Parse CONFIG.SYS device line at REQ_PTR+18 for /NOUMB
        LES     DI,CS:[REQ_PTR]
        LDS     SI,ES:[DI+18]
        CALL    SCAN_CMD_SWITCHES
        PUSH    CS
        POP     DS

        ; Query extended memory size (KB above 1024 KB) via INT 15h AH=88h
        CLC
        MOV     AH,88h
        INT     15h
        JNC     INIT_CHK_64
        JMP     INIT_NO_EXT
INIT_CHK_64:
        CMP     AX,64
        JAE     INIT_HAS_EXT
        JMP     INIT_NO_EXT
INIT_HAS_EXT:

        ; AX = extended memory in KB above 1 MB
        MOV     BYTE PTR [HMA_EXISTS],1
        MOV     BYTE PTR [HMA_USED],0
        ADD     AX,1024                 ; end_kb
        MOV     [POOL_END_KB],AX
        SUB     AX,1088                 ; EMB pool above 1088 KB
        JZ      INIT_SKIP_EMB_HOLE
        MOV     BYTE PTR [EMB_TABLE],2  ; free hole
        MOV     BYTE PTR [EMB_TABLE+1],0
        MOV     WORD PTR [EMB_TABLE+2],1088
        MOV     WORD PTR [EMB_TABLE+4],AX
INIT_SKIP_EMB_HOLE:

        ; Discover safe Upper Memory Blocks in D000h..DFFFh unless /NOUMB
        CMP     BYTE PTR [OPT_NOUMB],0
        JNE     INIT_HOOK_VECS
        CALL    DISCOVER_UMBS

INIT_HOOK_VECS:
        ; Hook INT 2Fh and INT 15h
        MOV     AX,352Fh
        INT     21h
        MOV     WORD PTR [OLD_INT2F],BX
        MOV     WORD PTR [OLD_INT2F+2],ES
        MOV     DX,OFFSET NEW_INT2F
        MOV     AX,252Fh
        INT     21h

        MOV     AX,3515h
        INT     21h
        MOV     WORD PTR [OLD_INT15],BX
        MOV     WORD PTR [OLD_INT15+2],ES
        MOV     DX,OFFSET NEW_INT15
        MOV     AX,2515h
        INT     21h

        MOV     BYTE PTR [INSTALLED_OK],1
        MOV     DX,OFFSET MSG_INSTALLED
        MOV     AH,09h
        INT     21h

        LES     DI,CS:[REQ_PTR]
        MOV     WORD PTR ES:[DI+14],OFFSET RESIDENT_END
        MOV     WORD PTR ES:[DI+16],CS
        MOV     WORD PTR ES:[DI+3],0100h
        JMP     SHORT INIT_EXIT

INIT_NO_EXT:
        PUSH    CS
        POP     DS
        MOV     DX,OFFSET MSG_NO_EXT
        MOV     AH,09h
        INT     21h
        LES     DI,CS:[REQ_PTR]
        MOV     WORD PTR ES:[DI+14],0
        MOV     WORD PTR ES:[DI+16],CS
        MOV     WORD PTR ES:[DI+3],0100h

INIT_EXIT:
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
DEV_INIT_MAIN   ENDP

SCAN_CMD_SWITCHES PROC  NEAR
        ASSUME  DS:NOTHING
        MOV     CX,128
SCS_LP:
        LODSB
        CMP     AL,13
        JE      SCS_DONE
        CMP     AL,10
        JE      SCS_DONE
        OR      AL,AL
        JZ      SCS_DONE
        CMP     AL,'/'
        JNE     SCS_NX
        ; Check /NOUMB (case-insensitive)
        MOV     AL,DS:[SI]
        AND     AL,0DFh
        CMP     AL,'N'
        JNE     SCS_NX
        MOV     AL,DS:[SI+1]
        AND     AL,0DFh
        CMP     AL,'O'
        JNE     SCS_NX
        MOV     AL,DS:[SI+2]
        AND     AL,0DFh
        CMP     AL,'U'
        JNE     SCS_NX
        MOV     BYTE PTR CS:[OPT_NOUMB],1
SCS_NX:
        LOOP    SCS_LP
SCS_DONE:
        RET
SCAN_CMD_SWITCHES ENDP

; ---------------------------------------------------------------------------
; Safe Upper Memory Discovery in D000h..DFFFh (64 KB ISA UMB window):
;   1. On 386+ with Intel i440FX host bridge (8086:1237), enables PAM3/PAM4
;      (D000h..DFFFh R/W shadow RAM) if no option ROM 55AAh is present there.
;   2. Scans D000h..DFFFh in 2 KB (0080h para) steps, skipping any option ROM
;      and claiming only clean (all-00h or all-FFh) read/write RAM pages.
; ---------------------------------------------------------------------------
DISCOVER_UMBS   PROC    NEAR
        ASSUME  DS:CODE
        ; First check if any Option ROM (55AAh) already exists in D000h..DFFFh
        MOV     BX,0D000h
DU_PRE_ROM:
        MOV     ES,BX
        CMP     WORD PTR ES:[0],0AA55h
        JE      DU_SKIP_PAM
        ADD     BX,0080h
        CMP     BX,0E000h
        JB      DU_PRE_ROM

        ; Check if CPU is 386+ (IOPL bits 12..14 changeable in FLAGS)
        PUSHF
        PUSHF
        POP     AX
        OR      AX,7000h
        PUSH    AX
        POPF
        PUSHF
        POP     AX
        POPF
        TEST    AX,7000h
        JZ      DU_SCAN_PAGES

        ; 386+ detected: read PCI Bus 0 Dev 0 Func 0 Vendor/Device (80000000h)
        MOV     DX,0CF8h
        DB      66h,0B8h,00h,00h,00h,80h ; MOV EAX, 80000000h
        DB      66h,0EFh                 ; OUT DX, EAX
        MOV     DX,0CFCh
        DB      66h,0EDh                 ; IN  EAX, DX
        CMP     AX,8086h                 ; Intel Corp.
        JNE     DU_SCAN_PAGES
        DB      66h,0C1h,0E8h,10h        ; SHR EAX, 16
        CMP     AX,1237h                 ; i440FX host bridge
        JNE     DU_SCAN_PAGES

        ; Enable PAM3 (5Ch: D000-D7FF) and PAM4 (5Dh: D800-DFFF) in PCI dword 5Ch
        MOV     DX,0CF8h
        DB      66h,0B8h,5Ch,00h,00h,80h ; MOV EAX, 8000005Ch
        DB      66h,0EFh                 ; OUT DX, EAX
        MOV     DX,0CFCh
        DB      66h,0EDh                 ; IN  EAX, DX
        MOV     AX,3333h                 ; PAM3=33h (R/W), PAM4=33h (R/W), preserve 5Eh/5Fh
        DB      66h,0EFh                 ; OUT DX, EAX

DU_SKIP_PAM:
DU_SCAN_PAGES:
        MOV     BX,0D000h
        MOV     SI,OFFSET UMB_TABLE
        XOR     DI,DI                   ; DI = current active UMB slot (0 = none)

DU_PAGE_LP:
        CMP     BX,0E000h
        JB      DU_PAGE_BODY
        JMP     DU_DONE
DU_PAGE_BODY:
        MOV     ES,BX
        ; Check Option ROM signature 55AAh
        CMP     WORD PTR ES:[0],0AA55h
        JNE     DU_CHK_RAM
        ; Skip entire option ROM span (ES:[2] * 512 bytes, rounded to 0080h paras)
        XOR     AH,AH
        MOV     AL,ES:[2]
        MOV     CL,5
        SHL     AX,CL                   ; * 32 paras
        ADD     AX,007Fh
        AND     AX,0FF80h
        OR      AX,AX
        JNZ     DU_ROM_ADV
        MOV     AX,0080h
DU_ROM_ADV:
        XOR     DI,DI
        ADD     BX,AX
        JMP     SHORT DU_PAGE_LP

DU_CHK_RAM:
        ; Verify page appears uninitialized (0000h or FFFFh) and passes R/W RAM test
        MOV     DX,ES:[0]
        OR      DX,DX
        JZ      DU_RW_TEST
        CMP     DX,0FFFFh
        JNE     DU_NOT_UMB

DU_RW_TEST:
        PUSHF
        CLI
        MOV     WORD PTR ES:[0],5A5Ah
        CMP     WORD PTR ES:[0],5A5Ah
        JNE     DU_RW_FAIL
        MOV     WORD PTR ES:[0],0A5A5h
        CMP     WORD PTR ES:[0],0A5A5h
        JNE     DU_RW_FAIL
        MOV     WORD PTR ES:[07FEh],5A5Ah
        CMP     WORD PTR ES:[07FEh],5A5Ah
        JNE     DU_RW_FAIL
        MOV     WORD PTR ES:[0],DX
        MOV     WORD PTR ES:[07FEh],DX
        POPF

        ; Page BX..BX+007Fh is valid clean UMB RAM! Coalesce or open new slot.
        OR      DI,DI
        JZ      DU_NEW_UMB
        ADD     WORD PTR CS:[DI+4],0080h
        ADD     BX,0080h
        JMP     DU_PAGE_LP

DU_NEW_UMB:
        CMP     SI,OFFSET UMB_TABLE + (MAX_UMBS * 6)
        JAE     DU_DONE
        MOV     BYTE PTR CS:[SI],1      ; state = free
        MOV     BYTE PTR CS:[SI+1],0
        MOV     CS:[SI+2],BX
        MOV     WORD PTR CS:[SI+4],0080h
        MOV     DI,SI
        ADD     SI,6
        ADD     BX,0080h
        JMP     DU_PAGE_LP

DU_RW_FAIL:
        MOV     ES:[0],DX
        POPF
DU_NOT_UMB:
        XOR     DI,DI
        ADD     BX,0080h
        JMP     DU_PAGE_LP

DU_DONE:
        RET
DISCOVER_UMBS   ENDP

CODE    ENDS
        END
