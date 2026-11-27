@ECHO OFF
REM DE, the DOSDev Editor: built in DOS, with DJGPP (in the Workbench).
REM Makes BUILD\DE.EXE, where the Linux build puts it.
REM src\MANUAL.H comes with the project; the Linux build makes it from DE.TXT.
REM (src\*.C: a DOS command line holds 127 characters, not every name.)
IF NOT EXIST BUILD\NUL MKDIR BUILD
gcc -Wall -O2 -o BUILD\DE.EXE -x c src\*.C
