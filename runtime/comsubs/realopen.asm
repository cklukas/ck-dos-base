; Clean-room REALOPEN replacement for COMSUBS.LIB (WP-5)
; extern unsigned far pascal REALOPEN(char far *name, unsigned far *handle, unsigned mode);

_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT

        PUBLIC  REALOPEN

REALOPEN PROC   FAR
        push    bp
        mov     bp, sp
        push    ds
        lds     dx, DWORD PTR [bp+10]
        mov     ax, [bp+6]
        mov     ah, 3Dh
        int     21h
        jc      ro_err
        lds     bx, DWORD PTR [bp+8]
        mov     [bx], ax
        xor     ax, ax
        jmp     ro_done
ro_err:
        nop
ro_done:
        pop     ds
        pop     bp
        ret     10
REALOPEN ENDP

_TEXT   ENDS
        END
