#!/bin/sh
# Build Glim.exe with MinGW-w64 (Linux or an MSYS2 MINGW64 shell, where windres has no prefix)
set -e
WINDRES=$(command -v x86_64-w64-mingw32-windres || command -v windres)
"$WINDRES" res.rc -O coff -o res.o
x86_64-w64-mingw32-gcc -O2 -s $CFLAGS -municode -mwindows glim.c res.o -o Glim.exe -lcomctl32 -lole32 -lshell32 -luuid
echo "Built Glim.exe"
