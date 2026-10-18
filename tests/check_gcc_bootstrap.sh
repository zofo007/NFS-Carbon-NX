#!/usr/bin/env bash
set -euo pipefail
task_root=$(cygpath -u "${CARBON_WORKSPACE:?}")
cd "$task_root/toolchains/devkitA64-buildscripts"
. "$task_root/scripts/devkita64-config.sh"
host_triplet=$(./config.guess)
test "$host_triplet" = "$(gcc -dumpmachine)"
source_dir="$PWD/.devkitA64/gcc-15.2.0/fixincludes"
mkdir -p "$task_root/build/fixincludes-host-smoke"
cd "$task_root/build/fixincludes-host-smoke"
"$source_dir/configure" --srcdir="$source_dir" --target=aarch64-none-elf --build="$host_triplet" --host="$host_triplet"
grep -q '^#define SEPARATE_FIX_PROC 1$' config.h
make fixincl.o
echo 'PASS: fixincludes selects its Windows process implementation and compiles the failing unit.'
cd "$task_root/toolchains/devkitA64-buildscripts/.devkitA64/aarch64-none-elf/gcc/libcody"
g++ -fno-char8_t -g -O2 -fno-exceptions -fno-rtti -include config.h \
    -I../../../gcc-15.2.0/libcody -c ../../../gcc-15.2.0/libcody/buffer.cc \
    -o "$task_root/build/libcody-host-smoke.o"
g++ -std=c++11 -fno-char8_t -c "$task_root/tests/gcc-cxx11-probe.cpp" \
    -o "$task_root/build/libcody-cxx11-probe.o"
echo 'PASS: libcody source and its exact C++11 probe both accept the host compatibility flag.'
