; Clean-room 386+ 32-bit integer IEEE-754 binary64/binary32 fast paths (MLIBCE.LIB)
; Installed by __x87_init ONLY after 386 CPU detection succeeds and no x87 FPU is present.

_DATA   SEGMENT WORD PUBLIC 'DATA'
        EXTRN   ___ck_has_fast_fp:WORD
        EXTRN   ___ck_x87_dadd:DWORD
        EXTRN   ___ck_x87_dsub:DWORD
        EXTRN   ___ck_x87_dmul:DWORD
        EXTRN   ___ck_x87_ddiv:DWORD
        EXTRN   ___ck_x87_dcmp:DWORD
        EXTRN   ___ck_x87_ltod:DWORD
        EXTRN   ___ck_x87_ultod:DWORD
        EXTRN   ___ck_x87_dtos:DWORD
        EXTRN   ___ck_x87_stod:DWORD
_DATA   ENDS

DGROUP  GROUP   _DATA

CRT_FP386_TEXT SEGMENT PARA PUBLIC 'CODE'
        ASSUME  CS:CRT_FP386_TEXT, DS:DGROUP
.386

        PUBLIC  __386_fp_init

__386_fp_init PROC FAR
        mov     ax, cs
        mov     WORD PTR [___ck_x87_dadd+0], OFFSET __386_dadd
        mov     WORD PTR [___ck_x87_dadd+2], ax
        mov     WORD PTR [___ck_x87_dsub+0], OFFSET __386_dsub
        mov     WORD PTR [___ck_x87_dsub+2], ax
        mov     WORD PTR [___ck_x87_dmul+0], OFFSET __386_dmul
        mov     WORD PTR [___ck_x87_dmul+2], ax
        mov     WORD PTR [___ck_x87_ddiv+0], OFFSET __386_ddiv
        mov     WORD PTR [___ck_x87_ddiv+2], ax
        mov     WORD PTR [___ck_x87_dcmp+0], OFFSET __386_dcmp
        mov     WORD PTR [___ck_x87_dcmp+2], ax
        mov     WORD PTR [___ck_x87_ltod+0], OFFSET __386_ltod
        mov     WORD PTR [___ck_x87_ltod+2], ax
        mov     WORD PTR [___ck_x87_ultod+0], OFFSET __386_ultod
        mov     WORD PTR [___ck_x87_ultod+2], ax
        mov     WORD PTR [___ck_x87_dtos+0], OFFSET __386_dtos
        mov     WORD PTR [___ck_x87_dtos+2], ax
        mov     WORD PTR [___ck_x87_stod+0], OFFSET __386_stod
        mov     WORD PTR [___ck_x87_stod+2], ax
        mov     WORD PTR [___ck_has_fast_fp], 1
        ret
__386_fp_init ENDP

; ---------------------------------------------------------------------------
; void __386_ltod(double *out [bp+6], long v [bp+8])
; void __386_ultod(double *out [bp+6], unsigned long v [bp+8])
; ---------------------------------------------------------------------------
__386_ltod PROC FAR
        push    bp
        mov     bp, sp
        push    ebx
        mov     eax, DWORD PTR [bp+8]
        xor     ebx, ebx
        test    eax, eax
        jz      ltod_zero
        jns     ltod_norm
        mov     ebx, 80000000h
        neg     eax
        jmp     SHORT ltod_norm
__386_ltod ENDP

__386_ultod PROC FAR
        push    bp
        mov     bp, sp
        push    ebx
        mov     eax, DWORD PTR [bp+8]
        xor     ebx, ebx
        test    eax, eax
        jz      ltod_zero
ltod_norm:
        bsr     edx, eax
        mov     ecx, 31
        sub     ecx, edx
        shl     eax, cl
        add     edx, 1023
        shl     edx, 20
        or      ebx, edx
        shl     eax, 1
        mov     edx, eax
        shr     edx, 12
        or      edx, ebx
        shl     eax, 20
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        pop     ebx
        pop     bp
        ret
ltod_zero:
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], eax
        pop     ebx
        pop     bp
        ret
__386_ultod ENDP

