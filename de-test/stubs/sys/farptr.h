/* Test stand-in: the far-pointer reads DE uses from DJGPP's <sys/farptr.h>. */
unsigned char _farpeekb(unsigned short selector, unsigned long offset);
unsigned short _farpeekw(unsigned short selector, unsigned long offset);
unsigned long _farpeekl(unsigned short selector, unsigned long offset);
