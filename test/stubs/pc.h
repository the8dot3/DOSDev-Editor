/* Test stand-in: the declarations DE uses from DJGPP's <pc.h>. */
int ScreenRows(void);
int ScreenCols(void);
void ScreenUpdate(const void *_virtual_screen);
void ScreenUpdateLine(const void *_virtual_screen_line, int _row);
void ScreenRetrieve(void *_virtual_screen);
void ScreenSetCursor(int _row, int _col);
void ScreenGetCursor(int *_row, int *_col);
void ScreenClear(void);
/* (DJGPP declares this one in <sys/stat.h>.) */
void _fixpath(const char *_in, char *_out);
