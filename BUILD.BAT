@ECHO OFF
REM DE, the DOSDev Editor: built in DOS, with DJGPP (in the Workbench).
REM src\MANUAL.H comes with the project; the Linux build makes it from DE.TXT.
REM (src\*.C: a DOS command line holds 127 characters, not every name.)
gcc -Wall -O2 -o DE.EXE -x c src\*.C
