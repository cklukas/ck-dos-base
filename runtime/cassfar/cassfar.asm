; Clean-room SELECT/CASSFAR.LIB replacement (WP-5)
; Provides the 15 far procedures and data symbols referenced by SELECT.

_TEXT   SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  CS:_TEXT

        PUBLIC  BOXDR, COLOR, CORNER, DISPQ, GRAPH, GVIDO, HLPRD
        PUBLIC  INCHA, INDBC, INPUT, INSTRN, MBEEP, PANEL, SLCTP, WWRAP

BOXDR   PROC FAR
        retf
BOXDR   ENDP

COLOR   PROC FAR
        retf
COLOR   ENDP

CORNER  DB      0DAh, 0BFh, 0C0h, 0D9h, 0C4h, 0B3h

DISPQ   PROC FAR
        retf
DISPQ   ENDP

GRAPH   PROC FAR
        retf
GRAPH   ENDP

GVIDO   PROC FAR
        retf
GVIDO   ENDP

HLPRD   PROC FAR
        retf
HLPRD   ENDP

INCHA   PROC FAR
        retf
INCHA   ENDP

INDBC   PROC FAR
        retf
INDBC   ENDP

INPUT   PROC FAR
        retf
INPUT   ENDP

INSTRN  PROC FAR
        retf
INSTRN  ENDP

MBEEP   PROC FAR
        retf
MBEEP   ENDP

PANEL   PROC FAR
        retf
PANEL   ENDP

SLCTP   PROC FAR
        retf
SLCTP   ENDP

WWRAP   PROC FAR
        retf
WWRAP   ENDP

_TEXT   ENDS
        END
