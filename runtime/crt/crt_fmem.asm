; Clean-room Medium-Model DOS/BIOS, far-memory, and EXEC helpers (MLIBCE.LIB)

_DATA   SEGMENT WORD PUBLIC 'DATA'
        EXTRN   __psp:WORD
        EXTRN   __stack_bottom:WORD
        EXTRN   ___ck_has_x87:WORD
        EXTRN   ___ck_x87_dadd:DWORD
        EXTRN   ___ck_x87_dsub:DWORD
        EXTRN   ___ck_x87_dmul:DWORD
        EXTRN   ___ck_x87_ddiv:DWORD
        EXTRN   ___ck_x87_dcmp:DWORD
        EXTRN   ___ck_x87_ltod:DWORD
        EXTRN   ___ck_x87_ultod:DWORD
        EXTRN   ___ck_x87_dtol:DWORD
        EXTRN   ___ck_x87_dtos:DWORD
        EXTRN   ___ck_x87_stod:DWORD
_DATA   ENDS

DGROUP  GROUP   _DATA

CRT_FMEM_TEXT SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:CRT_FMEM_TEXT, DS:DGROUP

        PUBLIC  _segread
        PUBLIC  _int86
        PUBLIC  _int86x
        PUBLIC  _intdos
        PUBLIC  _intdosx
        PUBLIC  __enable
        PUBLIC  __disable
        PUBLIC  _inp
        PUBLIC  _inpw
        PUBLIC  _outp
        PUBLIC  _outpw
        PUBLIC  _getch
        PUBLIC  _getche
        PUBLIC  _putch
        PUBLIC  _kbhit
        PUBLIC  __dos_getvect
        PUBLIC  __dos_setvect
        PUBLIC  __fmalloc
        PUBLIC  __ffree
        PUBLIC  __fmemcpy
        PUBLIC  __fmemmove
        PUBLIC  __fmemset
        PUBLIC  __fmemchr
        PUBLIC  __fmemcmp
        PUBLIC  __fstrlen
        PUBLIC  __fstrcpy
        PUBLIC  __fstrcmp
        PUBLIC  _strcpy
        PUBLIC  _stackavail
        PUBLIC  __dos_raw_find
        PUBLIC  __dos_raw_exec
        PUBLIC  __x87_init
        PUBLIC  ___x87_sqrt
        PUBLIC  ___x87_sin
        PUBLIC  ___x87_cos
        PUBLIC  ___x87_atan

; 256-entry 4-byte software interrupt dispatch table (genuine 0xCD <imm8>; RETF; NOP)
INT_ROW MACRO   base
        DB      0CDh, base + 00h, 0CBh, 90h
        DB      0CDh, base + 01h, 0CBh, 90h
        DB      0CDh, base + 02h, 0CBh, 90h
        DB      0CDh, base + 03h, 0CBh, 90h
        DB      0CDh, base + 04h, 0CBh, 90h
        DB      0CDh, base + 05h, 0CBh, 90h
        DB      0CDh, base + 06h, 0CBh, 90h
        DB      0CDh, base + 07h, 0CBh, 90h
        DB      0CDh, base + 08h, 0CBh, 90h
        DB      0CDh, base + 09h, 0CBh, 90h
        DB      0CDh, base + 0Ah, 0CBh, 90h
        DB      0CDh, base + 0Bh, 0CBh, 90h
        DB      0CDh, base + 0Ch, 0CBh, 90h
        DB      0CDh, base + 0Dh, 0CBh, 90h
        DB      0CDh, base + 0Eh, 0CBh, 90h
        DB      0CDh, base + 0Fh, 0CBh, 90h
ENDM

_int_stubs LABEL BYTE
        INT_ROW 00h
        INT_ROW 10h
        INT_ROW 20h
        INT_ROW 30h
        INT_ROW 40h
        INT_ROW 50h
        INT_ROW 60h
        INT_ROW 70h
        INT_ROW 80h
        INT_ROW 90h
        INT_ROW 0A0h
        INT_ROW 0B0h
        INT_ROW 0C0h
        INT_ROW 0D0h
        INT_ROW 0E0h
        INT_ROW 0F0h

