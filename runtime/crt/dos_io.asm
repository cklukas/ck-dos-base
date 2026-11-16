; Clean-room 16-bit MS-DOS CRT low-level DOS & BIOS I/O (WP-5)
; Small-model cdecl calling convention (args at [bp+4+AP], [bp+6+AP], ...).

IFDEF FARCODE
DOS_IO_TEXT SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:DOS_IO_TEXT
AP      EQU     2
ELSE
_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT
AP      EQU     0
ENDIF

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
        PUBLIC  _creat
        PUBLIC  _unlink
        PUBLIC  _remove
        PUBLIC  _rename
        PUBLIC  _strcpy
        PUBLIC  __dos_exec

; void segread(struct SREGS *sregs)
; SREGS: +0=es, +2=cs, +4=ss, +6=ds
IFDEF FARCODE
_segread PROC FAR
ELSE
_segread PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     bx, [bp+4+AP]
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
IFDEF FARCODE
_intdos PROC FAR
ELSE
_intdos PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        push    si
        push    di
        mov     bx, [bp+4+AP]
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        int     21h
        pushf
        push    bx
        mov     bx, [bp+6+AP]
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
IFDEF FARCODE
_intdosx PROC FAR
ELSE
_intdosx PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        mov     bx, [bp+8+AP]
        mov     es, [bx+0]
        mov     ax, [bx+6]
        mov     bx, [bp+4+AP]
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
        mov     bx, [bp+6+AP]
        mov     [bx+0], ax
        pop     ax
        mov     [bx+2], ax
        mov     [bx+4], cx
        mov     [bx+6], dx
        mov     [bx+8], si
        mov     [bx+10], di
        pop     cx
        mov     si, [bp+8+AP]
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
IFDEF FARCODE
_int86 PROC FAR
ELSE
_int86 PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        push    si
        push    di
        mov     ax, [bp+4+AP]
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
        mov     bx, [bp+6+AP]
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
        mov     bx, [bp+8+AP]
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
IFDEF FARCODE
_int86x PROC FAR
ELSE
_int86x PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     ax, [bp+4+AP]
        cmp     al, 21h
        jne     x_chk15
        push    [bp+10+AP]
        push    [bp+8+AP]
        push    [bp+6+AP]
IFDEF FARCODE
        call    FAR PTR _intdosx
ELSE
        call    _intdosx
ENDIF
        add     sp, 6
        pop     bp
        ret
x_chk15:
        push    si
        push    di
        push    ds
        push    es
        mov     bx, [bp+10+AP]
        mov     es, [bx+0]
        mov     ax, [bx+6]
        mov     bx, [bp+6+AP]
        push    ax
        mov     ax, [bx+0]
        mov     cx, [bx+4]
        mov     dx, [bx+6]
        mov     si, [bx+8]
        mov     di, [bx+10]
        mov     bx, [bx+2]
        pop     ds
        push    bp
        mov     bp, [bp+4+AP]
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
        mov     bx, [bp+8+AP]
        mov     [bx+0], ax
        pop     ax
        mov     [bx+2], ax
        mov     [bx+4], cx
        mov     [bx+6], dx
        mov     [bx+8], si
        mov     [bx+10], di
        pop     cx
        mov     si, [bp+10+AP]
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
IFDEF FARCODE
_exit PROC FAR
ELSE
_exit PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     ax, [bp+4+AP]
        mov     ah, 4Ch
        int     21h
_exit   ENDP

; int open(const char *path, int oflag, ...)
IFDEF FARCODE
_open PROC FAR
ELSE
_open PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     dx, [bp+4+AP]
        mov     ax, [bp+6+AP]
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
IFDEF FARCODE
_close PROC FAR
ELSE
_close PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     bx, [bp+4+AP]
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
IFDEF FARCODE
_read PROC FAR
ELSE
_read PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     bx, [bp+4+AP]
        mov     dx, [bp+6+AP]
        mov     cx, [bp+8+AP]
        mov     ah, 3Fh
        int     21h
        jnc     read_ok
        mov     ax, -1
read_ok:
        pop     bp
        ret
_read   ENDP

; int write(int fd, const void *buf, unsigned count)
IFDEF FARCODE
_write PROC FAR
ELSE
_write PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     bx, [bp+4+AP]
        mov     dx, [bp+6+AP]
        mov     cx, [bp+8+AP]
        mov     ah, 40h
        int     21h
        jnc     write_ok
        mov     ax, -1
write_ok:
        pop     bp
        ret
_write  ENDP

; long lseek(int fd, long offset, int origin)
IFDEF FARCODE
_lseek PROC FAR
ELSE
_lseek PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     bx, [bp+4+AP]
        mov     dx, [bp+6+AP]
        mov     cx, [bp+8+AP]
        mov     al, [bp+10+AP]
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
IFDEF FARCODE
_filelength PROC FAR
ELSE
_filelength PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     bx, [bp+4+AP]
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
IFDEF FARCODE
_access PROC FAR
ELSE
_access PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     dx, [bp+4+AP]
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
IFDEF FARCODE
_chdir PROC FAR
ELSE
_chdir PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     dx, [bp+4+AP]
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
IFDEF FARCODE
_mkdir PROC FAR
ELSE
_mkdir PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     dx, [bp+4+AP]
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
IFDEF FARCODE
_rmdir PROC FAR
ELSE
_rmdir PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     dx, [bp+4+AP]
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
IFDEF FARCODE
_getch PROC FAR
ELSE
_getch PROC NEAR
ENDIF
        mov     ah, 07h
        int     21h
        xor     ah, ah
        ret
