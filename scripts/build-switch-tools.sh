#!/usr/bin/env bash
set -euo pipefail
task_root=$(cygpath -u "${CARBON_WORKSPACE:?}")
prefix="$task_root/toolchains/msys64/opt/devkitpro"
export CC=gcc CXX=g++
pacman -Q
for package in general-tools switch-tools; do
    cd "$task_root/toolchains/$package"
    if [ ! -f configure ]; then bash autogen.sh; fi
    mkdir -p "$task_root/build/$package"
    cd "$task_root/build/$package"
    "$task_root/toolchains/$package/configure" --prefix="$prefix/tools"
    make -j"${CARBON_BUILD_JOBS:-2}"
    make install
done
make -C "$task_root/toolchains/devkita64-rules" install
"$prefix/tools/bin/nacptool" --create Carbon 'Local build test' 0.0.0 "$task_root/build/switch-tools-smoke.nacp"
test "$(stat -c %s "$task_root/build/switch-tools-smoke.nacp")" = 16384
if "$prefix/tools/bin/elf2nro" >"$task_root/logs/elf2nro-usage.log" 2>&1; then
    echo 'elf2nro incorrectly accepted missing input.' >&2; exit 1
fi
grep '<elf-file> <nro-file>' "$task_root/logs/elf2nro-usage.log"