; ---------------------------------------------------------------------------
; void __386_stod(double *out [bp+6], unsigned long fbits [bp+8])
; Handles all finite floats (fbexp != 0xFF, verified by caller).
; ---------------------------------------------------------------------------
__386_stod PROC FAR
        push    bp
        mov     bp, sp
        push    ebx
        push    esi
        mov     eax, DWORD PTR [bp+8]
        mov     ecx, eax
        and     ecx, 80000000h
        mov     edx, eax
        shr     edx, 23
        and     edx, 0FFh
        jz      stod_sub_or_zero
        add     edx, 380h
        shl     edx, 20
        or      ecx, edx
        mov     edx, eax
        shr     edx, 3
        and     edx, 000FFFFFh
        or      edx, ecx
        shl     eax, 29
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        pop     esi
        pop     ebx
        pop     bp
        ret
stod_sub_or_zero:
        and     eax, 007FFFFFh
        jz      stod_zero
        mov     esi, ecx
        bsr     edx, eax
        mov     ecx, 31
        sub     ecx, edx
        shl     eax, cl
        add     edx, 874
        shl     edx, 20
        or      esi, edx
        shl     eax, 1
        mov     edx, eax
        shr     edx, 12
        or      edx, esi
        shl     eax, 20
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        pop     esi
        pop     ebx
        pop     bp
        ret
stod_zero:
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], ecx
        pop     esi
        pop     ebx
        pop     bp
        ret
__386_stod ENDP

; ---------------------------------------------------------------------------
; unsigned long __386_dtos(const double *a [bp+6])
; Caller guarantees 898 <= bexp <= 1149 (in-range normal float).
; ---------------------------------------------------------------------------
__386_dtos PROC FAR
        push    bp
        mov     bp, sp
        push    ebx
        push    esi
        mov     bx, [bp+6]
        mov     eax, DWORD PTR [bx]
        mov     edx, DWORD PTR [bx+4]
        mov     ecx, edx
        and     ecx, 80000000h
        mov     ebx, edx
        shr     ebx, 20
        and     ebx, 7FFh
        sub     ebx, 380h
        shl     ebx, 23
        and     edx, 000FFFFFh
        shl     edx, 3
        mov     esi, eax
        shr     esi, 29
        or      edx, esi
        add     edx, ebx
        and     eax, 1FFFFFFFh
        cmp     eax, 10000000h
        jb      dtos_done
        ja      dtos_inc
        test    dl, 1
        jz      dtos_done
dtos_inc:
        inc     edx
dtos_done:
        or      edx, ecx
        mov     ax, dx
        shr     edx, 16
        pop     esi
        pop     ebx
        pop     bp
        ret
__386_dtos ENDP

; ---------------------------------------------------------------------------
; int __386_dcmp(const double *a [bp+6], const double *b [bp+8])
; ---------------------------------------------------------------------------
__386_dcmp PROC FAR
        push    bp
        mov     bp, sp
        push    ebx
        push    esi
        push    edi
        mov     bx, [bp+6]
        mov     eax, DWORD PTR [bx]
        mov     edx, DWORD PTR [bx+4]
        mov     bx, [bp+8]
        mov     ecx, DWORD PTR [bx]
        mov     esi, DWORD PTR [bx+4]

        mov     edi, edx
        and     edi, 7FFFFFFFh
        cmp     edi, 7FF00000h
        ja      dcmp_nan
        jb      dcmp_a_ok
        test    eax, eax
        jnz     dcmp_nan
dcmp_a_ok:
        mov     ebx, esi
        and     ebx, 7FFFFFFFh
        cmp     ebx, 7FF00000h
        ja      dcmp_nan
        jb      dcmp_b_ok
        test    ecx, ecx
        jnz     dcmp_nan
dcmp_b_ok:
        xor     esi, edx
        js      dcmp_diff_sign
        cmp     edi, ebx
        jne     dcmp_same_ne
        cmp     eax, ecx
        je      dcmp_eq
dcmp_same_ne:
        jb      dcmp_mag_lt
        test    edx, edx
        js      dcmp_lt
        jmp     SHORT dcmp_gt