_getch  ENDP

; int putch(int c)
IFDEF FARCODE
_putch PROC FAR
ELSE
_putch PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     dl, [bp+4+AP]
        mov     ah, 02h
        int     21h
        mov     ax, [bp+4+AP]
        pop     bp
        ret
_putch  ENDP

; int kbhit(void)
IFDEF FARCODE
_kbhit PROC FAR
ELSE
_kbhit PROC NEAR
ENDIF
        mov     ah, 0Bh
        int     21h
        xor     ah, ah
        ret
_kbhit  ENDP

; int creat(const char *path, int mode)
IFDEF FARCODE
_creat PROC FAR
ELSE
_creat PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        mov     dx, [bp+4+AP]
        xor     cx, cx
        mov     ah, 3Ch
        int     21h
        jnc     creat_ok
        mov     ax, -1
creat_ok:
        pop     bp
        ret
_creat  ENDP

; int unlink(const char *path) / int remove(const char *path)
IFDEF FARCODE
_remove PROC FAR
ELSE
_remove PROC NEAR
ENDIF
_unlink:
        push    bp
        mov     bp, sp
        mov     dx, [bp+4+AP]
        mov     ah, 41h
        int     21h
        mov     ax, 0
        jnc     unlink_ok
        mov     ax, -1
unlink_ok:
        pop     bp
        ret
_remove ENDP

; int rename(const char *oldpath, const char *newpath)
IFDEF FARCODE
_rename PROC FAR
ELSE
_rename PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        push    di
        push    es
        push    ds
        pop     es
        mov     dx, [bp+4+AP]
        mov     di, [bp+6+AP]
        mov     ah, 56h
        int     21h
        mov     ax, 0
        jnc     ren_ok
        mov     ax, -1
ren_ok:
        pop     es
        pop     di
        pop     bp
        ret
_rename ENDP

_exec_ss DW     0
_exec_sp DW     0

; int _dos_exec(const char *prog, const char *tail)
IFDEF FARCODE
__dos_exec PROC FAR
ELSE
__dos_exec PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    es
        push    ds

        sub     sp, 148
        ; [bp-154..bp-26] = tail buffer (128 bytes: len, chars..., 0Dh)
        ; [bp-26..bp-12]  = EXEC param block (14 bytes)
        lea     di, [bp-154]
        mov     si, [bp+6+AP]
        xor     cx, cx
        inc     di
        or      si, si
        jz      exec_tail_done
        mov     al, [si]
        or      al, al
        jz      exec_tail_done
        mov     BYTE PTR [di], ' '
        inc     di
        inc     cx
exec_tail_cp:
        mov     al, [si]
        or      al, al
        jz      exec_tail_done
        cmp     cx, 125
        jae     exec_tail_done
        mov     [di], al
        inc     si
        inc     di
        inc     cx
        jmp     exec_tail_cp
exec_tail_done:
        mov     BYTE PTR [di], 0Dh
        lea     di, [bp-154]
        mov     [di], cl

        ; Build EXEC param block at [bp-26]
        lea     bx, [bp-26]
        xor     ax, ax
        mov     [bx+0], ax
        mov     [bx+2], di
        mov     [bx+4], ds
        mov     WORD PTR [bx+6], 5Ch
        mov     ax, ds
        mov     [bx+8], ax
        mov     WORD PTR [bx+10], 6Ch
        mov     [bx+12], ax

        mov     ax, ss
        mov     es, ax
        mov     dx, [bp+4+AP]
        mov     cs:_exec_ss, ss
        mov     cs:_exec_sp, sp
        mov     ax, 4B00h
        int     21h
        mov     ss, cs:_exec_ss
        mov     sp, cs:_exec_sp
        jc      exec_fail
        mov     ah, 4Dh
        int     21h
        xor     ah, ah
        jmp     exec_done
exec_fail:
        mov     ax, -1
exec_done:
        add     sp, 148
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     bp
        ret
__dos_exec ENDP

; char *strcpy(char *dst, const char *src)
; Restores ES = DS like MS C 5.1 STRCPY.ASM (relied on by MS-DOS 4 fix_es_reg).
IFDEF FARCODE
_strcpy PROC FAR
ELSE
_strcpy PROC NEAR
ENDIF
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        pop     es
        cld
        mov     di, [bp+4+AP]
        mov     dx, di
        mov     si, [bp+6+AP]
        or      si, si
        jnz     strcpy_lp
        mov     BYTE PTR [di], 0
        jmp     strcpy_dn
strcpy_lp:
        lodsb
        stosb
        or      al, al
        jnz     strcpy_lp
strcpy_dn:
        mov     ax, dx
        pop     di
        pop     si
        pop     bp
        ret
_strcpy ENDP

IFDEF FARCODE
DOS_IO_TEXT ENDS
ELSE
_TEXT   ENDS
ENDIF
        END
