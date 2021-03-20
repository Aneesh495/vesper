#!/bin/sh
set -eu

renderer=$1
out=$2
mkdir -p "$out"

for scene in afterglow veil prism iris shutter array; do
    prefix="$out/$scene"
    checkpoint="$prefix.vfilm"
    set -- render "scenes/$scene.vs" --backend vulkan --width 1920 --height 1080 --spp 1024 \
        --sampler stratified --seed 1789 --depth 8 --prefix "$prefix" --checkpoint "$checkpoint" \
        --checkpoint-rows 128 --progress
    if [ -f "$checkpoint" ]; then set -- "$@" --resume "$checkpoint"; fi
    printf '%s\n' "Rendering $scene at 1920x1080, 1024 spp"
    "$renderer" "$@" >"$prefix.json" 2>"$prefix.log"
done
