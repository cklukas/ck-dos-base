; Copyright (c) 2026 C. Klukas. All rights reserved.
; SPDX-License-Identifier: MIT
;
; DOSUI-0 public interface for MASM 5.10.
;
; All entry points are NEAR calls for an 8086-compatible tiny-model program.
; The caller must have DS addressing its own data (normally DS=CS in a COM
; program).  Unless an entry says otherwise, all registers and FLAGS are
; preserved.  Coordinates are zero based for an 80-column by 25-row mode-3
; screen.  Out-of-range drawing requests are ignored; row writes are clipped
; at column 79.  An application on a 43- or 50-row screen sets DOSUI_ROWS,
; and DOSUI_ROW_OFFSET moves every row it draws, positions or is clicked at.

DOSUI_SCREEN_COLUMNS       EQU 80
DOSUI_SCREEN_ROWS          EQU 25
DOSUI_TEXT_SAVE_PARAGRAPHS EQU 600     ; Saved caller text cells: 4800 cells
DOSUI_FRAME_PARAGRAPHS     EQU 500     ; Offscreen backbuffer: 4000 cells (80x50)
DOSUI_SAVE_PARAGRAPHS      EQU 1100    ; Total caller workspace segment size

; CP437 characters used by DOSUI_DRAW_MODAL.
DOSUI_BOX_HORIZONTAL EQU 0CDh
DOSUI_BOX_VERTICAL   EQU 0BAh
DOSUI_BOX_TOP_LEFT   EQU 0C9h
DOSUI_BOX_TOP_RIGHT  EQU 0BBh
DOSUI_BOX_BOTTOM_LEFT  EQU 0C8h
DOSUI_BOX_BOTTOM_RIGHT EQU 0BCh

; Six-byte descriptor consumed through DS:SI by DOSUI_DRAW_MODAL.
DOSUI_RECT_TOP        EQU 0       ; byte: top row
DOSUI_RECT_LEFT       EQU 1       ; byte: left column
DOSUI_RECT_BOTTOM     EQU 2       ; byte: bottom row, inclusive
DOSUI_RECT_RIGHT      EQU 3       ; byte: right column, inclusive
DOSUI_RECT_FRAME_ATTR EQU 4       ; byte: frame character attribute
DOSUI_RECT_FILL_ATTR  EQU 5       ; byte: interior space attribute
DOSUI_RECT_SIZE       EQU 6

; DOSUI_INIT
;   Saves the current BIOS video mode, page, cursor position, and cursor
;   shape, then selects 80x25 color text mode 3.  Repeated calls are harmless.
;   With DOSUI_SAVE_SEGMENT naming DOSUI_SAVE_PARAGRAPHS of the caller's
;   memory, a text screen's cells are kept there too.
;   Call DOSUI_SHUTDOWN once when the application releases the screen.
;
; DOSUI_SHUTDOWN
;   Restores the BIOS state saved by DOSUI_INIT: the mode (a taller text
;   screen gets the 8x8 font back), the page, the kept cells, the cursor
;   shape and position.  A call without a matching initialization is
;   harmless.
;
; DOSUI_CLEAR
;   In:  AL = attribute used for every blank cell.
;   Fills the complete mode-3 screen with attributed spaces.
;
; DOSUI_FILL_ROW
;   In:  AL = character, BL = attribute, DH = row, DL = first column,
;        CX = number of cells.
;   Writes one attributed character repeatedly.  The count is clipped to the
;   remainder of the requested row.
;
; DOSUI_WRITE_Z
;   In:  DS:SI = zero-terminated byte string, BL = attribute,
;        DH = row, DL = first column.
;   Writes attributed bytes without interpreting control characters.  Output
;   stops at the zero byte or the right edge of the row.
;
; DOSUI_SET_CURSOR
;   In:  DH = row, DL = column.
;   Positions the mode-3 hardware cursor through BIOS INT 10h.
;
; DOSUI_SET_CURSOR_SHAPE
;   In:  CX = application's visible cursor shape.
;   Applies the shape immediately. DOSUI_SHUTDOWN still restores the caller's
;   original BIOS cursor shape.
;
; DOSUI_SHOW_CURSOR / DOSUI_HIDE_CURSOR
;   Restores the application's selected visible shape, or suppresses and
;   parks the hardware cursor while a non-text control owns keyboard focus.
;
; DOSUI_READ_KEY
;   Out: AL = ASCII byte (zero for an extended key), AH = BIOS scan code.
;   Waits for and consumes one BIOS key record through INT 16h/AH=00h.
;   AX and FLAGS are outputs; all other registers are preserved.
;
; DOSUI_MOUSE_INIT
;   Detects an installed INT 33h mouse driver and records the result in
;   DOSUI_MOUSE_PRESENT.  Vector 33h may be unset or point at a bare IRET
;   stub on a driverless system, so the vector is inspected through DOS
;   before function 0000h is trusted to answer with the FFFFh signature.
;   Call once after DOSUI_INIT; without a driver every mouse entry below
;   is inert and DOSUI_READ_EVENT degenerates to DOSUI_READ_KEY.
;
; DOSUI_MOUSE_SHUTDOWN
;   Resets a detected driver (function 0000h) so the cursor is hidden and
;   any handlers are detached before the application exits.  Harmless
;   without a driver.  Call before DOSUI_SHUTDOWN.
;
; DOSUI_MOUSE_PRESENT
;   Byte: nonzero once DOSUI_MOUSE_INIT has found a driver.
;
; DOSUI_READ_EVENT
;   Out: CF clear - AL/AH hold one BIOS key record, exactly as
;        DOSUI_READ_KEY returns it.
;        CF set   - the left mouse button was pressed with DH = cell row
;        and DL = cell column on the 80x25 grid.
;   Waits for one input event, polling the BIOS type-ahead flag and the
;   driver's left-press counter (function 0005h) around a HLT idle.  The
;   driver's cursor is shown only while this call is waiting and is hidden
;   again before it returns, so callers may draw to the screen freely
;   between events.  All registers other than the outputs are preserved.
;   When DOSUI_ALT_TAP is nonzero, a lone press and release of Alt, with
;   no key or click in between, is reported as the key record AX=3800h.
;
; DOSUI_ALT_TAP
;   byte: set nonzero to receive lone Alt taps from DOSUI_READ_EVENT.
;
; DOSUI_DRAW_MODAL
;   In:  DS:SI = DOSUI_RECT_SIZE-byte descriptor defined above.
;   Draws a double-line CP437 frame and fills its interior with spaces.  The
;   rectangle is inclusive and must be at least 2x2 cells wholly on screen.
;   The caller retains ownership of any covered cells; this primitive does
;   not allocate storage or save the background.

