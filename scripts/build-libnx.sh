#!/usr/bin/env bash
set -euo pipefail
task_root=$(cygpath -u "${CARBON_WORKSPACE:?}")
export DEVKITPRO=/opt/devkitpro
make -C "$task_root/toolchains/libnx-source" -j"${CARBON_BUILD_JOBS:-2}"
make -C "$task_root/toolchains/libnx-source" install