; void segread(struct SREGS *sregs)
_segread PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     ax, es
        mov     [bx+0], ax
        mov     ax, cs
        mov     [bx+2], ax
        mov     ax, ss
        mov     [bx+4], ax
        mov     ax, ds
        mov     [bx+6], ax
        pop     bp
        ret
_segread ENDP

; int int86(int intno, const union REGS *inregs, union REGS *outregs)
_int86 PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 4
        push    si
        push    di
        push    ds
        push    es

        mov     ax, [bp+6]
        xor     ah, ah
        shl     ax, 1
        shl     ax, 1
        add     ax, OFFSET CRT_FMEM_TEXT:_int_stubs
        mov     [bp-4], ax
        mov     [bp-2], cs

        mov     bx, [bp+8]
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        call    DWORD PTR [bp-4]
        pushf
        push    bx
        mov     bx, ss
        mov     ds, bx
        mov     bx, [bp+10]
        mov     [bx+0], ax
        pop     ax
        mov     [bx+2], ax
        mov     [bx+4], cx
        mov     [bx+6], dx
        mov     [bx+8], si
        mov     [bx+10], di
        pop     ax
        and     ax, 1
        mov     [bx+12], ax
        mov     ax, [bx+0]

        pop     es
        pop     ds
        pop     di
        pop     si
        mov     sp, bp
        pop     bp
        ret
_int86 ENDP

; int int86x(int intno, const union REGS *inregs, union REGS *outregs, struct SREGS *sregs)
_int86x PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 4
        push    si
        push    di
        push    ds
        push    es

        mov     ax, [bp+6]
        xor     ah, ah
        shl     ax, 1
        shl     ax, 1
        add     ax, OFFSET CRT_FMEM_TEXT:_int_stubs
        mov     [bp-4], ax
        mov     [bp-2], cs

        mov     bx, [bp+12]
        mov     es, [bx+0]
        mov     ax, [bx+6]
        mov     bx, [bp+8]
        push    ax
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        pop     ds
        call    DWORD PTR [bp-4]
        pushf
        push    ds
        push    bx
        mov     bx, ss
        mov     ds, bx
        mov     bx, [bp+10]
        mov     [bx+0], ax
        pop     ax
        mov     [bx+2], ax
        mov     [bx+4], cx
        mov     [bx+6], dx
        mov     [bx+8], si
        mov     [bx+10], di
        pop     cx
        mov     si, [bp+12]
        mov     [si+6], cx
        mov     cx, es
        mov     [si+0], cx
        pop     ax
        and     ax, 1
        mov     [bx+12], ax
        mov     ax, [bx+0]

        pop     es
        pop     ds
        pop     di
        pop     si
        mov     sp, bp
        pop     bp
        ret
_int86x ENDP

; int intdos(const union REGS *inregs, union REGS *outregs)
_intdos PROC FAR
        push    bp
        mov     bp, sp
        push    WORD PTR [bp+8]
        push    WORD PTR [bp+6]
        mov     ax, 21h
        push    ax
        call    FAR PTR _int86
        add     sp, 6
        pop     bp
        ret
_intdos ENDP

; int intdosx(const union REGS *inregs, union REGS *outregs, struct SREGS *sregs)
_intdosx PROC FAR
        push    bp
        mov     bp, sp
        push    WORD PTR [bp+10]
        push    WORD PTR [bp+8]
        push    WORD PTR [bp+6]
        mov     ax, 21h
        push    ax
        call    FAR PTR _int86x
        add     sp, 8
        pop     bp
        ret
_intdosx ENDP

__enable PROC FAR
        sti
        ret
__enable ENDP

__disable PROC FAR
        cli
        ret
__disable ENDP

_inp PROC FAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+6]
        xor     ax, ax
        in      al, dx
        pop     bp
        ret
_inp ENDP

_inpw PROC FAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+6]
        in      ax, dx
        pop     bp
        ret
_inpw ENDP

_outp PROC FAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+6]
        mov     ax, [bp+8]
        out     dx, al
        xor     ah, ah
        pop     bp
        ret
