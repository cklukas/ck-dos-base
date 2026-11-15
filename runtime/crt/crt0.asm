; Clean-room 16-bit MS-DOS C Runtime startup (WP-5)
; Provides __acrtused, __chkstk, _osmajor, _osminor, _errno, and DOS entry point.

_TEXT   SEGMENT BYTE PUBLIC 'CODE'
_TEXT   ENDS

_DATA   SEGMENT WORD PUBLIC 'DATA'
        PUBLIC  __acrtused
        PUBLIC  __osmajor
        PUBLIC  __osminor
        PUBLIC  _errno
        PUBLIC  __psp
        PUBLIC  _DOS_TopOfMemory
__acrtused  DW  1
__osmajor   DB  4
__osminor   DB  0
_errno      DW  0
__psp       DW  0
_DOS_TopOfMemory DW 0A000h
_DATA   ENDS

CONST   SEGMENT WORD PUBLIC 'CONST'
CONST   ENDS

_BSS    SEGMENT WORD PUBLIC 'BSS'
_BSS    ENDS

_STACK  SEGMENT PARA STACK 'STACK'
        DB      2048 DUP (0)
_STACK  ENDS

DGROUP  GROUP   _DATA, CONST, _BSS, _STACK

        EXTRN   _main:NEAR

_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT, DS:DGROUP, SS:DGROUP

        PUBLIC  __start
        PUBLIC  __chkstk

__start PROC    NEAR
        mov     ax, DGROUP
        mov     ds, ax
        mov     ss, ax
        mov     __psp, es

        ; Query DOS version (INT 21h AH=30h)
        mov     ah, 30h
        int     21h
        mov     __osmajor, al
        mov     __osminor, ah

        ; Call _main(0, 0, 0) - minimal startup; argv/envp can be populated by _setargv
        xor     ax, ax
        push    ax
        push    ax
        push    ax
        call    _main
        add     sp, 6

        ; Exit with return code in AL
        mov     ah, 4Ch
        int     21h
__start ENDP

__chkstk PROC   NEAR
        pop     cx
        sub     sp, ax
        jmp     cx
__chkstk ENDP

_TEXT   ENDS
        END     __start
