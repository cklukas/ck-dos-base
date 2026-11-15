; Clean-room 16-bit MS-DOS CRT low-level DOS & BIOS I/O (WP-5)
; Small-model cdecl calling convention (args at [bp+4], [bp+6], ...).

_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT

        PUBLIC  _segread
        PUBLIC  _intdos
        PUBLIC  _intdosx
        PUBLIC  _int86
        PUBLIC  _int86x
        PUBLIC  _exit
        PUBLIC  _open
        PUBLIC  _close
        PUBLIC  _read
        PUBLIC  _write
        PUBLIC  _lseek
        PUBLIC  _filelength
        PUBLIC  _access
        PUBLIC  _chdir
        PUBLIC  _mkdir
        PUBLIC  _rmdir
        PUBLIC  _getch
        PUBLIC  _putch
        PUBLIC  _kbhit

; void segread(struct SREGS *sregs)
; SREGS: +0=es, +2=cs, +4=ss, +6=ds
_segread PROC   NEAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
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

; int intdos(union REGS *inregs, union REGS *outregs)
; REGS: +0=ax, +2=bx, +4=cx, +6=dx, +8=si, +10=di, +12=cflag
_intdos PROC    NEAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        mov     bx, [bp+4]
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        int     21h
        pushf
        push    bx
        mov     bx, [bp+6]
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
        pop     di
        pop     si
        pop     bp
        ret
_intdos ENDP

; int intdosx(union REGS *inregs, union REGS *outregs, struct SREGS *sregs)
_intdosx PROC   NEAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        mov     bx, [bp+8]
        mov     es, [bx+0]
        mov     ax, [bx+6]
        mov     bx, [bp+4]
        push    ax
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        pop     ds
        int     21h
        pushf
        push    ds
        push    bx
        mov     bx, ss
        mov     ds, bx
        mov     bx, [bp+6]
        mov     [bx+0], ax
        pop     ax
        mov     [bx+2], ax
        mov     [bx+4], cx
        mov     [bx+6], dx
        mov     [bx+8], si
        mov     [bx+10], di
        pop     cx
        mov     si, [bp+8]
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
        pop     bp
        ret
_intdosx ENDP

; int int86(int intno, union REGS *inregs, union REGS *outregs)
_int86  PROC    NEAR
        push    bp
        mov     bp, sp
        push    si
        push    di
        mov     ax, [bp+4]
        cmp     al, 21h
        jne     chk_10
        jmp     i86_21
chk_10: cmp     al, 10h
        jne     chk_12
        jmp     i86_10
chk_12: cmp     al, 12h
        jne     chk_13
        jmp     i86_12
chk_13: cmp     al, 13h
        jne     chk_15
        jmp     i86_13
chk_15: cmp     al, 15h
        jne     chk_16
        jmp     i86_15
chk_16: cmp     al, 16h
        jne     chk_2f
        jmp     i86_16
chk_2f: cmp     al, 2Fh
        jne     chk_67
        jmp     i86_2f
chk_67: cmp     al, 67h
        jne     chk_def
        jmp     i86_67
chk_def:
        jmp     i86_21
i86_load MACRO
        mov     bx, [bp+6]
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        ENDM
i86_21: i86_load
        int     21h
        jmp     i86_store
i86_10: i86_load
        int     10h
        jmp     i86_store
i86_12: i86_load
        int     12h
        jmp     i86_store
i86_13: i86_load
        int     13h
        jmp     i86_store
i86_15: i86_load
        int     15h
        jmp     i86_store
i86_16: i86_load
        int     16h
        jmp     i86_store
i86_2f: i86_load
        int     2Fh
        jmp     i86_store
i86_67: i86_load
        int     67h
i86_store:
        pushf
        push    bx
        mov     bx, [bp+8]
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
        pop     di
        pop     si
        pop     bp
        ret
_int86  ENDP

; int int86x(int intno, union REGS *inregs, union REGS *outregs, struct SREGS *sregs)
_int86x PROC    NEAR
        push    bp
        mov     bp, sp
        mov     ax, [bp+4]
        cmp     al, 21h
        jne     x_chk15
        push    [bp+10]
        push    [bp+8]
        push    [bp+6]
        call    _intdosx
        add     sp, 6
        pop     bp
        ret
x_chk15:
        push    si
        push    di
        push    ds
        push    es
        mov     bx, [bp+10]
        mov     es, [bx+0]
        mov     ax, [bx+6]
        mov     bx, [bp+6]
        push    ax
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        pop     ds
        push    bp
        mov     bp, [bp+4]
        xchg    ax, bp
        cmp     al, 15h
        je      x_i15
        cmp     al, 13h
        je      x_i13
        cmp     al, 10h
        je      x_i10
        cmp     al, 2Fh
        je      x_i2f
        cmp     al, 67h
        je      x_i67
        xchg    ax, bp
        pop     bp
        int     21h
        jmp     x_done
