;	SCCSID = @(#)msconst.asm	1.4 85/09/12
;   Revision history
;      AN000  version 4.00  Jan. 1988
;      AN007  fake version check for IBMCACHE.COM
include mshead.asm
include version.inc

CODE		SEGMENT BYTE PUBLIC 'CODE'
	Extrn	LeaveDOS:NEAR
	Extrn	BadCall:FAR, OKCall:FAR
CODE		ENDS

Break <Initialized data and data used at DOS initialization>

;
; We need to identify the parts of the data area that are relevant to tasks
; and those that are relevant to the system as a whole.  Under 3.0, the system
; data will be gathered with the system code.  The process data under 2.x will
; be available for swapping and under 3.0 it will be allocated per-process.
;
; The data that is system data will be identified by [SYSTEM] in the comments
; describing that data item.
;

	AsmVars <Kanji, Debug, Redirector, ShareF>

CONSTANTS	SEGMENT WORD PUBLIC 'CONST'

	extrn	sfTabl:DWORD

	ORG	0

	EVEN
;
; WANGO!!!  The following word is used by SHARE and REDIR to determin data
; area compatability.  This location must be incremented EACH TIME the data
; area here gets mucked with.
;
; Also, do NOT change this position relative to DOSGROUP:0.
;
Public MSCT001S,MSCT001E
MSCT001S:
	I_am	DataVersion,WORD,<1>	;AC000; [SYSTEM] version number for DOS DATA

%out WARNING!!! Debug fields are being included!!!
	DB	"BUG "                  ; THIS FIELD MUST BE EVEN # OF BYTES
	I_am	BugTyp,WORD,<0>
	I_am	BugLev,WORD,<0>
include bugtyp.asm

	I_am	MYNUM,WORD,<0>		; [SYSTEM] A number that goes with MYNAME
	I_am	FCBLRU,WORD,<0> 	; [SYSTEM] LRU count for FCB cache
	I_am	OpenLRU,WORD,<0>	; [SYSTEM] LRU count for FCB cache opens
; NOTE: We include the decl of OEM_HANDLER in IBM DOS even though it is not used.
;	This allows the REDIRector to work on either IBM or MS-DOS.
	PUBLIC	OEM_HANDLER
OEM_HANDLER	DD	-1		; [SYSTEM] Pointer to OEM handler code
	I_am	LeaveAddr,WORD,<<OFFSET DOSGroup:LeaveDOS>> ; [SYSTEM]
	I_am	RetryCount,WORD,<3>	; [SYSTEM] Share retries
	I_am	RetryLoop,WORD,<1>	; [SYSTEM] Share retries
	I_am	LastBuffer,DWORD,<-1,-1>; [SYSTEM] Buffer queue recency pointer
	I_am	CONTPOS,WORD		; [SYSTEM] location in buffer of next read
	I_am	arena_head,WORD 	; [SYSTEM] Segment # of first arena in memory
; The following block of data is used by SYSINIT.  Do not change the order or
; size of this block
	PUBLIC	SYSINITVAR		; [SYSTEM]
SYSINITVAR  LABEL   WORD		; [SYSTEM]
	I_am	DPBHEAD,DWORD		; [SYSTEM] Pointer to head of DPB-FAT list
	I_am	sft_addr,DWORD,<<OFFSET DosGroup:sfTabl>,?> ; [SYSTEM] Pointer to first SFT table
	I_am	BCLOCK,DWORD		; [SYSTEM] The CLOCK device
	I_am	BCON,DWORD		; [SYSTEM] Console device entry points
	I_am	MAXSEC,WORD,<128>	; [SYSTEM] Maximum allowed sector size
	I_am	BUFFHEAD,DWORD		; [SYSTEM] Pointer to head of buffer queue
	I_am	CDSADDR,DWORD		; [SYSTEM] Pointer to curdir structure table
	I_am	sftFCB,DWORD		; [SYSTEM] pointer to FCB cache table
	I_am	KeepCount,WORD		; [SYSTEM] count of FCB opens to keep
	I_am	NUMIO,BYTE		; [SYSTEM] Number of disk tables
	I_am	CDSCOUNT,BYTE		; [SYSTEM] Number of CDS structures in above
; A fake header for the NUL device
	I_am	NULDEV,DWORD		; [SYSTEM] Link to rest of device list
	DW	DEVTYP OR ISNULL	; [SYSTEM] Null device attributes
	short_addr  SNULDEV		; [SYSTEM] Strategy entry point
	short_addr  INULDEV		; [SYSTEM] Interrupt entry point
	DB	"NUL     "              ; [SYSTEM] Name of null device
	I_am	SETVER_TAB,DWORD,<0,0>	; +34h [SYSTEM] pointer to SETVER program list
	I_am	A20_FIXUP_OFF,WORD,<0FFFFh> ; +38h [SYSTEM] offset in DOS CS of A20 fixup
	I_am	HMA_PSP,WORD,<0>	; +3Ah [SYSTEM] PSP of last program when DOS in HMA
	I_am	BUFFERS_PARM1,WORD,<0>	; +3Ch [SYSTEM] value of BUFFERS= x
	I_am	BUFFERS_PARM2,WORD,<0>	; +3Eh [SYSTEM] value of BUFFERS= ,y
	I_am	BOOTDRIVE,BYTE		; +40h [SYSTEM] the boot drive
	I_am	DDMOVE,BYTE,<0> 	; +41h [SYSTEM] 1 if 386+ machine
	I_am	EXT_MEM_SIZE,WORD,<0>	; +42h [SYSTEM] extended memory size in KB
	I_am	Splices,BYTE,<0>	; +44h [SYSTEM] TRUE => splices being done
	I_am	DOS_FLAG,BYTE,<0>	; [SYSTEM] 10h when DOS is in HMA, else 00h
	I_am	UMB_LINK_STATE,BYTE,<0>	; [SYSTEM] 0=unlinked, 1=UMBs linked to arena
	I_am	UMB_HEAD,WORD,<0>	; [SYSTEM] first UMB arena paragraph (0 if none)
	I_am	CONV_END_ARENA,WORD,<0>	; [SYSTEM] bridge MCB at top of conventional RAM

	PUBLIC	DEFAULT_SETVER_TABLE,DEFAULT_SETVER_END
DEFAULT_SETVER_TABLE LABEL BYTE
	DB	10,"WIN200.BIN",3,40
	DB	12,"IBMCACHE.SYS",3,40
	DB	0
	DB	(256 - ($ - DEFAULT_SETVER_TABLE)) DUP (0)
DEFAULT_SETVER_END LABEL BYTE

	PUBLIC	HASHINITVAR		; [SYSTEM]				;AN000;
HASHINITVAR  LABEL   WORD		; [SYSTEM]				;AN000;
	I_am	BUF_HASH_PTR,DWORD	; [SYSTEM] buffer Hash table addr	;AN000;
	I_am	BUF_HASH_COUNT,WORD,<1> ; [SYSTEM] number of Hash entries	;AN000;
	I_am	SC_CACHE_PTR,DWORD	; [SYSTEM] secondary cache pointer	;AN000;
	I_am	SC_CACHE_COUNT,WORD,<0> ; [SYSTEM] secondary cache count	;AN000;

IF	BUFFERFLAG

	I_am	BUF_EMS_SAFE_FLAG,BYTE,<1>	; indicates whether the page used by buffers is safe or not
	I_am	BUF_EMS_LAST_PAGE,4,<0,0,0,0>	; holds the last page above 640k
	I_am	BUF_EMS_FIRST_PAGE,4,<0,0,0,0>  ; holds the first page above 640K
	I_am	BUF_EMS_NPA640,WORD,<0>		; holds the number of pages above 640K

ENDIF

	I_am	BUF_EMS_MODE,BYTE,<-1>	; [SYSTEM] EMS mode			;AN000;
	I_am	BUF_EMS_HANDLE,WORD	; [SYSTEM] buffer EMS handle		;AN000;
	I_am	BUF_EMS_PAGE_FRAME,WORD ,<-1>;[SYSTEM] EMS page frame number	;AN000;
	I_am	BUF_EMS_SEG_CNT,WORD,<1>; [SYSTEM] EMS seg count		;AN000;
	I_am	BUF_EMS_PFRAME,WORD	; [SYSTEM] EMS page frame seg address	;AN000;
	I_am	BUF_EMS_RESERV,WORD,<0> ; [SYSTEM] reserved			;AN000;

IF	BUFFERFLAG
	I_am	BUF_EMS_MAP_BUFF,1,<0>	; this is not used to save the state of the 
					; of the buffers page. this one byte is
					; retained to keep the size of this data 
					; block the same.
ELSE
	I_am	BUF_EMS_MAP_BUFF,12,<0,0,0,0,0,0,0,0,0,0,0,0>	; map buufer	;AN000;
ENDIF

; End of SYSINITVar block

;
; Sharer jump table
;
PUBLIC	JShare
	EVEN
JShare	LABEL	DWORD
	DW	OFFSET DOSGROUP:BadCall, 0
	DW	OFFSET DOSGROUP:OKCall,  0  ;	1   MFT_enter
	DW	OFFSET DOSGROUP:OKCall,  0  ;	2   MFTClose
	DW	OFFSET DOSGROUP:BadCall, 0  ;	3   MFTclU
	DW	OFFSET DOSGROUP:BadCall, 0  ;	4   MFTCloseP
	DW	OFFSET DOSGROUP:BadCall, 0  ;	5   MFTCloN
	DW	OFFSET DOSGROUP:BadCall, 0  ;	6   set_block
	DW	OFFSET DOSGROUP:BadCall, 0  ;	7   clr_block
	DW	OFFSET DOSGROUP:OKCall,  0  ;	8   chk_block
	DW	OFFSET DOSGROUP:BadCall, 0  ;	9   MFT_get
	DW	OFFSET DOSGROUP:BadCall, 0  ;	10  ShSave
	DW	OFFSET DOSGROUP:BadCall, 0  ;	11  ShChk
	DW	OFFSET DOSGROUP:OKCall , 0  ;	12  ShCol
	DW	OFFSET DOSGROUP:BadCall, 0  ;	13  ShCloseFile
	DW	OFFSET DOSGROUP:BadCall, 0  ;	14  ShSU

MSCT001E:
CONSTANTS	ENDS