; DOSUI_MENU_BOX
;   In:  DS:SI = DOSUI_RECT_SIZE-byte descriptor.
;   Draws the single-line drop-down frame and its attribute-only shadow two
;   columns right and one row below. The shadow recolours covered cells and
;   leaves their characters intact.
;
; DOSUI_MENU_SHADOW
;   In:  DS:SI = DOSUI_RECT_SIZE-byte descriptor.
;   The shadow alone, for a frame drawn by other means.
;
; DOSUI_MENU_SEPARATOR
;   In:  DH = row, DL = left column, CX = full frame width.
;   Draws a horizontal rule with the tee characters at both frame edges.
;
; DOSUI_MENU_ITEM
;   In:  AL = item index, AH = accelerator offset within the text,
;        DH = row, DL = left column, DS:SI = zero-terminated item text.
;   Draws one drop-down row. The item is highlighted when its index equals
;   DOSUI_MENU_SELECTED, and dimmed when its bit is clear in the mask returned
;   by DOSUI_MENU_MASK_VECTOR. Set DOSUI_MENU_ITEM_WIDTH first.
;
; DOSUI_MENU_SELECTED    byte: index of the highlighted item.
; DOSUI_MENU_ITEM_WIDTH  byte: interior width of the open drop-down.
; DOSUI_MENU_MASK_VECTOR word: near proc returning AL = enabled-item bit mask.
;   These primitives render a menu; they do not decide what a menu contains.
;   Menu identity, command dispatch, accelerators and enable policy belong to
;   the application, whose menus differ in kind rather than in size.

IFNDEF DOSUI_IMPLEMENTATION
EXTRN DOSUI_INIT:NEAR
EXTRN DOSUI_SHUTDOWN:NEAR
EXTRN DOSUI_CLEAR:NEAR
EXTRN DOSUI_FILL_ROW:NEAR
EXTRN DOSUI_WRITE_Z:NEAR
EXTRN DOSUI_SET_CURSOR:NEAR
EXTRN DOSUI_SET_CURSOR_SHAPE:NEAR
EXTRN DOSUI_SHOW_CURSOR:NEAR
EXTRN DOSUI_HIDE_CURSOR:NEAR
EXTRN DOSUI_READ_KEY:NEAR
EXTRN DOSUI_MOUSE_INIT:NEAR
EXTRN DOSUI_MOUSE_SHUTDOWN:NEAR
EXTRN DOSUI_MOUSE_PRESENT:BYTE
EXTRN DOSUI_READ_EVENT:NEAR
EXTRN DOSUI_ALT_TAP:BYTE
EXTRN DOSUI_DRAW_MODAL:NEAR
EXTRN DOSUI_MENU_BOX:NEAR
EXTRN DOSUI_MENU_SHADOW:NEAR
EXTRN DOSUI_MENU_SEPARATOR:NEAR
EXTRN DOSUI_MENU_ITEM:NEAR
EXTRN DOSUI_MENU_SELECTED:BYTE
EXTRN DOSUI_MENU_ITEM_WIDTH:BYTE
EXTRN DOSUI_MENU_MASK_VECTOR:WORD
EXTRN DOSUI_ATTR_MAP:WORD
EXTRN DOSUI_ATTR_RAW:BYTE
EXTRN DOSUI_SHADOW_ATTR:BYTE
EXTRN DOSUI_ROWS:BYTE
EXTRN DOSUI_ROW_OFFSET:BYTE
EXTRN DOSUI_SAVE_SEGMENT:WORD
EXTRN DOSUI_SAVED_CELLS:WORD
EXTRN DOSUI_DRAW_SEGMENT:WORD
EXTRN DOSUI_FRAME_SEGMENT:WORD
EXTRN DOSUI_HAS_BACKBUFFER:BYTE
EXTRN DOSUI_BEGIN_FRAME:NEAR
EXTRN DOSUI_END_FRAME:NEAR
ENDIF