_outp ENDP

_outpw PROC FAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+6]
        mov     ax, [bp+8]
        out     dx, ax
        pop     bp
        ret
_outpw ENDP

_getch PROC FAR
        mov     ah, 07h
        int     21h
        xor     ah, ah
        ret
_getch ENDP

_getche PROC FAR
        mov     ah, 01h
        int     21h
        xor     ah, ah
        ret
_getche ENDP

_putch PROC FAR
        push    bp
        mov     bp, sp
        mov     dl, [bp+6]
        mov     ah, 02h
        int     21h
        mov     al, [bp+6]
        xor     ah, ah
        pop     bp
        ret
_putch ENDP

_kbhit PROC FAR
        mov     ah, 0Bh
        int     21h
        cbw
        ret
_kbhit ENDP

; void (__interrupt __far *_dos_getvect(unsigned intnum))()
__dos_getvect PROC FAR
        push    bp
        mov     bp, sp
        push    es
        mov     al, [bp+6]
        mov     ah, 35h
        int     21h
        mov     ax, bx
        mov     dx, es
        pop     es
        pop     bp
        ret
__dos_getvect ENDP

; void _dos_setvect(unsigned intnum, void (__interrupt __far *handler)())
__dos_setvect PROC FAR
        push    bp
        mov     bp, sp
        push    ds
        mov     al, [bp+6]
        mov     dx, [bp+8]
        mov     ds, [bp+10]
        mov     ah, 25h
        int     21h
        pop     ds
        pop     bp
        ret
__dos_setvect ENDP

; void __far *_fmalloc(size_t size)
__fmalloc PROC FAR
        push    bp
        mov     bp, sp
        mov     ax, [bp+6]
        or      ax, ax
        jnz     fm_calc
        mov     bx, 1
        jmp     SHORT fm_alloc
fm_calc:
        xor     dx, dx
        add     ax, 15
        adc     dx, 0
        mov     cx, 4
fm_shr:
        shr     dx, 1
        rcr     ax, 1
        loop    fm_shr
        mov     bx, ax
fm_alloc:
        mov     ah, 48h
        int     21h
        jc      fm_fail
        mov     dx, ax
        xor     ax, ax
        pop     bp
        ret
fm_fail:
        xor     ax, ax
        xor     dx, dx
        pop     bp
        ret
__fmalloc ENDP

; void _ffree(void __far *ptr)
__ffree PROC FAR
        push    bp
        mov     bp, sp
        mov     ax, [bp+6]
        or      ax, [bp+8]
        jz      ff_done
        push    es
        mov     es, [bp+8]
        mov     ah, 49h
        int     21h
        pop     es
ff_done:
        pop     bp
        ret
__ffree ENDP

; void __far *_fmemcpy(void __far *dst, const void __far *src, size_t n)
__fmemcpy PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        cld
        mov     cx, [bp+14]
        jcxz    fmc_done
        les     di, DWORD PTR [bp+6]
        lds     si, DWORD PTR [bp+10]
        shr     cx, 1
        rep     movsw
        adc     cx, cx
        rep     movsb
fmc_done:
        mov     ax, [bp+6]
        mov     dx, [bp+8]
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
__fmemcpy ENDP

; void __far *_fmemmove(void __far *dst, const void __far *src, size_t n)
__fmemmove PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        mov     cx, [bp+14]
        jcxz    fmm_done

        ; Compute 20-bit linear dst in DX:AX and src in BX:SI
        mov     ax, [bp+8]
        xor     dx, dx
        mov     di, 4
fmm_dl: shl     ax, 1
        rcl     dx, 1
        dec     di
        jnz     fmm_dl
        add     ax, [bp+6]
        adc     dx, 0

        mov     si, [bp+12]
        xor     bx, bx
        mov     di, 4
fmm_sl: shl     si, 1
        rcl     bx, 1
        dec     di
        jnz     fmm_sl
        add     si, [bp+10]
        adc     bx, 0

        ; If dst_lin <= src_lin, copy forward
        cmp     dx, bx
        jb      fmm_fwd
        ja      fmm_chk_end
        cmp     ax, si
        jbe     fmm_fwd
