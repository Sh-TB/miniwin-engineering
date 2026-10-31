#!/bin/bash
# SWEEP-031: build Wine 9.0's own notepad as a real PE64 (mingw-w64)
# Toolchain: user-space mingw-w64 debs (gcc-mingw-w64-x86-64 14.2 + binutils-mingw-w64 + mingw-w64-{common,x86-64-dev})
# Wine source: winehq.org wine-9.0.tar.xz, programs/notepad (main.c dialog.c + notepad.rc)
set -e
W=/home/z/my-project/workspace/wine-ref
B=$W/notepad-build
M=$W/toolchain/usr/x86_64-w64-mingw32/include
G=$W/toolchain/usr/lib/gcc/x86_64-w64-mingw32/14-posix/include
mkdir -p $B && cd $B
cp $W/wine-9.0/programs/notepad/{main.c,dialog.c,main.h,dialog.h,notepad_res.h,notepad.rc,notepad.ico,notepad.manifest} .
# patch 1: winternl.h not in mingw -> compat shim for RtlUshortByteSwap
python3 - <<'PY'
src = open('dialog.c').read()
src = src.replace('#include <winternl.h>', '#include "np_compat.h"')
open('dialog.c','w').write(src)
PY
cat > np_compat.h <<'H'
#ifndef NP_COMPAT_H
#define NP_COMPAT_H
#include <windows.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif
#ifndef RtlUshortByteSwap
static __inline unsigned short RtlUshortByteSwap(unsigned short x) { return (unsigned short)((x << 8) | (x >> 8)); }
#endif
#endif
H
# patch 2: wine msgctxt accelerator syntax -> plain virtual keys (binutils windres limitation)
python3 - <<'PY'
import re
src = open('notepad.rc').read()
src = re.sub(r'"#msgctxt#accelerator [^"]*#([A-Z])"', r'"\1"', src)
open('notepad.rc','w').write(src)
PY
export PATH=$W/toolchain/bin:$PATH
$W/toolchain/usr/bin/x86_64-w64-mingw32-windres \
  --preprocessor "$W/toolchain/usr/bin/x86_64-w64-mingw32-cpp-posix" \
  --preprocessor-arg -DWIN32 --preprocessor-arg -DRC_INVOKED --preprocessor-arg -nostdinc \
  --preprocessor-arg -I$M --preprocessor-arg -I$G --preprocessor-arg -I. \
  -I$M -I. notepad.rc -O coff -o rsrc.o
x86_64-w64-mingw32-gcc -O2 -DUNICODE -D_UNICODE -include np_compat.h \
  main.c dialog.c rsrc.o -o notepad_wine.exe -mwindows \
  -lcomctl32 -lcomdlg32 -lshell32 -lshlwapi -lgdi32
sha256sum notepad_wine.exe
