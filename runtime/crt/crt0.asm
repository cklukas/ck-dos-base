; Clean-room 16-bit MS-DOS C Runtime startup (WP-5)
; Provides __acrtused, __chkstk, _osmajor, _osminor, _errno, and DOS entry point.

IFDEF FARCODE
CRT0_TEXT SEGMENT BYTE PUBLIC 'CODE'
CRT0_TEXT ENDS
ELSE
_TEXT   SEGMENT BYTE PUBLIC 'CODE'
_TEXT   ENDS
ENDIF

NULL    SEGMENT PARA PUBLIC 'BEGDATA'
        DB      16 DUP (0)
NULL    ENDS

_DATA   SEGMENT WORD PUBLIC 'DATA'
        PUBLIC  __acrtused
        PUBLIC  __osmajor
        PUBLIC  __osminor
        PUBLIC  _errno
        PUBLIC  __psp
        PUBLIC  _DOS_TopOfMemory
        PUBLIC  __heap_ptr
        PUBLIC  __heap_limit
__acrtused  DW  1
__osmajor   DB  4
__osminor   DB  0
_errno      DW  0
__psp       DW  0
_DOS_TopOfMemory DW 0A000h
__heap_ptr  DW  0
__heap_limit DW 0
__argv_buf  DB  128 DUP (0)
__argv_ptrs DW  32 DUP (0)
__prog_name DB  'DOS', 0
__env_empty DW  0
_DATA   ENDS

CONST   SEGMENT WORD PUBLIC 'CONST'
CONST   ENDS

_BSS    SEGMENT WORD PUBLIC 'BSS'
_BSS    ENDS

_STACK  SEGMENT PARA STACK 'STACK'
        DB      128 DUP (0)
_STACK  ENDS

DGROUP  GROUP   NULL, _DATA, CONST, _BSS, _STACK

IFDEF FARCODE
        EXTRN   _main:FAR
CRT0_TEXT SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:CRT0_TEXT, DS:DGROUP, SS:DGROUP
ELSE
        EXTRN   _main:NEAR
_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT, DS:DGROUP, SS:DGROUP
ENDIF

        PUBLIC  __start
        PUBLIC  __chkstk

__start PROC    NEAR
        mov     ax, DGROUP
        mov     ds, ax
        mov     bx, ss
        sub     bx, ax
        mov     cl, 4
        shl     bx, cl
        add     sp, bx
        mov     ss, ax
        mov     __psp, es

        ; Expand DGROUP stack up to 64 KB (psp_memsz) and claim gap for heap
        mov     bx, es:[2]
        sub     bx, ax
        jbe     no_heap_expand
        test    bx, 0F000h
        jz      heap_shift
        mov     bx, 0FFF0h
        jmp     SHORT heap_chk
heap_shift:
        mov     cl, 4
        shl     bx, cl
heap_chk:
        mov     dx, sp
        add     dx, 2048
        jc      no_heap_expand
        cmp     bx, dx
        jbe     no_heap_expand
        mov     dx, sp
        inc     dx
        and     dx, 0FFFEh
        mov     __heap_ptr, dx
        mov     sp, bx
        sub     bx, 2048
        mov     __heap_limit, bx
        ; Shrink DOS memory block to DGROUP + 64KB so child EXEC has RAM
        push    es
        mov     es, __psp
        mov     ax, DGROUP
        add     ax, 1000h
        mov     bx, __psp
        sub     ax, bx
        mov     bx, ax
        mov     ah, 4Ah
        int     21h
        pop     es
no_heap_expand:

        ; Query DOS version (INT 21h AH=30h)
        mov     ah, 30h
        int     21h
        mov     __osmajor, al
        mov     __osminor, ah

        ; Parse PSP command tail (es:80h = length, es:81h.. = chars)
        mov     es, __psp
        xor     cx, cx
        mov     cl, es:[80h]
        cmp     cx, 127
        jbe     len_ok
        mov     cx, 127
len_ok:
        mov     si, 81h
        mov     di, OFFSET DGROUP:__argv_buf
        jcxz    copy_done
copy_loop:
        mov     al, es:[si]
        mov     [di], al
        inc     si
        inc     di
        loop    copy_loop
copy_done:
        mov     BYTE PTR [di], 0

        ; Build argv[0..argc-1] in __argv_ptrs
        mov     di, OFFSET DGROUP:__argv_ptrs
        mov     ax, OFFSET DGROUP:__prog_name
        mov     [di], ax
        add     di, 2
        mov     dx, 1
        mov     si, OFFSET DGROUP:__argv_buf
skip_ws:
        mov     al, [si]
        cmp     al, 0
        je      args_done
        cmp     al, 0Dh
        je      args_done
        cmp     al, ' '
        je      next_ws
        cmp     al, 09h
        je      next_ws
        cmp     dx, 30
        jae     args_done
        mov     [di], si
        add     di, 2
        inc     dx
scan_arg:
        inc     si
        mov     al, [si]
        cmp     al, 0
        je      args_done
        cmp     al, 0Dh
        je      term_done
        cmp     al, ' '
        je      term_arg
        cmp     al, 09h
        je      term_arg
        jmp     scan_arg
term_arg:
        mov     BYTE PTR [si], 0
        inc     si
        jmp     skip_ws
term_done:
        mov     BYTE PTR [si], 0
        jmp     args_done
next_ws:
        inc     si
        jmp     skip_ws
args_done:
        xor     ax, ax
        mov     [di], ax
        mov     es, __psp

        ; Call _main(argc, argv, envp)
        mov     ax, OFFSET DGROUP:__env_empty
        push    ax
        mov     ax, OFFSET DGROUP:__argv_ptrs
        push    ax
        push    dx
IFDEF FARCODE
        call    FAR PTR _main
ELSE
        call    _main
ENDIF
        add     sp, 6

        ; Exit with return code in AL
        mov     ah, 4Ch
        int     21h
__start ENDP

IFDEF FARCODE
__chkstk PROC   FAR
        pop     cx
        pop     dx
        sub     sp, ax
        push    dx
        push    cx
        ret
__chkstk ENDP
CRT0_TEXT ENDS
ELSE
__chkstk PROC   NEAR
        pop     cx
        sub     sp, ax
        jmp     cx
__chkstk ENDP
_TEXT   ENDS
ENDIF
        END     __start