x_i15:  xchg    ax, bp
        pop     bp
        int     15h
        jmp     x_done
x_i13:  xchg    ax, bp
        pop     bp
        int     13h
        jmp     x_done
x_i10:  xchg    ax, bp
        pop     bp
        int     10h
        jmp     x_done
x_i2f:  xchg    ax, bp
        pop     bp
        int     2Fh
        jmp     x_done
x_i67:  xchg    ax, bp
        pop     bp
        int     67h
x_done:
        pushf
        push    ds
        push    bx
        mov     bx, ss
        mov     ds, bx
        mov     bx, [bp+8]
        mov     [bx+0], ax
        pop     ax
        mov     [bx+2], ax
        mov     [bx+4], cx
        mov     [bx+6], dx
        mov     [bx+8], si
        mov     [bx+10], di
        pop     cx
        mov     si, [bp+10]
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
        pop     bp
        ret
_int86x ENDP

; void exit(int status)
_exit   PROC    NEAR
        push    bp
        mov     bp, sp
        mov     ax, [bp+4]
        mov     ah, 4Ch
        int     21h
_exit   ENDP

; int open(const char *path, int oflag, ...)
_open   PROC    NEAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+4]
        mov     ax, [bp+6]
        and     al, 3
        mov     ah, 3Dh
        int     21h
        jnc     open_ok
        mov     ax, -1
open_ok:
        pop     bp
        ret
_open   ENDP

; int close(int fd)
_close  PROC    NEAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     ah, 3Eh
        int     21h
        mov     ax, 0
        jnc     close_ok
        mov     ax, -1
close_ok:
        pop     bp
        ret
_close  ENDP

; int read(int fd, void *buf, unsigned count)
_read   PROC    NEAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     dx, [bp+6]
        mov     cx, [bp+8]
        mov     ah, 3Fh
        int     21h
        jnc     read_ok
        mov     ax, -1
read_ok:
        pop     bp
        ret
_read   ENDP

; int write(int fd, const void *buf, unsigned count)
_write  PROC    NEAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     dx, [bp+6]
        mov     cx, [bp+8]
        mov     ah, 40h
        int     21h
        jnc     write_ok
        mov     ax, -1
write_ok:
        pop     bp
        ret
_write  ENDP

; long lseek(int fd, long offset, int origin)
_lseek  PROC    NEAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     dx, [bp+6]
        mov     cx, [bp+8]
        mov     al, [bp+10]
        mov     ah, 42h
        int     21h
        jnc     lseek_ok
        mov     ax, -1
        mov     dx, -1
lseek_ok:
        pop     bp
        ret
_lseek  ENDP

; long filelength(int fd)
_filelength PROC NEAR
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        xor     cx, cx
        xor     dx, dx
        mov     ax, 4202h
        int     21h
        jnc     flen_ok
        mov     ax, -1
        mov     dx, -1
flen_ok:
        pop     bp
        ret
_filelength ENDP

; int access(const char *path, int mode)
_access PROC    NEAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+4]
        mov     ax, 4300h
        int     21h
        mov     ax, 0
        jnc     acc_ok
        mov     ax, -1
acc_ok:
        pop     bp
        ret
_access ENDP

; int chdir(const char *path)
_chdir  PROC    NEAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+4]
        mov     ah, 3Bh
        int     21h
        mov     ax, 0
        jnc     chd_ok
        mov     ax, -1
chd_ok:
        pop     bp
        ret
_chdir  ENDP

; int mkdir(const char *path)
_mkdir  PROC    NEAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+4]
        mov     ah, 39h
        int     21h
        mov     ax, 0
        jnc     mkd_ok
        mov     ax, -1
mkd_ok:
        pop     bp
        ret
_mkdir  ENDP

; int rmdir(const char *path)
_rmdir  PROC    NEAR
        push    bp
        mov     bp, sp
        mov     dx, [bp+4]
        mov     ah, 3Ah
        int     21h
        mov     ax, 0
        jnc     rmd_ok
        mov     ax, -1
rmd_ok:
        pop     bp
        ret
_rmdir  ENDP

; int getch(void)
_getch  PROC    NEAR
        mov     ah, 07h
        int     21h
        xor     ah, ah
        ret
_getch  ENDP

; int putch(int c)
_putch  PROC    NEAR
        push    bp
        mov     bp, sp
        mov     dl, [bp+4]
        mov     ah, 02h
        int     21h
        mov     ax, [bp+4]
        pop     bp
        ret
_putch  ENDP

; int kbhit(void)
_kbhit  PROC    NEAR
        mov     ah, 0Bh
        int     21h
        xor     ah, ah
        ret
_kbhit  ENDP

_TEXT   ENDS
        END