dcmp_mag_lt:
        test    edx, edx
        js      dcmp_gt
        jmp     SHORT dcmp_lt
dcmp_diff_sign:
        or      edi, ebx
        or      edi, eax
        or      edi, ecx
        jz      dcmp_eq
        test    edx, edx
        js      dcmp_lt
dcmp_gt:
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        pop     bp
        ret
dcmp_lt:
        mov     ax, -1
        pop     edi
        pop     esi
        pop     ebx
        pop     bp
        ret
dcmp_eq:
        xor     ax, ax
        pop     edi
        pop     esi
        pop     ebx
        pop     bp
        ret
dcmp_nan:
        mov     ax, 2
        pop     edi
        pop     esi
        pop     ebx
        pop     bp
        ret
__386_dcmp ENDP

; ---------------------------------------------------------------------------
; int __386_dmul(double *out [bp+6], const double *a [bp+8], const double *b [bp+10])
; Returns AX=1 if handled, AX=0 to fall through to C soft-float.
; ---------------------------------------------------------------------------
__386_dmul PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 16
        push    ebx
        push    esi
        push    edi

        mov     bx, [bp+8]
        mov     eax, DWORD PTR [bx]
        mov     edx, DWORD PTR [bx+4]
        mov     bx, [bp+10]
        mov     ecx, DWORD PTR [bx]
        mov     esi, DWORD PTR [bx+4]

        mov     edi, edx
        xor     edi, esi
        and     edi, 80000000h
        mov     DWORD PTR [bp-4], edi

        mov     edi, edx
        shr     edi, 20
        and     edi, 7FFh
        mov     ebx, esi
        shr     ebx, 20
        and     ebx, 7FFh

        test    edi, edi
        jz      dmul_ea_zero
        cmp     edi, 7FFh
        je      dmul_fallback
        test    ebx, ebx
        jz      dmul_eb_zero
        cmp     ebx, 7FFh
        je      dmul_fallback

        add     edi, ebx
        sub     edi, 1023
        mov     DWORD PTR [bp-8], edi
        sub     edi, 2
        cmp     edi, 2042
        ja      dmul_fallback

        shld    edx, eax, 11
        or      edx, 80000000h
        shl     eax, 11
        shld    esi, ecx, 11
        or      esi, 80000000h
        shl     ecx, 11

        mov     ebx, eax
        or      ebx, ecx
        jnz     dmul_full

        mov     eax, edx
        mul     esi
        mov     edi, eax
        xor     ecx, ecx
        xor     ebx, ebx
        jmp     SHORT dmul_norm

dmul_full:
        mov     DWORD PTR [bp-12], eax
        mov     DWORD PTR [bp-16], ecx
        mov     edi, edx

        mul     ecx
        mov     ebx, eax
        mov     ecx, edx

        mov     eax, edi
        mul     DWORD PTR [bp-16]
        add     ecx, eax
        adc     edx, 0
        mov     DWORD PTR [bp-16], edx

        mov     eax, DWORD PTR [bp-12]
        mul     esi
        add     ecx, eax
        adc     edx, DWORD PTR [bp-16]
        mov     DWORD PTR [bp-12], edx
        mov     eax, 0
        adc     eax, 0
        mov     DWORD PTR [bp-16], eax

        mov     eax, edi
        mul     esi
        mov     edi, DWORD PTR [bp-12]
        add     edi, eax
        adc     edx, DWORD PTR [bp-16]

dmul_norm:
        mov     esi, DWORD PTR [bp-8]
        test    edx, edx
        jns     dmul_shift1
        inc     esi
        jmp     SHORT dmul_round
dmul_shift1:
        shld    edx, edi, 1
        shld    edi, ecx, 1
        shl     ecx, 1

dmul_round:
        mov     eax, edi
        shrd    eax, edx, 11
        shr     edx, 11
        and     edi, 7FFh
        or      ecx, ebx
        jz      dmul_no_sticky
        or      edi, 1
dmul_no_sticky:
        cmp     edi, 400h
        jb      dmul_pack
        ja      dmul_inc
        test    al, 1
        jz      dmul_pack
