#!/bin/sh
# Builds ddraw.dll (i686) with clang + lld-link, no Windows SDK needed.
# Usage: ./build.sh            -> build/ddraw.dll, build/usnf97cfg.exe
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
for f in util ddraw present patches patch_core input_core input hangwatch widescreen hires terrain main; do
    clang $CFLAGS -c src/$f.c -o "$OUT/obj/$f.obj"
    OBJS="$OBJS $OUT/obj/$f.obj"
done
lld-link /nologo /dll /nodefaultlib /entry:DllMainCRTStartup /machine:x86 /subsystem:windows,5.01 \
    /def:src/ddraw.def /out:"$OUT/ddraw.dll" /safeseh:no /opt:ref \
    $OBJS "$OUT/obj/kernel32.lib" "$OUT/obj/user32.lib" "$OUT/obj/gdi32.lib" "$OUT/obj/winmm.lib"
echo "built $OUT/ddraw.dll"

# usnf97cfg.exe: the controls configurator (shares input_core.c)
for d in kernel32 user32 gdi32; do
    llvm-dlltool -m i386 -k -d src/cfg/def/$d.def -l "$OUT/obj/cfg_$d.lib"
done
clang $CFLAGS -c src/cfg/usnf97cfg.c -o "$OUT/obj/usnf97cfg.obj"
clang $CFLAGS -c src/input_core.c -o "$OUT/obj/cfg_input_core.obj"
lld-link /nologo /nodefaultlib /entry:WinMainCRTStartup /machine:x86 /subsystem:windows,5.01 \
    /out:"$OUT/usnf97cfg.exe" /safeseh:no /opt:ref /stack:0x100000,0x10000 \
    "$OUT/obj/usnf97cfg.obj" "$OUT/obj/cfg_input_core.obj" \
    "$OUT/obj/cfg_kernel32.lib" "$OUT/obj/cfg_user32.lib" "$OUT/obj/cfg_gdi32.lib"
echo "built $OUT/usnf97cfg.exe"
