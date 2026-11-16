struct sysVarsType
{
        long    pDPB ;          /* pointer to DPB chain                    */
        long    pSFT ;          /* pointer to System File Table            */
        long    pClock ;        /* pointer to clock device                 */
        long    pCon ;          /* pointer to CON device                   */
        unsigned maxSec ;       /* size of largest physical sector         */
        long    pBuf ;          /* pointer to buffer cache                 */
        long    pCDS ;          /* pointer to Current Dirs                 */
        long    pFCBSFT ;       /* pointer to FCB sftable                  */
        unsigned cKeep ;        /* number of un-recyclable FCBs            */
        char    cDrv ;          /* maximum number of physical drives       */
        char    cCDS ;          /* number of Current Dirs                  */
#ifdef DOS4
        long    pDEVHLP ;       /* ptr DOS DevHlp routine                  */
#endif
        long    pDEV ;          /* pointer to device list                  */
        unsigned attrNUL ;      /* attributes of NUL device                */
        unsigned stratNUL ;     /* strategy entry point of NUL dev         */
        unsigned intNUL ;       /* interrupt entry point of NUL dev        */
        char    namNUL[8] ;     /* name of NUL device                      */
        long    setver_tab ;    /* +34h pointer to SETVER program list     */
        unsigned a20_off ;      /* +38h A20 fixup offset in DOS CS         */
        unsigned hma_psp ;      /* +3Ah HMA PSP                            */
        unsigned buffers ;      /* +3Ch BUFFERS= x                         */
        unsigned buffers2 ;     /* +3Eh BUFFERS= y                         */
        char    boot_drive ;    /* +40h boot drive A=1 B=2...              */
        char    dwmove ;        /* +41h 1 if 386 machine                   */
        unsigned ext_mem ;      /* +42h extended memory size in KB         */
        char    fSplice ;       /* +44h TRUE => splices are in effect      */
} ;