dmul_inc:
        add     eax, 1
        adc     edx, 0
dmul_pack:
        dec     esi
        shl     esi, 20
        add     edx, esi
        or      edx, DWORD PTR [bp-4]
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

dmul_ea_zero:
        and     edx, 7FFFFFFFh
        or      edx, eax
        jnz     dmul_fallback
        cmp     ebx, 7FFh
        je      dmul_fallback
        jmp     SHORT dmul_ret_zero
dmul_eb_zero:
        and     esi, 7FFFFFFFh
        or      esi, ecx
        jnz     dmul_fallback
dmul_ret_zero:
        mov     edx, DWORD PTR [bp-4]
        xor     eax, eax
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

dmul_fallback:
        xor     ax, ax
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret
__386_dmul ENDP

; ---------------------------------------------------------------------------
; int __386_ddiv(double *out [bp+6], const double *a [bp+8], const double *b [bp+10])
; Returns AX=1 if handled, AX=0 to fall through to C soft-float.
; ---------------------------------------------------------------------------
__386_ddiv PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 20
        push    ebx
        push    esi
        push    edi

        mov     bx, [bp+8]
        mov     eax, DWORD PTR [bx]
        mov     edx, DWORD PTR [bx+4]
        mov     bx, [bp+10]
        mov     ecx, DWORD PTR [bx]
        mov     esi, DWORD PTR [bx+4]

        mov     edi, edx
        xor     edi, esi
        and     edi, 80000000h
        mov     DWORD PTR [bp-4], edi

        mov     edi, edx
        shr     edi, 20
        and     edi, 7FFh
        mov     ebx, esi
        shr     ebx, 20
        and     ebx, 7FFh

        test    edi, edi
        jz      ddiv_ea_zero
        cmp     edi, 7FFh
        je      ddiv_fallback
        test    ebx, ebx
        jz      ddiv_fallback
        cmp     ebx, 7FFh
        je      ddiv_fallback

        sub     edi, ebx
        add     edi, 1023
        mov     DWORD PTR [bp-8], edi
        sub     edi, 3
        cmp     edi, 2041
        ja      ddiv_fallback

        shld    edx, eax, 11
        or      edx, 80000000h
        shl     eax, 11
        shld    esi, ecx, 11
        or      esi, 80000000h
        shl     ecx, 11
        mov     DWORD PTR [bp-12], esi
        mov     DWORD PTR [bp-16], ecx

        cmp     edx, esi
        jb      ddiv_a_lt_b
        ja      ddiv_a_ge_b
        cmp     eax, ecx
        jb      ddiv_a_lt_b
ddiv_a_ge_b:
        shrd    eax, edx, 2
        shr     edx, 2
        jmp     SHORT ddiv_step1
ddiv_a_lt_b:
        dec     DWORD PTR [bp-8]
        shrd    eax, edx, 1
        shr     edx, 1

ddiv_step1:
        div     esi
        mov     ebx, eax
        mov     edi, edx
        mul     DWORD PTR [bp-16]
        neg     eax
        sbb     edi, edx
        jnc     ddiv_step2
        dec     ebx
        add     eax, DWORD PTR [bp-16]
        adc     edi, esi

ddiv_step2:
        mov     DWORD PTR [bp-20], ebx
        mov     ecx, eax
        shl     ecx, 25
        shrd    eax, edi, 7
        mov     edx, edi
        shr     edx, 7
        div     esi
        mov     ebx, eax
        mov     edi, edx
        mul     DWORD PTR [bp-16]
        sub     ecx, eax
        sbb     edi, edx
        jnc     ddiv_rem_ok
        dec     ebx
        add     ecx, DWORD PTR [bp-16]
        adc     edi, esi
ddiv_rem_ok:
        or      ecx, edi
        jz      ddiv_no_sticky
        or      ebx, 1
ddiv_no_sticky:
        mov     ecx, ebx
        and     ecx, 7
        shr     ebx, 3
        mov     edx, DWORD PTR [bp-20]
        mov     eax, edx
        shl     eax, 22
        or      eax, ebx
        shr     edx, 10
        cmp     ecx, 4
        jb      ddiv_pack
        ja      ddiv_inc
        test    al, 1
        jz      ddiv_pack
