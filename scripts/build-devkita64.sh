#!/usr/bin/env bash
set -euo pipefail
task_root=$(cygpath -u "${CARBON_WORKSPACE:?}")
cd "$task_root/toolchains/devkitA64-buildscripts"
# Upstream's /opt prefix must resolve inside our portable MSYS2, not a system SDK.
case "$(cygpath -m /opt/devkitpro)" in
    "$CARBON_WORKSPACE"/toolchains/msys64/opt/devkitpro) ;;
    *) echo 'Unsafe devkitPro installation prefix.' >&2; exit 1 ;;
esac
case "$(realpath -m .devkitA64)" in
    "$task_root"/toolchains/devkitA64-buildscripts/.devkitA64) ;;
    *) echo 'Unsafe upstream build/cleanup directory.' >&2; exit 1 ;;
esac
for program in gcc g++ make patch makeinfo flex bison; do command -v "$program" >/dev/null; done
if [ -f config.sh ]; then
    cmp config.sh "$task_root/scripts/devkita64-config.sh" || { echo 'Preserving existing nonmatching config.sh.' >&2; exit 1; }
else
    cp "$task_root/scripts/devkita64-config.sh" config.sh
fi
# The pinned upstream script initializes some optional variables itself.
set +u
bash ./build-devkit.sh
compiler=/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++
test "$("$compiler" -dumpmachine)" = aarch64-none-elf
mkdir -p "$task_root/build"
"$compiler" --version
"$compiler" -std=c++23 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft \
    -ftls-model=local-exec -fPIE -D__SWITCH__ -DNX -c "$task_root/tests/arm64-smoke.cpp" \
    -o "$task_root/build/arm64-smoke.o"
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-readelf -h "$task_root/build/arm64-smoke.o" | grep 'Machine:.*AArch64'
