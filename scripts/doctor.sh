#!/bin/sh
set -eu
uname -sm
printf '%s\n' 'Workspace storage'
df -h .
printf '%s\n' 'Build tools'
for tool in cmake cc glslangValidator vulkaninfo python3; do
    command -v "$tool" || printf '%s missing\n' "$tool"
done
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary
fi
