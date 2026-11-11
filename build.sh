#!/bin/sh
# Builds ddraw.dll (i686) with clang + lld-link, no Windows SDK needed.
# Usage: ./build.sh            -> build/ddraw.dll
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p "$OUT/obj"
for d in kernel32 user32 gdi32 winmm; do
    llvm-dlltool -m i386 -k -d src/def/$d.def -l "$OUT/obj/$d.lib"
done
CFLAGS="--target=i686-pc-windows-msvc -O2 -msse2 -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter -Werror -std=c99 -fms-extensions"
OBJS=""
for f in util ddraw present patches patch_core input_core input hangwatch widescreen hires main; do
    clang $CFLAGS -c src/$f.c -o "$OUT/obj/$f.obj"
    OBJS="$OBJS $OUT/obj/$f.obj"
done
lld-link /nologo /dll /nodefaultlib /entry:DllMainCRTStartup /machine:x86 /subsystem:windows,5.01 \
    /def:src/ddraw.def /out:"$OUT/ddraw.dll" /safeseh:no /opt:ref \
    $OBJS "$OUT/obj/kernel32.lib" "$OUT/obj/user32.lib" "$OUT/obj/gdi32.lib" "$OUT/obj/winmm.lib"
echo "built $OUT/ddraw.dll"
