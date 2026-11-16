; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; CK-DOS EDIT's help topics: CK-DOS's own text, shown in a viewer that
; works like DOS 5 EDIT's (EDIT.ASM, HELP_VIEW). Linked last, after DOSUI,
; on a paragraph of their own: EDIT reads them through a segment whose
; offset 0 is EDITHLP_START (HELP_SEGMENT, set at START). The table gives
; each topic's first line as an offset from EDITHLP_START; a line is a
; zero-terminated string (01h switches bold on and off, 11h and 10h enclose
; a link, whose text names a topic), and 0FFh follows a topic's last line.

.8086
DOSUI_TEXT SEGMENT PARA PUBLIC 'CODE'

PUBLIC EDITHLP_START
PUBLIC EDITHLP_END

EDITHLP_START   LABEL BYTE
                DW HELP_B0-EDITHLP_START,HELP_B1-EDITHLP_START
                DW HELP_B2-EDITHLP_START,HELP_B3-EDITHLP_START
                DW HELP_B4-EDITHLP_START,HELP_B5-EDITHLP_START
                DW HELP_B6-EDITHLP_START,HELP_B7-EDITHLP_START
                DW HELP_B8-EDITHLP_START
HELP_B0         DB 0
                DB ' ',1,'Getting around the CK-DOS Editor',1,0
                DB 0
                DB '   ',0FEH,' Alt selects the menu bar.',0
                DB '   ',0FEH,' A highlighted letter picks a menu or a command.',0
                DB '   ',0FEH,' The arrow keys move between menus and between commands.',0
                DB '   ',0FEH,' F1 explains the menu, command or dialog box in use.',0
                DB '   ',0FEH,' Esc closes Help and takes you back to your text.',0
                DB 0
                DB ' ',1,'Moving through Help',1,0
                DB 0
                DB '   ',0FEH,' Tab, or the first letter of a topic, selects it; Enter shows',0
                DB '     it, and Alt+F1 returns to where you came from. More about:',0
                DB 0
                DB '     ',11H,'Getting Started',10H,'  Starting the editor, its menus and dialogs',0
                DB 0
                DB '     ',11H,'Keyboard',10H,'         Keys for moving, editing and selecting',0
                DB 0
                DB ' The Help menu lists both topics too.',0
                DB 0
                DB 0FFH
HELP_B1         DB 0
                DB ' The CK-DOS Editor edits plain text files as large as memory',0
                DB ' holds. Start it with EDIT, or with EDIT and a file name, at the',0
                DB ' DOS prompt.',0
                DB 0
                DB '   /B     black-and-white colours on a colour screen',0
                DB '   /H     as many text rows as the display can show',0
                DB '   /NOHI  eight colours, none of them bright',0
                DB '   /G     quick screen updates on a CGA',0
                DB 0
                DB ' More about:',0
                DB '   ',11H,'Using Help',10H,0
                DB '   ',11H,'Menus and Commands',10H,0
                DB '   ',11H,'Dialog Boxes',10H,0
                DB 0FFH
HELP_B2         DB 0
                DB '   Tab, Shift+Tab   The next or previous link',0
                DB '   A letter         The next link starting with it',0
                DB '   Enter            The topic of the link',0
                DB '   Alt+F1           The topic shown before',0
                DB '   Ctrl+F1          The next topic (Shift+Ctrl+F1: the one before)',0
                DB '   Arrows, PgUp/Dn  Scroll the topic',0
                DB '   F1               Getting Started',0
                DB '   F6, Shift+F6     Between Help and the text; Help stays open',0
                DB '   Alt+Plus, Minus  The window in use a row taller or shorter',0
                DB '   Esc              Close Help',0
                DB 0FFH
HELP_B3         DB 0
                DB ' Press Alt, or click the menu bar, to reach the menus.',0
                DB ' Each menu and command has a highlighted letter: press',0
                DB ' it to choose. The arrow keys move between menus and',0
                DB ' commands, Enter chooses the one selected and Esc leaves',0
                DB ' the menus. A dimmed command is not available now; one',0
                DB ' ending in "..." asks for more in a dialog box.',0
                DB 0FFH
HELP_B4         DB 0
                DB ' Tab and Shift+Tab move between the parts of a dialog box,',0
                DB ' Alt and a highlighted letter go straight to one. Typing',0
                DB ' replaces the text in a text box; the arrow keys move in a',0
                DB ' list and Space checks or clears a check box. Enter',0
                DB ' chooses the button with bright brackets, Esc cancels.',0
                DB 0FFH
HELP_B5         DB 0
                DB ' Besides the usual PC keys the editor knows the WordStar',0
                DB ' control keys many DOS editors share. More about:',0
                DB 0
                DB '   ',11H,'Cursor Movement Keys',10H,0
                DB '   ',11H,'Editing Keys',10H,0
                DB '   ',11H,'Selecting and Copying',10H,0
                DB 0FFH
HELP_B6         DB 0
                DB '   Arrows               Ctrl+E Ctrl+X Ctrl+S Ctrl+D  A cell or line',0
                DB '   Ctrl+Left, Right     Ctrl+A Ctrl+F                A word',0
                DB '   Home, End            Ctrl+Q S, Ctrl+Q D           Line start, end',0
                DB '   PgUp, PgDn           Ctrl+R Ctrl+C                A screen',0
                DB '   Ctrl+PgUp, PgDn                                   A screen sideways',0
                DB '   Ctrl+Home, End       Ctrl+Q R, Ctrl+Q C           File start, end',0
                DB '   Ctrl+Up, Down        Ctrl+W Ctrl+Z                Scroll a line',0
                DB '                        Ctrl+Q E, Ctrl+Q X           Window top, bottom',0
                DB '                        Ctrl+K 0-3, Ctrl+Q 0-3       Set, go to a mark',0
                DB 0FFH
HELP_B7         DB 0
                DB '   Insert, Ctrl+V       Insert or overwrite',0
                DB '   Backspace, Ctrl+H    Delete to the left',0
                DB '   Del, Ctrl+G          Delete here, or the selection',0
                DB '   Ctrl+T               Delete the rest of the word',0
                DB '   Ctrl+Y               Cut the line',0
                DB '   Ctrl+Q Y             Delete to the line end',0
                DB '   Ctrl+N               Split the line, cursor stays',0
                DB '   Ctrl+P and a key     Type a control character',0
                DB '   Tab, Shift+Tab       Indent, outdent (a selection: all its lines)',0
                DB 0FFH
HELP_B8         DB 0
                DB '   Shift and a movement key    Select text',0
                DB '   Shift+Del                   Cut the selection',0
                DB '   Ctrl+Ins                    Copy the selection',0
                DB '   Shift+Ins                   Paste',0
                DB '   Del                         Delete the selection',0
                DB '   Ctrl+Q F, Ctrl+Q A          Find, Change',0
                DB '   F3                          Find again',0
                DB '   Double click                Select the word',0
                DB 0FFH
EDITHLP_END     LABEL BYTE

DOSUI_TEXT ENDS
END