fmm_chk_end:
        ; If dst_lin >= src_lin + n, copy forward
        add     si, cx
        adc     bx, 0
        cmp     dx, bx
        ja      fmm_fwd
        jb      fmm_bwd
        cmp     ax, si
        jae     fmm_fwd
fmm_bwd:
        les     di, DWORD PTR [bp+6]
        lds     si, DWORD PTR [bp+10]
        add     di, cx
        dec     di
        add     si, cx
        dec     si
        std
        rep     movsb
        cld
        jmp     SHORT fmm_done
fmm_fwd:
        les     di, DWORD PTR [bp+6]
        lds     si, DWORD PTR [bp+10]
        cld
        shr     cx, 1
        rep     movsw
        adc     cx, cx
        rep     movsb
fmm_done:
        mov     ax, [bp+6]
        mov     dx, [bp+8]
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
__fmemmove ENDP

; void __far *_fmemset(void __far *dst, int c, size_t n)
__fmemset PROC FAR
        push    bp
        mov     bp, sp
        push    di
        push    es
        cld
        mov     cx, [bp+12]
        jcxz    fms_done
        les     di, DWORD PTR [bp+6]
        mov     al, [bp+10]
        mov     ah, al
        shr     cx, 1
        rep     stosw
        adc     cx, cx
        rep     stosb
fms_done:
        mov     ax, [bp+6]
        mov     dx, [bp+8]
        pop     es
        pop     di
        pop     bp
        ret
__fmemset ENDP

; void __far *_fmemchr(const void __far *s, int c, size_t n)
__fmemchr PROC FAR
        push    bp
        mov     bp, sp
        push    di
        push    es
        cld
        mov     cx, [bp+12]
        jcxz    fmchr_nf
        les     di, DWORD PTR [bp+6]
        mov     al, [bp+10]
        repne   scasb
        jne     fmchr_nf
        dec     di
        mov     ax, di
        mov     dx, es
        jmp     SHORT fmchr_ret
fmchr_nf:
        xor     ax, ax
        xor     dx, dx
fmchr_ret:
        pop     es
        pop     di
        pop     bp
        ret
__fmemchr ENDP

; int _fmemcmp(const void __far *a, const void __far *b, size_t n)
__fmemcmp PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        cld
        xor     ax, ax
        mov     cx, [bp+14]
        jcxz    fmcmp_ret
        lds     si, DWORD PTR [bp+6]
        les     di, DWORD PTR [bp+10]
        repe    cmpsb
        je      fmcmp_ret
        xor     ax, ax
        xor     dx, dx
        mov     al, [si-1]
        mov     dl, es:[di-1]
        sub     ax, dx
fmcmp_ret:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
__fmemcmp ENDP

; size_t _fstrlen(const char __far *s)
__fstrlen PROC FAR
        push    bp
        mov     bp, sp
        push    di
        push    es
        mov     ax, [bp+6]
        or      ax, [bp+8]
        jz      fslen_zero
        cld
        les     di, DWORD PTR [bp+6]
        mov     bx, di
        xor     al, al
        mov     cx, 0FFFFh
        repne   scasb
        jne     fslen_zero
        dec     di
        mov     ax, di
        sub     ax, bx
        jmp     SHORT fslen_ret
fslen_zero:
        xor     ax, ax
fslen_ret:
        pop     es
        pop     di
        pop     bp
        ret
__fstrlen ENDP

; char __far *_fstrcpy(char __far *dst, const char __far *src)
__fstrcpy PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        cld
        les     di, DWORD PTR [bp+6]
        lds     si, DWORD PTR [bp+10]
fscpy_lp:
        lodsb
        stosb
        or      al, al
        jnz     fscpy_lp
        mov     ax, [bp+6]
        mov     dx, [bp+8]
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
__fstrcpy ENDP

; int _fstrcmp(const char __far *a, const char __far *b)
__fstrcmp PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        cld
        lds     si, DWORD PTR [bp+6]
        les     di, DWORD PTR [bp+10]
