#ifndef _CKCL_CONIO_H
#define _CKCL_CONIO_H

int getch(void);
int getche(void);
int putch(int c);
int kbhit(void);
int inp(unsigned int port);
unsigned int inpw(unsigned int port);
int outp(unsigned int port, int value);
unsigned int outpw(unsigned int port, unsigned int value);

#endif