ddiv_inc:
        add     eax, 1
        adc     edx, 0
ddiv_pack:
        mov     ecx, DWORD PTR [bp-8]
        dec     ecx
        shl     ecx, 20
        add     edx, ecx
        or      edx, DWORD PTR [bp-4]
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

ddiv_ea_zero:
        and     edx, 7FFFFFFFh
        or      edx, eax
        jnz     ddiv_fallback
        test    ebx, ebx
        jz      ddiv_fallback
        cmp     ebx, 7FFh
        je      ddiv_fallback
        mov     edx, DWORD PTR [bp-4]
        xor     eax, eax
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

ddiv_fallback:
        xor     ax, ax
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret
__386_ddiv ENDP

; ---------------------------------------------------------------------------
; int __386_dadd(double *out [bp+6], const double *a [bp+8], const double *b [bp+10])
; int __386_dsub(double *out [bp+6], const double *a [bp+8], const double *b [bp+10])
; Returns AX=1 if handled, AX=0 to fall through to C soft-float.
; ---------------------------------------------------------------------------
__386_dadd PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 16
        push    ebx
        push    esi
        push    edi
        mov     bx, [bp+8]
        mov     eax, DWORD PTR [bx]
        mov     edx, DWORD PTR [bx+4]
        mov     bx, [bp+10]
        mov     ecx, DWORD PTR [bx]
        mov     esi, DWORD PTR [bx+4]
        jmp     SHORT dadd_core
__386_dadd ENDP

__386_dsub PROC FAR
        push    bp
        mov     bp, sp
        sub     sp, 16
        push    ebx
        push    esi
        push    edi
        mov     bx, [bp+8]
        mov     eax, DWORD PTR [bx]
        mov     edx, DWORD PTR [bx+4]
        mov     bx, [bp+10]
        mov     ecx, DWORD PTR [bx]
        mov     esi, DWORD PTR [bx+4]
        xor     esi, 80000000h
dadd_core:
        mov     edi, edx
        shr     edi, 20
        and     edi, 7FFh
        mov     ebx, esi
        shr     ebx, 20
        and     ebx, 7FFh

        test    edi, edi
        jz      dadd_ea_zero
        cmp     edi, 7FFh
        je      dadd_fallback
        test    ebx, ebx
        jz      dadd_eb_zero
        cmp     ebx, 7FFh
        je      dadd_fallback

        mov     DWORD PTR [bp-8], edx
        xor     DWORD PTR [bp-8], esi

        mov     edi, edx
        and     edi, 7FFFFFFFh
        mov     ebx, esi
        and     ebx, 7FFFFFFFh
        cmp     edi, ebx
        ja      dadd_ordered
        jb      dadd_swap
        cmp     eax, ecx
        jae     dadd_ordered
dadd_swap:
        mov     DWORD PTR [bp-16], eax
        mov     eax, ecx
        mov     ecx, DWORD PTR [bp-16]
        mov     DWORD PTR [bp-16], edx
        mov     edx, esi
        mov     esi, DWORD PTR [bp-16]
        mov     DWORD PTR [bp-16], edi
        mov     edi, ebx
        mov     ebx, DWORD PTR [bp-16]

dadd_ordered:
        and     edx, 80000000h
        mov     DWORD PTR [bp-4], edx

        mov     edx, edi
        shr     edx, 20
        mov     esi, ebx
        shr     esi, 20
        mov     DWORD PTR [bp-12], edx
        sub     edx, esi

        and     edi, 000FFFFFh
        or      edi, 00100000h
        shld    edi, eax, 3
        shl     eax, 3

        and     ebx, 000FFFFFh
        or      ebx, 00100000h
        shld    ebx, ecx, 3
        shl     ecx, 3
        mov     esi, ecx

        test    edx, edx
        jz      dadd_aligned
        cmp     edx, 57
        jae     dadd_diff_huge
        cmp     edx, 32
        je      dadd_diff_32
        ja      dadd_diff_gt32

        mov     ecx, edx
        xor     edx, edx
        shrd    edx, esi, cl
        shrd    esi, ebx, cl
        shr     ebx, cl
        test    edx, edx
        jz      dadd_aligned
        or      esi, 1
        jmp     SHORT dadd_aligned