fscmp_lp:
        lodsb
        mov     dl, es:[di]
        inc     di
        cmp     al, dl
        jne     fscmp_diff
        or      al, al
        jnz     fscmp_lp
        xor     ax, ax
        jmp     SHORT fscmp_ret
fscmp_diff:
        xor     ah, ah
        xor     dh, dh
        sub     ax, dx
fscmp_ret:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
__fstrcmp ENDP

; char *strcpy(char *dst, const char *src)
_strcpy PROC FAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        pop     es
        cld
        mov     di, [bp+6]
        mov     dx, di
        mov     si, [bp+8]
        or      si, si
        jnz     nscpy_lp
        mov     BYTE PTR [di], 0
        jmp     SHORT nscpy_dn
nscpy_lp:
        lodsb
        stosb
        or      al, al
        jnz     nscpy_lp
nscpy_dn:
        mov     ax, dx
        pop     di
        pop     si
        pop     bp
        ret
_strcpy ENDP

; size_t stackavail(void)
_stackavail PROC FAR
        mov     ax, sp
        mov     dx, __stack_bottom
        cmp     ax, dx
        jbe     stk_none
        sub     ax, dx
        ret
stk_none:
        xor     ax, ax
        ret
_stackavail ENDP

; unsigned _dos_raw_find(const char *path, unsigned attr, void *dta, int first)
__dos_raw_find PROC FAR
        push    bp
        mov     bp, sp
        push    ds
        push    es

        ; Save current DTA (AH=2Fh -> ES:BX)
        mov     ah, 2Fh
        int     21h
        push    es
        push    bx

        ; Set DTA to DS:dta
        mov     dx, [bp+10]
        mov     ah, 1Ah
        int     21h

        mov     ax, [bp+12]
        or      ax, ax
        jz      drf_next
        mov     dx, [bp+6]
        mov     cx, [bp+8]
        mov     ah, 4Eh
        int     21h
        jmp     SHORT drf_after
drf_next:
        mov     ah, 4Fh
        int     21h
drf_after:
        jc      drf_err
        xor     ax, ax
drf_err:
        mov     cx, ax

        ; Restore saved DTA
        pop     dx
        pop     ds
        mov     ah, 1Ah
        int     21h

        mov     ax, cx
        pop     es
        pop     ds
        pop     bp
        ret
__dos_raw_find ENDP

_exec_ss DW 0
_exec_sp DW 0

; int _dos_raw_exec(const char *path, const void *tail128, unsigned env_seg)
__dos_raw_exec PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 16
        push    si
        push    di
        push    ds
        push    es

        ; Build 14-byte EXEC parameter block at [bp-14]
        lea     bx, [bp-14]
        mov     ax, [bp+10]
        mov     [bx+0], ax
        mov     ax, [bp+8]
        mov     [bx+2], ax
        mov     [bx+4], ds
        mov     ax, __psp
        mov     WORD PTR [bx+6], 5Ch
        mov     [bx+8], ax
        mov     WORD PTR [bx+10], 6Ch
        mov     [bx+12], ax

        mov     ax, ss
        mov     es, ax
        mov     dx, [bp+6]
        mov     cs:_exec_ss, ss
        mov     cs:_exec_sp, sp
        mov     ax, 4B00h
        int     21h
        mov     ss, cs:_exec_ss
        mov     sp, cs:_exec_sp
        mov     cx, ss
        mov     ds, cx
        mov     es, cx
        jc      dre_fail
        mov     ah, 4Dh
        int     21h
        xor     ah, ah
        jmp     SHORT dre_done
dre_fail:
        neg     ax
dre_done:
        pop     es
        pop     ds
        pop     di
        pop     si
        mov     sp, bp
        pop     bp
        ret
__dos_raw_exec ENDP

; One-time x87 FPU probe and IEEE-754 53-bit double-precision setup (027Fh).
; Safe across 8086, 186, 286, 386SX/DX, 486SX/DX, Pentium+, and QEMU.
x87_ud_isr PROC FAR
        push    bp
        mov     bp, sp
        mov     WORD PTR [bp+2], OFFSET x87_after_386
        mov     bx, 1
        pop     bp
        iret
