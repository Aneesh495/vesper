#!/bin/sh
set -eu
uname -sm
if command -v sysctl >/dev/null && [ "$(uname -s)" = Darwin ]; then
    sysctl hw.ncpu hw.memsize
elif [ -r /proc/meminfo ]; then
    sed -n '1p' /proc/meminfo
    getconf _NPROCESSORS_ONLN
fi
printf '%s\n' 'Workspace storage'
df -h .
if [ -w . ]; then printf '%s\n' 'Workspace writable'; else printf '%s\n' 'Workspace read-only'; fi
if [ -r data/CIE_xyz_1931_2deg.csv ]; then
    printf '%s\n' 'CIE observer table present'
else
    printf '%s\n' 'CIE observer table missing'
fi
printf '%s\n' 'Build tools'
for tool in cmake cc glslangValidator vulkaninfo python3; do
    command -v "$tool" || printf '%s missing\n' "$tool"
done
cmake --version | sed -n '1p'
cc --version | sed -n '1p'
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary
    vulkaninfo 2>/dev/null | awk '/maxStorageBufferRange|shaderFloat64|maxComputeWorkGroupInvocations/ { print }'
fi