dadd_diff_32:
        mov     edx, esi
        mov     esi, ebx
        xor     ebx, ebx
        test    edx, edx
        jz      dadd_aligned
        or      esi, 1
        jmp     SHORT dadd_aligned

dadd_diff_gt32:
        mov     ecx, edx
        sub     ecx, 32
        xor     edx, edx
        shrd    edx, ebx, cl
        or      edx, esi
        shr     ebx, cl
        mov     esi, ebx
        xor     ebx, ebx
        test    edx, edx
        jz      dadd_aligned
        or      esi, 1
        jmp     SHORT dadd_aligned

dadd_diff_huge:
        xor     ebx, ebx
        mov     esi, 1

dadd_aligned:
        test    DWORD PTR [bp-8], 80000000h
        jnz     dadd_sub

        add     eax, esi
        adc     edi, ebx
        test    edi, 01000000h
        jz      dadd_round
        mov     ecx, eax
        and     ecx, 1
        shrd    eax, edi, 1
        shr     edi, 1
        or      eax, ecx
        inc     DWORD PTR [bp-12]
        jmp     SHORT dadd_round

dadd_sub:
        sub     eax, esi
        sbb     edi, ebx
        mov     ecx, edi
        or      ecx, eax
        jz      dadd_pos_zero
        test    edi, edi
        jz      dadd_norm_lo
        bsr     edx, edi
        mov     ecx, 23
        sub     ecx, edx
        jz      dadd_round
        sub     DWORD PTR [bp-12], ecx
        shld    edi, eax, cl
        shl     eax, cl
        jmp     SHORT dadd_round

dadd_norm_lo:
        bsr     edx, eax
        cmp     edx, 23
        ja      dadd_norm_lo_hi
        mov     ecx, 23
        sub     ecx, edx
        mov     edi, eax
        shl     edi, cl
        xor     eax, eax
        add     ecx, 32
        sub     DWORD PTR [bp-12], ecx
        jmp     SHORT dadd_round

dadd_norm_lo_hi:
        mov     ecx, edx
        sub     ecx, 23
        mov     edi, eax
        shr     edi, cl
        mov     ecx, 55
        sub     ecx, edx
        shl     eax, cl
        sub     DWORD PTR [bp-12], ecx

dadd_round:
        mov     edx, DWORD PTR [bp-12]
        dec     edx
        cmp     edx, 2044
        ja      dadd_fallback
        mov     ecx, eax
        and     ecx, 7
        shrd    eax, edi, 3
        shr     edi, 3
        cmp     ecx, 4
        jb      dadd_pack
        ja      dadd_inc
        test    al, 1
        jz      dadd_pack
dadd_inc:
        add     eax, 1
        adc     edi, 0
dadd_pack:
        shl     edx, 20
        add     edi, edx
        or      edi, DWORD PTR [bp-4]
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edi
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

dadd_pos_zero:
        xor     eax, eax
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], eax
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

dadd_ea_zero:
        mov     edi, edx
        and     edi, 7FFFFFFFh
        or      edi, eax
        jnz     dadd_fallback
        test    ebx, ebx
        jz      dadd_fallback
        cmp     ebx, 7FFh
        je      dadd_fallback
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], ecx
        mov     DWORD PTR [bx+4], esi
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

dadd_eb_zero:
        mov     ebx, esi
        and     ebx, 7FFFFFFFh
        or      ebx, ecx
        jnz     dadd_fallback
        mov     bx, [bp+6]
        mov     DWORD PTR [bx], eax
        mov     DWORD PTR [bx+4], edx
        mov     ax, 1
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret

dadd_fallback:
        xor     ax, ax
        pop     edi
        pop     esi
        pop     ebx
        mov     sp, bp
        pop     bp
        ret
__386_dsub ENDP

CRT_FP386_TEXT ENDS
        END