x87_ud_isr ENDP

__x87_init PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 4
        push    bx
        push    cx
        push    dx
        push    es
        ; 1. Reject 8086 without raising #UD: PUSH SP pushes decremented SP on 8086
        mov     ax, sp
        push    sp
        pop     bx
        cmp     ax, bx
        je      x87_is_186
        jmp     x87_absent
x87_is_186:
        ; 2. Guard 386 probe with temporary INT 06h (#UD) handler at 0000:0018h
        xor     ax, ax
        mov     es, ax
        pushf
        cli
        mov     cx, es:[18h]
        mov     dx, es:[1Ah]
        mov     WORD PTR es:[18h], OFFSET x87_ud_isr
        mov     es:[1Ah], cs
        xor     bx, bx
        DB      0Fh, 01h, 0E0h          ; smsw ax (#UD on 186 / i386-real)
        DB      66h, 89h, 0C0h          ; mov eax, eax (#UD on 286 / 186 / i386-real)
x87_after_386:
        mov     es:[18h], cx
        mov     es:[1Ah], dx
        popf
        test    bx, bx
        jnz     x87_no_fpu_near
        ; 3. Check CR0.EM (bit 2) and CR0.TS (bit 3)
        test    ax, 000Ch
        jnz     x87_no_fpu_near
        ; 4. Probe physical x87 via FNINIT + FNSTSW AX + FNSTCW [bp-2]
        DB      0DBh, 0E3h              ; fninit
        mov     ax, 5A5Ah
        DB      0DFh, 0E0h              ; fnstsw ax
        cmp     al, 0
        jne     x87_no_fpu_near
        mov     WORD PTR [bp-2], 5A5Ah
        DB      0D9h, 7Eh, 0FEh         ; fnstcw word ptr [bp-2]
        mov     ax, [bp-2]
        and     ax, 103Fh
        cmp     ax, 003Fh
        je      x87_present
x87_no_fpu_near:
        jmp     SHORT x87_absent
x87_present:
        ; 5. Set CW = 027Fh (53-bit IEEE-754 double, nearest-even, all masked)
        mov     WORD PTR [bp-2], 027Fh
        DB      0D9h, 6Eh, 0FEh         ; fldcw word ptr [bp-2]
        mov     ax, cs
        mov     WORD PTR [___ck_x87_dadd+0], OFFSET __x87_dadd
        mov     WORD PTR [___ck_x87_dadd+2], ax
        mov     WORD PTR [___ck_x87_dsub+0], OFFSET __x87_dsub
        mov     WORD PTR [___ck_x87_dsub+2], ax
        mov     WORD PTR [___ck_x87_dmul+0], OFFSET __x87_dmul
        mov     WORD PTR [___ck_x87_dmul+2], ax
        mov     WORD PTR [___ck_x87_ddiv+0], OFFSET __x87_ddiv
        mov     WORD PTR [___ck_x87_ddiv+2], ax
        mov     WORD PTR [___ck_x87_dcmp+0], OFFSET __x87_dcmp
        mov     WORD PTR [___ck_x87_dcmp+2], ax
        mov     WORD PTR [___ck_x87_ltod+0], OFFSET __x87_ltod
        mov     WORD PTR [___ck_x87_ltod+2], ax
        mov     WORD PTR [___ck_x87_ultod+0], OFFSET __x87_ultod
        mov     WORD PTR [___ck_x87_ultod+2], ax
        mov     WORD PTR [___ck_x87_dtol+0], OFFSET __x87_dtol
        mov     WORD PTR [___ck_x87_dtol+2], ax
        mov     WORD PTR [___ck_x87_dtos+0], OFFSET __x87_dtos
        mov     WORD PTR [___ck_x87_dtos+2], ax
        mov     WORD PTR [___ck_x87_stod+0], OFFSET __x87_stod
        mov     WORD PTR [___ck_x87_stod+2], ax
        mov     WORD PTR [___ck_has_x87], 1
x87_absent:
        pop     es
        pop     dx
        pop     cx
        pop     bx
        mov     sp, bp
        pop     bp
        ret
__x87_init ENDP

__x87_dadd PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        mov     bx, [bp+10]
        DB      0DCh, 07h               ; fadd qword ptr [bx]
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
__x87_dadd ENDP

__x87_dsub PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        mov     bx, [bp+10]
        DB      0DCh, 27h               ; fsub qword ptr [bx]
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
__x87_dsub ENDP

__x87_dmul PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        mov     bx, [bp+10]
        DB      0DCh, 0Fh               ; fmul qword ptr [bx]
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
__x87_dmul ENDP

__x87_ddiv PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        mov     bx, [bp+10]
        DB      0DCh, 37h               ; fdiv qword ptr [bx]
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
__x87_ddiv ENDP

__x87_dcmp PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        mov     bx, [bp+8]
        DB      0DCh, 1Fh               ; fcomp qword ptr [bx]
        DB      0DFh, 0E0h              ; fnstsw ax
        and     ah, 45h
        jz      xdcmp_gt
        cmp     ah, 01h
        je      xdcmp_lt
        cmp     ah, 40h
        je      xdcmp_eq
        mov     ax, 2
        pop     bp
        ret
xdcmp_gt:
        mov     ax, 1
        pop     bp
        ret
xdcmp_lt:
        mov     ax, -1
        pop     bp
        ret
xdcmp_eq:
        xor     ax, ax
        pop     bp
        ret
__x87_dcmp ENDP

__x87_ltod PROC FAR
        push    bp
        mov     bp, sp
        DB      0DBh, 46h, 08h          ; fild dword ptr [bp+8]
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
__x87_ltod ENDP

__x87_ultod PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 8
        mov     ax, [bp+8]
        mov     [bp-8], ax
        mov     ax, [bp+10]
        mov     [bp-6], ax
        xor     ax, ax
        mov     [bp-4], ax
        mov     [bp-2], ax
        DB      0DFh, 6Eh, 0F8h         ; fild qword ptr [bp-8]
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        mov     sp, bp
        pop     bp
        ret
__x87_ultod ENDP

__x87_dtol PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 8
        mov     WORD PTR [bp-6], 0E7Fh
        mov     WORD PTR [bp-8], 027Fh
        DB      0D9h, 6Eh, 0FAh         ; fldcw word ptr [bp-6] (chop)
        mov     bx, [bp+6]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        DB      0DBh, 5Eh, 0FCh         ; fistp dword ptr [bp-4]
        DB      0D9h, 6Eh, 0F8h         ; fldcw word ptr [bp-8] (nearest)
        mov     ax, [bp-4]
        mov     dx, [bp-2]
        mov     sp, bp
        pop     bp
        ret
__x87_dtol ENDP

__x87_dtos PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 4
        mov     bx, [bp+6]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        DB      0D9h, 5Eh, 0FCh         ; fstp dword ptr [bp-4]
        mov     ax, [bp-4]
        mov     dx, [bp-2]
        mov     sp, bp
        pop     bp
        ret
__x87_dtos ENDP

__x87_stod PROC FAR
        push    bp
        mov     bp, sp
        DB      0D9h, 46h, 08h          ; fld dword ptr [bp+8]
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
__x87_stod ENDP

___x87_sqrt PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        DB      0D9h, 0FAh              ; fsqrt
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
___x87_sqrt ENDP

___x87_sin PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        DB      0D9h, 0FEh              ; fsin
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
___x87_sin ENDP

___x87_cos PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        DB      0D9h, 0FFh              ; fcos
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
___x87_cos ENDP

___x87_atan PROC FAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        DB      0DDh, 07h               ; fld qword ptr [bx]
        DB      0D9h, 0E8h              ; fld1
        DB      0D9h, 0F3h              ; fpatan
        mov     bx, [bp+6]
        DB      0DDh, 1Fh               ; fstp qword ptr [bx]
        pop     bp
        ret
___x87_atan ENDP

CRT_FMEM_TEXT ENDS
        END
